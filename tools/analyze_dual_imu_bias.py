#!/usr/bin/env python3
"""Verify frozen gyro calibration on independent captures and compare both IMUs."""
import argparse
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path

from validate_dual_imu_capture import validate, validate_streams

SENSORS = ('icm42688', 'bmi270')
RAW = ('gx_dps', 'gy_dps', 'gz_dps')
CORRECTED = ('gx_corrected_dps', 'gy_corrected_dps', 'gz_corrected_dps')


def rows(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))


def means(data, fields):
    return [statistics.fmean(float(row[field]) for row in data) for field in fields]


def close_vectors(actual, expected):
    if len(actual) != len(expected) or any(not math.isclose(a, b, abs_tol=1e-7) for a, b in zip(actual, expected)):
        raise ValueError('Calibration or summary differs from independently recomputed CSV statistics')


def analyze(run, reference_profile, training_end):
    summary = json.loads((run / 'summary.json').read_text())
    if not summary.get('stationary_confirmed'):
        raise ValueError('Stationary confirmation missing')
    checks = validate(run, True)
    profile = json.loads((run / 'calibration.json').read_text())
    if profile != reference_profile or profile != summary['calibration_profile']:
        raise ValueError('Calibration changed between training and validation')
    result = {'run': str(run), 'elapsed_seconds': summary['elapsed_seconds'],
              'validation': checks, 'sensors': {}, 'phase_validation': {}}
    for phase in ('warmup', 'calibration'):
        if phase in summary['phases']:
            result['phase_validation'][phase] = validate_streams(run / phase, summary['phases'][phase], True)
    for name in SENSORS:
        if summary[name]['gyro_correction']['applied'] is not True or not summary['static_validation'][name]['passed']:
            raise ValueError(name + ': correction or static validation failed')
        if summary[name]['invalid_samples'] or summary[name]['saturated_samples']:
            raise ValueError(name + ': invalid capture')
        if summary['icm_spi' if name == 'icm42688' else 'bmi_spi']['errors']:
            raise ValueError(name + ': SPI error')
        data = rows(run / (name + '.csv'))
        if int(data[0]['host_start_ns']) <= training_end[name]:
            raise ValueError('Calibration and validation overlap or host monotonic clock was reset')
        close_vectors(summary[name]['gyro_correction']['bias_dps'], profile[name]['bias_dps'])
        if (run / 'calibration').is_dir():
            training = rows(run / 'calibration' / (name + '.csv'))
            close_vectors(means(training, RAW), profile[name]['bias_dps'])
            if int(training[-1]['host_end_ns']) >= int(data[0]['host_start_ns']):
                raise ValueError('Calibration and holdout samples overlap')
        raw_mean, corrected_mean = means(data, RAW), means(data, CORRECTED)
        raw_norm = math.sqrt(sum(v*v for v in raw_mean))
        corrected_norm = math.sqrt(sum(v*v for v in corrected_mean))
        midpoint = [(int(row['host_start_ns']) + int(row['host_end_ns'])) / 2e9 for row in data]
        groups = {}
        for row, timestamp in zip(data, midpoint):
            groups.setdefault(int((timestamp-midpoint[0]) / 5), []).append(row)
        blocks = [{'start_seconds': k*5, 'samples': len(v), 'raw_mean_dps': means(v, RAW),
                   'corrected_mean_dps': means(v, CORRECTED),
                   'temperature_c': statistics.fmean(float(row['temp_c']) for row in v)}
                  for k, v in sorted(groups.items())]
        integral = []
        for field in CORRECTED:
            integral.append(sum((float(a[field])+float(b[field]))*0.5*(tb-ta)
                                for a,b,ta,tb in zip(data,data[1:],midpoint,midpoint[1:])))
        result['sensors'][name] = {
            'samples': len(data), 'received_hz': summary[name]['received_hz'],
            'calibration_bias_dps': profile[name]['bias_dps'],
            'calibration_temperature_c': profile[name]['reference_temperature_c'],
            'raw_mean_dps': raw_mean, 'corrected_mean_dps': corrected_mean,
            'stddev_dps': [statistics.stdev(float(r[f]) for r in data) for f in CORRECTED],
            'corrected_rms_dps': [math.sqrt(statistics.fmean(float(r[f])**2 for r in data)) for f in CORRECTED],
            'raw_bias_norm_dps': raw_norm, 'corrected_bias_norm_dps': corrected_norm,
            'mean_bias_norm_reduction_percent': (1-corrected_norm/raw_norm)*100 if raw_norm else None,
            'temperature_mean_c': statistics.fmean(float(r['temp_c']) for r in data),
            'temperature_range_c': [min(float(r['temp_c']) for r in data), max(float(r['temp_c']) for r in data)],
            'host_time_integrated_residual_deg': integral,
            'blocks_5s': blocks,
            'csv_sha256': hashlib.sha256((run / (name+'.csv')).read_bytes()).hexdigest(),
        }
        if max(abs(v) for v in corrected_mean) > 0.05:
            raise ValueError(name + ': independent residual check failed')
    return result


def plot(result, output):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    fig, axes = plt.subplots(2, 2, figsize=(12, 7), constrained_layout=True)
    colors = ['#2878b5', '#e07a28']
    labels = ['ICM-42688-P', 'BMI270']
    for i, name in enumerate(SENSORS):
        sensor = result['sensors'][name]
        axes[0, 0].bar(np.arange(3)+(i-0.5)*0.34, sensor['raw_mean_dps'], 0.34, label=labels[i], color=colors[i])
        for axis, target in enumerate([axes[0, 1], axes[1, 0], axes[1, 1]]):
            blocks = [b for b in sensor['blocks_5s'] if b['samples'] >= 600]
            target.plot([b['start_seconds']+2.5 for b in blocks],
                        [b['corrected_mean_dps'][axis] for b in blocks], color=colors[i], label=labels[i])
    axes[0, 0].set(xticks=range(3), xticklabels=['X', 'Y', 'Z'], ylabel='Raw mean (deg/s)', title='Raw bias in independent validation')
    axes[0, 0].axhline(0, color='gray', lw=0.7)
    axes[0, 0].legend()
    for axis, target in enumerate([axes[0, 1], axes[1, 0], axes[1, 1]]):
        target.axhline(0, color='gray', lw=0.7)
        target.set(title='XYZ'[axis]+' residual: non-overlapping 5 s means', xlabel='Validation time (s)', ylabel='Corrected mean (deg/s)')
        target.grid(alpha=0.2)
        target.legend()
    fig.suptitle('Frozen calibration tested on separate stationary data\nNative sensor axes; identical ODR/range, different filter responses', fontsize=12)
    fig.savefig(output.with_suffix('.png'), dpi=170)
    fig.savefig(output.with_suffix('.pdf'))
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+', help='Completed, accepted stationary validation runs')
    parser.add_argument('--calibration-run', type=Path, help='Original training run; defaults to first validation run')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--plot', action='store_true')
    args = parser.parse_args()
    training_run = args.calibration_run or args.runs[0]
    if not (training_run / 'calibration').is_dir():
        raise ValueError('Original calibration CSVs are required')
    profile = json.loads((training_run / 'calibration.json').read_text())
    training_summary = json.loads((training_run / 'summary.json').read_text())
    if training_summary['cleanup_errors'] or not profile['accepted']:
        raise ValueError('Training run did not restore hardware or calibration was rejected')
    training_check = validate_streams(training_run / 'calibration', training_summary['phases']['calibration'], True)
    training_end = {}
    for name in SENSORS:
        if not training_summary['calibration_quality'][name]['accepted']:
            raise ValueError(name + ': original calibration quality rejected')
        data = rows(training_run / 'calibration' / (name + '.csv'))
        close_vectors(means(data, RAW), profile[name]['bias_dps'])
        training_end[name] = int(data[-1]['host_end_ns'])
    result = {'validated': True, 'method': 'frozen stationary mean subtraction; separate holdout data',
              'calibration_run': str(training_run), 'training_validation': training_check,
              'training_run_holdout_passed': training_summary['passed'],
              'training_run_holdout_error': training_summary.get('error'),
              'runs': [analyze(run, profile, training_end) for run in args.runs]}
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    if args.plot:
        plot(result['runs'][0], args.output.parent / 'bias_comparison')
    print(json.dumps({'validated': True, 'runs': len(result['runs']), 'output': str(args.output)}))


if __name__ == '__main__':
    main()
