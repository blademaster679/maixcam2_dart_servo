#!/usr/bin/env python3
"""Independently validate saved V1.1 capture CSVs against their summary.

Checks wire-value conversions, timestamps, row counts and summary statistics.
Does not certify sensor accuracy, calibration, synchronization or lossless capture.
"""
import argparse
import csv
import hashlib
import json
import math
import statistics
from pathlib import Path


def validate(path, stationary):
    summary = json.loads((path / 'summary.json').read_text())
    if not summary['passed'] or not summary['complete']:
        raise ValueError('Capture did not complete and pass')
    if summary['cleanup_errors'] or summary['board']['pinmux_before'] != summary['board']['pinmux_after']:
        raise ValueError('Board restoration failed')
    return validate_streams(path, summary, stationary)


def validate_streams(path, summary, stationary):
    """Validate both streams, also usable for warmup/calibration phase summaries."""
    result = {'validated': True, 'stationary_confirmed': stationary, 'sensors': {}}
    fields = ['ax_g', 'ay_g', 'az_g', 'gx_dps', 'gy_dps', 'gz_dps', 'temp_c']
    raw_fields = ['ax_raw', 'ay_raw', 'az_raw', 'gx_raw', 'gy_raw', 'gz_raw']
    for name, gyro_divisor in [('icm42688', 65.5), ('bmi270', 65.536)]:
        file = path / (name + '.csv')
        with file.open(newline='') as stream:
            rows = list(csv.DictReader(stream))
        if len(rows) != summary[name]['samples'] or len(rows) < 100:
            raise ValueError(name + ': wrong row count')
        previous_end = 0
        previous_sensor_time = None
        norm = []
        correction = summary[name].get('gyro_correction', {'applied': False, 'bias_dps': [0, 0, 0]})
        corrected_fields = ['gx_corrected_dps', 'gy_corrected_dps', 'gz_corrected_dps']
        for row in rows:
            start, end = int(row['host_start_ns']), int(row['host_end_ns'])
            if not previous_end < start <= end:
                raise ValueError(name + ': non-monotonic read timestamps')
            previous_end = end
            if row['valid'] != '1':
                raise ValueError(name + ': invalid sample')
            if row['saturated'] != '0':
                raise ValueError(name + ': saturated sample')
            for i, field in enumerate(raw_fields):
                raw = int(row[field])
                value = float(row[fields[i]])
                if not -32768 <= raw <= 32767 or not math.isfinite(value):
                    raise ValueError(name + ': non-finite or out of range sample')
                if not math.isclose(value, raw / (8192 if i < 3 else gyro_divisor), abs_tol=1e-7):
                    raise ValueError(name + ': conversion mismatch')
            if 'gyro_bias_applied' in row:
                if bool(int(row['gyro_bias_applied'])) != correction['applied']:
                    raise ValueError(name + ': inconsistent correction flag')
                for i, field in enumerate(corrected_fields):
                    expected = float(row[fields[i+3]]) - (correction['bias_dps'][i] if correction['applied'] else 0)
                    if not math.isclose(float(row[field]), expected, abs_tol=1e-7):
                        raise ValueError(name + ': incorrect gyro bias subtraction')
            elif correction['applied']:
                raise ValueError(name + ': corrected columns missing')
            norm.append(math.sqrt(sum(float(row[field]) ** 2 for field in fields[:3])))
            if name == 'bmi270':
                timestamp = int(row['sensor_time'])
                if not 0 <= timestamp < 2**24:
                    raise ValueError('BMI sensor time out of range')
                if previous_sensor_time is not None and (timestamp - previous_sensor_time) % 2**24 == 0:
                    raise ValueError('BMI sensor time did not advance')
                previous_sensor_time = timestamp
        mean = [statistics.fmean(float(row[field]) for row in rows) for field in fields]
        for actual, expected in zip(mean, summary[name]['mean']):
            if not math.isclose(actual, expected, abs_tol=1e-7):
                raise ValueError(name + ': CSV mean disagrees with summary')
        result['sensors'][name] = {
            'rows': len(rows), 'csv_sha256': hashlib.sha256(file.read_bytes()).hexdigest(),
            'mean': dict(zip(fields, mean)),
            'acceleration_norm_mean_g': statistics.fmean(norm),
            'static_gyro_bias_estimate_dps': mean[3:6] if stationary else None,
            'bias_applied': correction['applied'],
        }
        if 'gyro_bias_applied' in rows[0]:
            corrected_mean = [statistics.fmean(float(row[field]) for row in rows) for field in corrected_fields]
            corrected_std = [statistics.stdev(float(row[field]) for row in rows) for field in corrected_fields]
            for actual, expected in zip(corrected_mean, correction['mean_dps']):
                if not math.isclose(actual, expected, abs_tol=1e-7):
                    raise ValueError(name + ': corrected mean disagrees with summary')
            for actual, expected in zip(corrected_std, correction['stddev_dps']):
                if not math.isclose(actual, expected, abs_tol=1e-7):
                    raise ValueError(name + ': corrected standard deviation disagrees with summary')
            result['sensors'][name]['corrected_mean_dps'] = corrected_mean
            result['sensors'][name]['corrected_stddev_dps'] = corrected_std
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--stationary-confirmed', action='store_true')
    args = parser.parse_args()
    print(json.dumps(validate(args.run, args.stationary_confirmed), indent=2))
