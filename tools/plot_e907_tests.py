#!/usr/bin/env python3
"""Reconstruct command timing and ideal PWM; never represents scope samples."""
from pathlib import Path
import os
import re
os.environ.setdefault('MPLCONFIGDIR', '/tmp/e907_matplotlib')
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib import font_manager
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'reports/waveforms'
FONT = Path('/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf')
if FONT.exists():
    font_manager.fontManager.addfont(str(FONT))
    plt.rcParams['font.family'] = ['DejaVu Sans']
plt.rcParams.update({'axes.unicode_minus': False, 'font.size': 11,
                     'axes.spines.top': False, 'axes.spines.right': False})
TESTS = [
    ('small', 'Small excursion | 18:36', 'e907_single_servo2_video_20260918_183642.log', '#2563eb'),
    ('wider', 'Wider excursion | 18:47', 'e907_single_servo2_wider_20260918_184716.log', '#c2410c'),
]
# PWM period counts verified by the loader: 72072 / 24 MHz = 3.003 ms.
PERIOD_MS = 72072 / 24000

def save(fig, name):
    for ext in ('png', 'svg'):
        fig.savefig(OUT / f'{name}.{ext}', dpi=180, facecolor='white')
    plt.close(fig)

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for key, title, filename, color in TESTS:
        text = (ROOT / 'reports' / filename).read_text()
        pulses = [int(x) for x in re.findall(r'BENCH servo=2 PWM=5 pulse_us=(\d+)', text)]
        if len(pulses) != 9 or 'EXIT_STATUS 0' not in text:
            raise ValueError(f'Unexpected sequence/result: {filename}')
        edges = np.array([0, 3, 4.5, 6, 7.5, 9, 10.5, 12, 13.5, 15])
        fig, ax = plt.subplots(figsize=(12, 5.3))
        fig.subplots_adjust(left=.09, right=.97, top=.80, bottom=.25)
        fig.suptitle(title + ' | Commanded pulse width', fontsize=19, y=.96)
        fig.text(.5, .86, 'Servo 2 / PWM5 / H2 pin 15  |  Reconstructed commands, NOT measured waveforms', ha='center', color='#555555')
        ax.stairs(pulses, edges, baseline=None, linewidth=2.6, color=color)
        ax.axhline(1500, color='#94a3b8', linestyle='--', linewidth=1, zorder=0)
        for i, p in enumerate(pulses):
            ax.text((edges[i]+edges[i+1])/2, p+7, str(p), ha='center', color=color, fontsize=10)
        ax.axvline(15, color='#475569', linestyle=':')
        ax.text(15.12, 1510, 'PWM\noff', fontsize=10, color='#475569')
        ax.set(xlim=(-.15, 16), ylim=(1375, 1630), xticks=edges,
               xlabel='Nominal time / s (first output at t=0)', ylabel='Commanded high time / us')
        ax.grid(alpha=.16)
        fig.text(.09,.12,'Initial center: 3 s; each subsequent step: 1.5 s. Repeated keepalive commands (~100 ms) omitted.', fontsize=10)
        fig.text(.09,.075,'No per-command timestamps: timing is nominal. PWM stops at 15 s; no pulse is not a 0 us pulse.',fontsize=10)
        fig.text(.09,.03,'Source: '+filename,fontsize=9,color='#64748b')
        save(fig, key+'_timing')

        levels = sorted(set(pulses))
        fig, axs = plt.subplots(len(levels), 2, figsize=(12, 7.8),
                                gridspec_kw={'width_ratios':[3,1]})
        fig.subplots_adjust(left=.12, right=.96, top=.79, bottom=.20, hspace=.32, wspace=.18)
        fig.suptitle(title + ' | Ideal PWM illustration', fontsize=19,y=.97)
        fig.text(.5,.91,'Period: 3.003 ms (~333 Hz) | Logic levels only, NOT measured voltage',ha='center',color='#555555')
        axs[0,0].set_title('Steady-state examples: two periods each', fontsize=12)
        axs[0,1].set_title('Falling-edge detail', fontsize=12)
        for row, us in enumerate(levels):
            width=us/1000
            x=[0,width,width,PERIOD_MS,PERIOD_MS,PERIOD_MS+width,PERIOD_MS+width,2*PERIOD_MS]
            y=[1,1,0,0,1,1,0,0]
            for col, ax in enumerate(axs[row]):
                ax.plot(x,y,color=color,lw=2)
                ax.set_ylim(-.15,1.25)
                ax.set_yticks([0,1]); ax.set_yticklabels(['L','H'] if col==0 else [])
                ax.grid(alpha=.15)
                ax.set_xlim((-.03,2*PERIOD_MS) if col==0 else (1.35,1.65))
                if row<len(levels)-1: ax.tick_params(labelbottom=False)
            axs[row,0].set_ylabel(f'{us} μs',rotation=0,labelpad=40,va='center',color=color)
            axs[row,0].text(4.7,.75,f'Duty: {us/(PERIOD_MS*1000)*100:.2f}%',fontsize=9)
        for ax in axs[-1]: ax.set_xlabel('Time / ms')
        fig.text(.12,.12,'Each row is a separate pulse-width example, sorted by width; not concurrent channels or execution order.',fontsize=10)
        fig.text(.12,.08,'Glitches, jitter, voltage and start/stop transients are not modeled; hardware measurement is required.',fontsize=10)
        fig.text(.12,.035,'Source: '+filename+'; period = 72072 counts / 24 MHz.',fontsize=8,color='#64748b')
        save(fig,key+'_pwm')
        print(key, pulses)

if __name__ == '__main__':
    main()
