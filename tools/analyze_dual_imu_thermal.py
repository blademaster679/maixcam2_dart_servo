#!/usr/bin/env python3
"""Audit a raw stationary dual-IMU thermal run; retain native sensor axes.

No model is installed on the board. First-minute reference and chronological
thermal-model holdout are separate calculations. Acceleration drift includes
possible gravity projection changes and is not a six-position calibration.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np

SENSORS = ('icm42688', 'bmi270')
LABELS = {'icm42688': 'ICM-42688-P', 'bmi270': 'BMI270'}
COLORS = {'icm42688': '#1764ab', 'bmi270': '#c36717'}
STYLES = {'icm42688': '-', 'bmi270': '--'}
AXES = ('x', 'y', 'z')
FIELDS = tuple('a'+a+'_g' for a in AXES) + tuple('g'+a+'_dps' for a in AXES) + ('temp_c',)
RAW_FIELDS = tuple('a'+a+'_raw' for a in AXES) + tuple('g'+a+'_raw' for a in AXES)


def require(ok, message):
    if not ok:
        raise ValueError(message)


def values(data, fields):
    return np.column_stack([data[field] for field in fields])


def block_stats(t, y, duration, width=5):
    """All complete wall-time bins, never silently omit a sparse/noisy bin."""
    blocks = []
    for start in np.arange(0, duration-width+1e-6, width):
        lo, hi = np.searchsorted(t, (start, start+width))
        sample = y[lo:hi]
        require(len(sample) >= 2, f'Empty/insufficient {width}s bin at {start}s')
        blocks.append({'start_s': float(start), 'time_s': float(t[lo:hi].mean()),
                       'samples': len(sample), 'mean': sample.mean(axis=0).tolist(),
                       'stddev': sample.std(axis=0, ddof=1).tolist()})
    return blocks


def fit_thermal(t, temp, y, split_s=600):
    """Fit chronological 5s blocks, assess untouched later blocks.

    Slopes are descriptive correlations: a single natural-temperature run
    cannot identify causality or temperature hysteresis.
    """
    train, test = t < split_s, t >= split_s
    require(train.sum() >= 12 and test.sum() >= 12, 'Need at least 12 blocks in each split')
    reference = float(temp[train].mean())
    x = np.column_stack((np.ones(len(temp)), temp-reference))
    span = float(np.ptp(temp[train]))
    result = {'train_s': [0, split_s], 'holdout_s': [split_s, float(t[-1]+2.5)],
              'train_blocks': int(train.sum()), 'holdout_blocks': int(test.sum()),
              'reference_temperature_c': reference,
              'training_temperature_range_c': [float(temp[train].min()), float(temp[train].max())],
              'training_temperature_span_c': span,
              'holdout_outside_training_temperature_blocks': int(np.count_nonzero(
                  (temp[test] < temp[train].min()) | (temp[test] > temp[train].max()))),
              'minimum_exploratory_span_c': 1.0, 'deployment_approved': False,
              'limitation': 'One natural-temperature run confounds time and temperature; no independent thermal cycle.'}
    if span < 1e-6:
        result.update({'fit_available': False, 'reason': 'No resolved training temperature variation'})
        return result, None
    coeff, _, rank, _ = np.linalg.lstsq(x[train], y[train], rcond=None)
    require(rank == 2, 'Singular thermal fit')
    prediction = x @ coeff
    residual = y[test]-prediction[test]
    constant = y[test]-y[train].mean(axis=0)
    constant_rmse = np.sqrt(np.mean(constant**2, axis=0))
    linear_rmse = np.sqrt(np.mean(residual**2, axis=0))
    train_residual = y[train]-prediction[train]
    ss = np.sum((y[train]-y[train].mean(axis=0))**2, axis=0)
    r2 = np.divide(np.sum(train_residual**2, axis=0), ss, out=np.ones_like(ss), where=ss>0)
    result.update({'fit_available': True, 'intercept': coeff[0].tolist(),
                   'slope_per_c': coeff[1].tolist(), 'training_r2': (1-r2).tolist(),
                   'holdout_constant_rmse': constant_rmse.tolist(),
                   'holdout_linear_rmse': linear_rmse.tolist(),
                   'holdout_linear_mean_residual': residual.mean(axis=0).tolist(),
                   'holdout_improved': (linear_rmse < constant_rmse).tolist(),
                   'training_span_sufficient_for_exploratory_fit': span >= 1,
                   'temperature_time_correlation': float(np.corrcoef(t[train], temp[train])[0, 1])})
    return result, prediction


def integrate(t, y):
    return np.vstack((np.zeros(y.shape[1]), np.cumsum((y[1:]+y[:-1])*0.5*np.diff(t)[:, None], axis=0)))


def analyze_sensor(run, name, summary, start_ns, duration, previous_profile=None):
    path = run / (name+'.csv')
    data = np.genfromtxt(path, delimiter=',', names=True, dtype=np.float64)
    require(data.ndim == 1 and len(data) > 1, name+': empty capture')
    require(all(field in data.dtype.names for field in FIELDS+RAW_FIELDS), 'Missing raw fields')
    y = values(data, FIELDS)
    raw = values(data, RAW_FIELDS)
    require(np.isfinite(values(data, data.dtype.names)).all(), name+': nonfinite CSV field')
    require(np.all(data['host_end_ns'] < 2**53), 'Nanosecond timestamps exceed exact float64 integer range')
    require(np.all(data['host_end_ns'] >= data['host_start_ns']), 'Reversed read interval')
    require(np.all(data['host_start_ns'][1:] > data['host_end_ns'][:-1]), 'Overlapping same-sensor reads')
    mid = (data['host_start_ns']+data['host_end_ns'])/2
    t = (mid-start_ns)/1e9
    require(np.all(np.diff(t)>0), name+': unordered/duplicate read midpoint')
    require(t[0]>=0 and t[0]<0.1 and t[-1]>=duration-0.1 and t[-1]<duration+0.1,
            name+': incomplete capture interval')
    require(np.all(data['valid']==1) and np.all(data['saturated']==0), name+': invalid/saturated samples')
    require(np.all(data['gyro_bias_applied']==0), 'Analysis requires an uncompensated capture')
    require(np.array_equal(raw, np.rint(raw)), 'Nonintegral raw samples')
    require(np.all((raw >= -32768) & (raw <= 32767)), 'Raw sample outside int16 range')
    sensor_clock = None
    if name == 'bmi270':
        counter = data['sensor_time'].astype(np.int64)
        require(np.all(counter == data['sensor_time']) and np.all((counter >= 0) & (counter < 2**24)), 'Invalid BMI sensor time')
        increments = np.diff(counter) % 2**24
        require(np.all(increments > 0), 'BMI free-running clock did not advance')
        sensor_clock = {'observed_wraps': int(np.count_nonzero(np.diff(counter) < 0)),
                        'unwrapped_elapsed_seconds': float(increments.sum()/25600),
                        'host_midpoint_elapsed_seconds': float(t[-1]-t[0]),
                        'meaning': 'Free-running clock, not per-sample timing or a loss counter'}
    scales = np.array([8192.]*3 + [65.5 if name=='icm42688' else 65.536]*3)
    np.testing.assert_allclose(y[:, :6], raw/scales, rtol=1e-9, atol=1e-9)
    np.testing.assert_allclose(values(data, tuple('g'+a+'_corrected_dps' for a in AXES)), y[:, 3:6], atol=1e-9, rtol=1e-9)
    reported = summary[name]
    require(len(data)==reported['samples'], name+': count differs from summary')
    np.testing.assert_allclose(y.mean(axis=0), reported['mean'], rtol=1e-7, atol=1e-8)
    np.testing.assert_allclose(y.std(axis=0, ddof=1), reported['stddev'], rtol=1e-6, atol=1e-8)
    require(len(data)>=duration*120, name+': insufficient reception rate')
    norm = np.linalg.norm(y[:, :3], axis=1)
    blocks = block_stats(t, y, duration)
    bt = np.array([b['time_s'] for b in blocks])
    by = np.array([b['mean'] for b in blocks])
    bs = np.array([b['stddev'] for b in blocks])
    counts = np.array([b['samples'] for b in blocks])
    baseline, validation, end = t<60, t>=60, t>=duration-60
    baseline_mean = y[baseline].mean(axis=0)
    first_mean, last_mean = baseline_mean, y[end].mean(axis=0)
    residual = y[validation, 3:6]-baseline_mean[3:6]
    integral = integrate(t[validation], residual)
    residual_magnitude = np.linalg.norm(y[:, 3:6]-baseline_mean[3:6], axis=1)
    peak = int(np.argmax(residual_magnitude))
    block_changes = np.diff(by[:, 3:6], axis=0)
    largest_changes = []
    for axis in range(3):
        k = int(np.argmax(np.abs(block_changes[:, axis])))
        largest_changes.append({'axis': AXES[axis],
                                'first_window_start_s': blocks[k]['start_s'],
                                'next_window_start_s': blocks[k+1]['start_s'],
                                'change_dps': float(block_changes[k, axis]),
                                'temperature_change_c': float(by[k+1, 6]-by[k, 6])})
    pooled = np.sqrt(np.sum((counts-1)[:, None]*bs**2, axis=0)/np.sum(counts-1))
    diffs_ms = np.diff(mid)/1e6
    end_gaps_ms = np.diff(data['host_end_ns'])/1e6
    norm_blocks = block_stats(t, norm[:, None], duration)
    nb = np.array([b['mean'][0] for b in norm_blocks])
    accel_ref = baseline_mean[:3]
    cos_angle = (by[:, :3] @ accel_ref)/(np.linalg.norm(by[:, :3], axis=1)*np.linalg.norm(accel_ref))
    tilt = np.rad2deg(np.arccos(np.clip(cos_angle, -1, 1)))
    movement_bins = np.flatnonzero((bs[:, :3].max(axis=1)>0.01) | (bs[:, 3:6].max(axis=1)>0.2))
    gyro_model, gp = fit_thermal(bt, by[:, 6], by[:, 3:6], duration/2)
    accel_model, ap = fit_thermal(bt, by[:, 6], by[:, :3]*1000, duration/2)
    gyro_model['units'] = 'deg/s; slope deg/s/C'
    accel_model['units'] = 'mg; slope mg/C; intercept includes gravity'
    result = {
        'samples': len(data), 'received_hz': len(data)/summary['elapsed_seconds'],
        'first_sample_s': float(t[0]), 'last_sample_s': float(t[-1]),
        'invalid_samples': int(np.count_nonzero(data['valid']!=1)),
        'saturated_samples': int(np.count_nonzero(data['saturated']!=0)),
        'temperature_range_c': [float(y[:, 6].min()), float(y[:, 6].max())],
        'temperature_first_last_60s_mean_c': [float(first_mean[6]), float(last_mean[6])],
        'temperature_5s_mean_span_c': float(np.ptp(by[:, 6])),
        'raw_gyro_mean_dps': y[:, 3:6].mean(axis=0).tolist(),
        'raw_accel_mean_g': y[:, :3].mean(axis=0).tolist(),
        'raw_stddev': y.std(axis=0, ddof=1).tolist(), 'stddev_columns': FIELDS,
        'pooled_within_5s_stddev': pooled.tolist(),
        'first_60s_mean': first_mean.tolist(), 'last_60s_mean': last_mean.tolist(),
        'gyro_end_minus_start_dps': (last_mean[3:6]-first_mean[3:6]).tolist(),
        'gyro_5s_mean_span_dps': np.ptp(by[:, 3:6], axis=0).tolist(),
        'gyro_peak_relative_to_first_minute': {
            'time_s': float(t[peak]), 'magnitude_dps': float(residual_magnitude[peak]),
            'raw_gyro_dps': y[peak, 3:6].tolist()},
        'largest_adjacent_5s_gyro_changes': largest_changes,
        'frozen_first_60s_validation_mean_dps': residual.mean(axis=0).tolist(),
        'frozen_first_60s_validation_rms_dps': np.sqrt(np.mean(residual**2, axis=0)).tolist(),
        'frozen_first_60s_validation_5s_rms_dps': np.sqrt(np.mean((by[bt>=60, 3:6]-first_mean[3:6])**2, axis=0)).tolist(),
        'frozen_first_60s_integral_deg_at_end': integral[-1].tolist(),
        'integration_interval_s': [float(t[validation][0]), float(t[validation][-1])],
        'accel_end_minus_start_mg': ((last_mean[:3]-first_mean[:3])*1000).tolist(),
        'accel_5s_mean_span_mg': (np.ptp(by[:, :3], axis=0)*1000).tolist(),
        'accel_norm_mean_g': float(norm.mean()), 'accel_norm_range_g': [float(norm.min()), float(norm.max())],
        'accel_norm_first_last_60s_mean_g': [float(norm[baseline].mean()), float(norm[end].mean())],
        'accel_norm_5s_mean_span_mg': float(np.ptp(nb)*1000),
        'accel_norm_outside_0p9_1p1_g': int(np.count_nonzero((norm<0.9)|(norm>1.1))),
        'max_5s_gravity_direction_change_deg': float(tilt.max()),
        'motion_candidate_5s_starts': [blocks[i]['start_s'] for i in movement_bins],
        'motion_check_description': 'No samples removed. Candidate if per-axis 5s SD >10mg accel or >0.2deg/s gyro; not proof of immobility.',
        'bins_below_120hz': int(np.count_nonzero(counts<600)),
        'host_read_interval_ms': {'median': float(np.median(diffs_ms)), 'p99': float(np.quantile(diffs_ms, .99)),
                                  'max': float(diffs_ms.max()), 'over_10ms': int(np.count_nonzero(diffs_ms>10)),
                                  'over_25ms': int(np.count_nonzero(diffs_ms>25))},
        'max_host_end_gap_ms': float(end_gaps_ms.max()),
        'read_duration_us': {'median': float(np.median(data['host_end_ns']-data['host_start_ns'])/1000),
                             'p99': float(np.quantile(data['host_end_ns']-data['host_start_ns'], .99)/1000)},
        'sensor_clock': sensor_clock,
        'gyro_thermal_model': gyro_model, 'accel_thermal_model': accel_model,
        'csv_sha256': hashlib.sha256(path.read_bytes()).hexdigest(), 'blocks_5s': blocks,
    }
    if previous_profile is not None:
        bias = np.array(previous_profile[name]['bias_dps'])
        temp_ref = previous_profile[name]['reference_temperature_c']
        old_residual = y[:, 3:6]-bias
        result['previous_profile_offline_only'] = {
            'bias_dps': bias.tolist(), 'reference_temperature_c': temp_ref,
            'mean_residual_dps': old_residual.mean(axis=0).tolist(),
            'rms_residual_dps': np.sqrt(np.mean(old_residual**2, axis=0)).tolist(),
            'outside_original_3c_guard_samples': int(np.count_nonzero(np.abs(y[:, 6]-temp_ref)>3)),
            'integral_deg_at_end': integrate(t, old_residual)[-1].tolist()}
    plot_data = {'t': t, 'y': y, 'bt': bt, 'by': by, 'bs': bs, 'counts': counts,
                 'norm': norm, 'nb': nb, 'tilt': tilt, 'baseline': first_mean,
                 'integration_t': t[validation], 'integral': integral,
                 'gyro_prediction': gp, 'accel_prediction': ap}
    return result, plot_data


def save_plots(result, data, out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 10, 'axes.titlesize': 11,
                         'axes.spines.top': False, 'axes.spines.right': False,
                         'axes.grid': True, 'grid.alpha': .18, 'savefig.dpi': 170})
    duration = result['requested_seconds']
    def line(ax, name, x, y, **kw):
        return ax.plot(x, y, color=COLORS[name], linestyle=STYLES[name], label=LABELS[name], lw=1.5, **kw)
    def time_axis(ax):
        ax.set(xlim=(0, duration/60), xlabel='Elapsed time (min)', xticks=np.linspace(0, duration/60, 5))
    def save(fig, name):
        fig.savefig(out/(name+'.png'))
        fig.savefig(out/(name+'.pdf'))
        plt.close(fig)
    fig, axes = plt.subplots(2, 2, figsize=(12, 7.5), layout='constrained')
    for name, d in data.items():
        line(axes[0,0], name, d['bt']/60, d['by'][:,6])
        line(axes[0,1], name, d['bt']/60, d['nb'])
        line(axes[1,0], name, d['bt']/60, np.linalg.norm(d['by'][:,3:6], axis=1))
        line(axes[1,1], name, d['bt']/60, d['counts']/5)
    for ax in axes.flat:
        time_axis(ax); ax.legend()
    axes[0,0].set(title='Die temperature', ylabel='Temperature (°C)')
    axes[0,1].set(title='Acceleration magnitude (includes gravity)', ylabel='Magnitude (g)')
    axes[0,1].axhline(1, color='#555555', lw=.8)
    axes[1,0].set(title='Magnitude of mean raw angular rate', ylabel='Angular rate (deg/s)', ylim=(0,None))
    axes[1,1].set(title='Received register samples', ylabel='Samples / second')
    axes[1,1].axhline(200, color='#555555', lw=.8)
    fig.suptitle(f'{duration/60:g}-minute dual-IMU capture | natural temperature variation\nNon-overlapping 5 s means; polling is not lossless or hardware-synchronized')
    save(fig, 'overview')
    for kind, offset, scale, unit, title in (('gyro',3,1,'deg/s','Angular velocity'),('accel',0,1000,'mg','Acceleration')):
        fig, axes = plt.subplots(3, 2, figsize=(13, 10), layout='constrained')
        for axis in range(3):
            for name,d in data.items():
                series=d['by'][:,offset+axis]*scale
                line(axes[axis,0],name,d['bt']/60,series)
                line(axes[axis,1],name,d['bt']/60,series-d['baseline'][offset+axis]*scale)
            axes[axis,0].set(title=f'{AXES[axis].upper()} raw 5 s mean',ylabel=f'{title} ({unit})')
            axes[axis,1].set(title=f'{AXES[axis].upper()} change from frozen first 60 s',ylabel=f'Change ({unit})')
            axes[axis,1].axhline(0,color='#555555',lw=.8)
            axes[axis,1].axvspan(0,1,color='#aaaaaa',alpha=.15)
            for ax in axes[axis]: time_axis(ax); ax.legend()
        note = 'Acceleration changes are not absolute zero-g offsets' if kind == 'accel' else 'Frozen first-minute reference; no online bias adaptation'
        fig.suptitle(title+' | native sensor axes, not aligned\nIndependent row scales; '+note)
        save(fig,kind+'_time')
    fig, axes=plt.subplots(3,2,figsize=(13,10),layout='constrained')
    for axis in range(3):
        for name,d in data.items():
            line(axes[axis,0],name,d['bt']/60,d['bs'][:,axis+3])
            line(axes[axis,1],name,d['bt']/60,d['bs'][:,axis]*1000)
        axes[axis,0].set(title=f'{AXES[axis].upper()} gyro within-window SD',ylabel='SD (deg/s)',ylim=(0,None))
        axes[axis,1].set(title=f'{AXES[axis].upper()} accel within-window SD',ylabel='SD (mg)',ylim=(0,None))
        for ax in axes[axis]:time_axis(ax);ax.legend()
    fig.suptitle('Short-term variation in each 5 s window\nCurrent configured bandwidths; not a noise-density or sensor-grade comparison')
    save(fig,'noise_time')
    fig, axes=plt.subplots(3,1,figsize=(12,8),layout='constrained')
    for axis,ax in enumerate(axes):
        for name,d in data.items():
            line(ax,name,d['integration_t'][::200]/60,d['integral'][::200,axis])
        ax.axhline(0,color='#555555',lw=.8); time_axis(ax);ax.legend()
        ax.set(title=AXES[axis].upper()+' component',ylabel='Integral (deg)')
    fig.suptitle('Gyro integral relative to the frozen first-minute mean\nIntegration starts at 60 s; diagnostic component integrals, not measured attitude error')
    save(fig,'gyro_integral')
    for kind, offset, scale, unit in (('gyro',3,1,'deg/s'),('accel',0,1000,'mg')):
        fig,axes=plt.subplots(3,2,figsize=(13,10),layout='constrained')
        for col,name in enumerate(SENSORS):
            d=data[name]; model=result['sensors'][name][kind+'_thermal_model']
            for axis in range(3):
                ax=axes[axis,col]; val=d['by'][:,offset+axis]*scale
                for mask,marker,label in ((d['bt']<duration/2,'o',f'First {duration/120:g} min: fit'),(d['bt']>=duration/2,'x',f'Last {duration/120:g} min: holdout')):
                    ax.scatter(d['by'][mask,6],val[mask],s=13,marker=marker,label=label,color=COLORS[name],alpha=.75)
                if model['fit_available']:
                    tr=np.array(model['training_temperature_range_c'])
                    ax.plot(tr,model['intercept'][axis]+model['slope_per_c'][axis]*(tr-model['reference_temperature_c']),color='#333333',lw=1,label='Training fit')
                ax.set(title=f'{LABELS[name]} / {AXES[axis].upper()}',xlabel='Own die temperature (°C)',ylabel=f'5 s mean ({unit})');ax.legend(fontsize=8)
        fig.suptitle(kind.capitalize()+' vs temperature | exploratory association\nIndependent panel scales; one heating history cannot establish a deployable thermal model')
        save(fig,kind+'_temperature')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run',type=Path)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--previous-profile',type=Path)
    parser.add_argument('--plot',action='store_true')
    args=parser.parse_args()
    summary=json.loads((args.run/'summary.json').read_text())
    require(summary.get('passed') and summary.get('complete'), 'Capture incomplete or failed')
    require(not summary['cleanup_errors'], 'Hardware cleanup failed')
    for bus in ('icm_spi','bmi_spi'):require(summary[bus]['errors']==0, bus+': transfer errors')
    require(summary['board']['pinmux_before']==summary['board']['pinmux_after'], 'Pad restore mismatch')
    require(summary['identity']['icm_errors']==0 and summary['identity']['bmi_errors']==0, 'Identity errors')
    duration=summary['requested_seconds']
    require(duration>=120, 'Run too short for chronological analysis')
    require(abs(summary['elapsed_seconds']-duration)<0.1, 'Requested duration not completed')
    start=summary['phase_start_monotonic_ns']['validation']
    previous=json.loads(args.previous_profile.read_text()) if args.previous_profile else None
    if previous is not None:
        require(previous['board_hostname']==summary['board']['hostname'], 'Previous profile is from another board')
        for sensor in ('icm','bmi'):
            require(previous[sensor+'_configuration']==summary[sensor+'_configuration'], 'Previous profile configuration differs')
    result={'validated':True,'requested_seconds':duration,'elapsed_seconds':summary['elapsed_seconds'],
            'phase_start_monotonic_ns':start,'reference_interval_s':[0,60],
            'temperature_model_split_s':duration/2,'native_sensor_axes':True,
            'all_samples_retained':True,'sensor_axis_alignment_known':False,
            'absolute_accel_bias_identifiable':False,'sensors':{},
            'summary_sha256':hashlib.sha256((args.run/'summary.json').read_bytes()).hexdigest()}
    plots={}
    for name in SENSORS:
        result['sensors'][name],plots[name]=analyze_sensor(args.run,name,summary,start,duration,previous)
    candidates = [set(result['sensors'][name]['motion_candidate_5s_starts']) for name in SENSORS]
    joint = candidates[0] | candidates[1]
    result['stationarity_screen'] = {
        'passed': not joint,
        'candidate_union_5s_starts': sorted(joint),
        'candidate_union_count': len(joint),
        'coincident_candidate_count': len(candidates[0] & candidates[1]),
        'total_windows': len(plots[SENSORS[0]]['bt']),
        'interpretation': 'Integrity validation is separate from stationarity. Coincident fluctuations suggest common disturbance; cause unproven.'}
    result['reference_stationarity_screen'] = {
        'interval_s': [0, 60],
        'passed': not any(t < 60 for t in joint),
        'candidate_union_5s_starts': sorted(t for t in joint if t < 60),
        'calibration_approved': False,
        'interpretation': 'The frozen mean is a comparison reference, not an accepted bias calibration. A passed screen alone cannot establish stationarity.'}
    # A labeled sensitivity result only; the primary results retain every sample.
    for name in SENSORS:
        sensor = result['sensors'][name]
        blocks = sensor['blocks_5s']
        quiet = np.array([b['start_s'] not in joint for b in blocks])
        count, std = plots[name]['counts'][quiet], plots[name]['bs'][quiet]
        if len(count):
            sensor['joint_non_candidate_windows'] = {
                'windows': int(quiet.sum()), 'seconds': int(quiet.sum()*5),
                'pooled_within_5s_stddev': np.sqrt(np.sum((count-1)[:,None]*std**2,axis=0)/np.sum(count-1)).tolist(),
                'meaning': 'Secondary subset using the same time mask for both sensors. Not proof of pure sensor noise; full-run results unchanged.'}
    args.output.mkdir(parents=True,exist_ok=True)
    (args.output/'analysis.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    for name in SENSORS:
        with (args.output/(name+'_5s.csv')).open('w',newline='') as f:
            w=csv.writer(f);w.writerow(['start_s','time_s','samples']+list(FIELDS)+[s+'_std' for s in FIELDS])
            for b in result['sensors'][name]['blocks_5s']:w.writerow([b['start_s'],b['time_s'],b['samples']]+b['mean']+b['stddev'])
    if args.plot:save_plots(result,plots,args.output)
    print(json.dumps({'validated':True,'sensors':{n:{k:r[k] for k in ('samples','received_hz','temperature_range_c','gyro_end_minus_start_dps','accel_end_minus_start_mg','motion_candidate_5s_starts')} for n,r in result['sensors'].items()}},indent=2))


if __name__=='__main__':main()
