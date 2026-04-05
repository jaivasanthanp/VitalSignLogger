#!/usr/bin/env python3
"""
analyse_log.py — Offline post-session analysis for VitalSignLogger CSV logs.

Usage:
    python analyse_log.py vital_log_20260330_120000.csv
    python analyse_log.py vital_log_20260330_120000.csv --no-plot

Output:
    - Console summary: duration, mean/max HR, peak g-force, temp stats, motion breakdown
    - analysis_<logname>.png saved alongside this script
"""

import sys
import os
import argparse
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

MOTION_LABELS = {0: 'Rest', 1: 'Walking', 2: 'Arm Raised'}
MOTION_COLORS = {0: '#2196F3', 1: '#4CAF50', 2: '#FF9800'}


def load(path: str) -> pd.DataFrame:
    df = pd.read_csv(path)
    required = {'time_s', 'bpm', 'ax', 'ay', 'az', 'temp', 'motion'}
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"CSV missing columns: {missing}")
    df['g_total'] = np.sqrt(df.ax**2 + df.ay**2 + df.az**2)
    return df


def print_summary(df: pd.DataFrame, path: str):
    hr = df.bpm[df.bpm > 0]
    duration = df.time_s.max()

    print(f"\n{'='*50}")
    print(f"  Log: {os.path.basename(path)}")
    print(f"{'='*50}")
    print(f"  Duration      : {duration:.1f} s  ({duration/60:.1f} min)")
    print(f"  Total frames  : {len(df):,}  ({len(df)/duration:.0f} Hz avg)")
    print()
    print(f"  Heart Rate")
    if len(hr) > 0:
        print(f"    Mean         : {hr.mean():.1f} BPM")
        print(f"    Median       : {hr.median():.1f} BPM")
        print(f"    Min / Max    : {hr.min()} / {hr.max()} BPM")
        print(f"    Std dev      : {hr.std():.1f} BPM")
        print(f"    Coverage     : {len(hr)/len(df)*100:.1f}% of session")
    else:
        print(f"    No valid HR samples (finger never placed?)")
    print()
    print(f"  Acceleration")
    print(f"    Mean |g|     : {df.g_total.mean():.3f} g")
    print(f"    Peak |g|     : {df.g_total.max():.3f} g")
    print(f"    Std dev |g|  : {df.g_total.std():.3f} g")
    print()
    print(f"  Temperature")
    print(f"    Mean         : {df.temp.mean():.1f} °C")
    print(f"    Range        : {df.temp.min():.1f} – {df.temp.max():.1f} °C")
    print()
    print(f"  Motion Classification")
    for cls, label in MOTION_LABELS.items():
        pct = (df.motion == cls).mean() * 100
        bar = '█' * int(pct / 2)
        print(f"    {label:<12}: {pct:5.1f}%  {bar}")
    print(f"{'='*50}\n")


def plot(df: pd.DataFrame, path: str, save: bool = True):
    fig = plt.figure(figsize=(14, 10), facecolor='#1a1a2e')
    stem = os.path.splitext(os.path.basename(path))[0]
    fig.suptitle(f'VitalSignLogger Analysis — {stem}',
                 fontsize=13, fontweight='bold', color='white')

    gs = gridspec.GridSpec(3, 2, figure=fig,
                           left=0.07, right=0.97,
                           top=0.93, bottom=0.07,
                           hspace=0.45, wspace=0.3)

    axes = [fig.add_subplot(gs[i, j]) for i in range(3) for j in range(2)]
    for a in axes:
        a.set_facecolor('#0d0d1a')
        a.tick_params(colors='#aaaaaa', labelsize=8)
        for sp in a.spines.values():
            sp.set_edgecolor('#333355')

    t = df.time_s

    # 1 — HR over time
    hr_valid = df.bpm.copy()
    hr_valid[hr_valid == 0] = np.nan
    axes[0].plot(t, hr_valid, color='#ff4444', lw=1, label='HR')
    axes[0].set_ylabel('BPM', color='#aaaaaa', fontsize=9)
    axes[0].set_title('Heart Rate', color='#cccccc', fontsize=10)
    axes[0].set_ylim(bottom=0)
    axes[0].grid(alpha=0.2, color='#444466')

    # 2 — Acceleration axes
    axes[1].plot(t, df.ax, color='#4fc3f7', lw=0.8, label='ax', alpha=0.9)
    axes[1].plot(t, df.ay, color='#81c784', lw=0.8, label='ay', alpha=0.9)
    axes[1].plot(t, df.az, color='#ffb74d', lw=0.8, label='az', alpha=0.9)
    axes[1].set_ylabel('Accel (g)', color='#aaaaaa', fontsize=9)
    axes[1].set_title('Acceleration', color='#cccccc', fontsize=10)
    axes[1].legend(loc='upper right', fontsize=7, facecolor='#1a1a2e',
                   labelcolor='white', framealpha=0.5)
    axes[1].grid(alpha=0.2, color='#444466')

    # 3 — Total g-force
    axes[2].plot(t, df.g_total, color='#ce93d8', lw=1)
    axes[2].axhline(1.0, color='#444466', lw=0.8, ls='--', label='1 g')
    axes[2].set_ylabel('|g| (g)', color='#aaaaaa', fontsize=9)
    axes[2].set_title('Total Acceleration Magnitude', color='#cccccc', fontsize=10)
    axes[2].legend(loc='upper right', fontsize=7, facecolor='#1a1a2e',
                   labelcolor='white', framealpha=0.5)
    axes[2].grid(alpha=0.2, color='#444466')

    # 4 — Motion class timeline
    colors = [MOTION_COLORS.get(int(m), 'grey') for m in df.motion]
    axes[3].scatter(t, df.motion, c=colors, s=1, alpha=0.6)
    axes[3].set_yticks([0, 1, 2])
    axes[3].set_yticklabels(['Rest', 'Walk', 'Arm Up'],
                             color='#aaaaaa', fontsize=8)
    axes[3].set_title('Motion Class', color='#cccccc', fontsize=10)
    axes[3].grid(alpha=0.2, color='#444466', axis='x')

    # 5 — Temperature
    axes[4].plot(t, df.temp, color='#80cbc4', lw=1)
    axes[4].set_ylabel('°C', color='#aaaaaa', fontsize=9)
    axes[4].set_xlabel('Time (s)', color='#aaaaaa', fontsize=8)
    axes[4].set_title('Board Temperature', color='#cccccc', fontsize=10)
    axes[4].grid(alpha=0.2, color='#444466')

    # 6 — Motion class pie chart
    counts = [( df.motion == cls).sum() for cls in [0, 1, 2]]
    labels_pie = [f"{MOTION_LABELS[c]}\n{counts[i]/len(df)*100:.1f}%"
                  for i, c in enumerate([0, 1, 2])]
    wedge_colors = [MOTION_COLORS[c] for c in [0, 1, 2]]
    wedges, _ = axes[5].pie(counts, labels=labels_pie, colors=wedge_colors,
                            startangle=90, textprops={'color': 'white', 'fontsize': 9})
    axes[5].set_title('Motion Distribution', color='#cccccc', fontsize=10)

    for a in axes[:5]:
        a.set_xlabel('Time (s)', color='#aaaaaa', fontsize=8)
        a.set_xlim(t.min(), t.max())

    out = os.path.join(os.path.dirname(os.path.abspath(path)),
                       f'analysis_{stem}.png')
    if save:
        plt.savefig(out, dpi=150, facecolor=fig.get_facecolor())
        print(f"Plot saved to {out}")
    plt.show()


def main():
    parser = argparse.ArgumentParser(description='Analyse VitalSignLogger CSV log')
    parser.add_argument('csv', help='Path to vital_log_*.csv file')
    parser.add_argument('--no-plot', action='store_true',
                        help='Print summary only, skip plot')
    args = parser.parse_args()

    if not os.path.isfile(args.csv):
        print(f"ERROR: file not found: {args.csv}")
        raise SystemExit(1)

    df = load(args.csv)
    print_summary(df, args.csv)
    if not args.no_plot:
        plot(df, args.csv)


if __name__ == '__main__':
    main()
