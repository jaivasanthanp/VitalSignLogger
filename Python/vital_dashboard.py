#!/usr/bin/env python3
"""
vital_dashboard.py — Live dashboard for STM32U575 VitalSignLogger

Reads HR,bpm,ax,ay,az,gx,gy,gz,temp,motion,ts_ms CSV frames from UART.
Plots live HR and accelerometer traces, shows current motion class.
Saves every received frame to a timestamped CSV log.

Usage:
    pip install pyserial matplotlib numpy
    python vital_dashboard.py

Edit PORT below to match your ST-Link VCP (check Device Manager on Windows).
"""

import serial
import time
import csv
import os
from collections import deque
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import matplotlib.gridspec as gridspec
import numpy as np

# ── Configuration ──────────────────────────────────────────────────────────────
PORT  = 'COM6'     # Windows: 'COM6', 'COM3', etc.  Linux/Mac: '/dev/ttyACM0'
BAUD  = 115200
N     = 500        # samples shown in rolling window (5 s at 100 Hz)
LOG   = f"vital_log_{time.strftime('%Y%m%d_%H%M%S')}.csv"

MOTION_LABELS = {0: 'REST', 1: 'WALKING', 2: 'ARM UP'}
MOTION_COLORS = {0: '#2196F3', 1: '#4CAF50', 2: '#FF9800'}

# ── Rolling buffers ─────────────────────────────────────────────────────────────
bpm_q = deque([0] * N, maxlen=N)
ax_q  = deque([0] * N, maxlen=N)
ay_q  = deque([0] * N, maxlen=N)
az_q  = deque([0] * N, maxlen=N)
g_q   = deque([0] * N, maxlen=N)

# ── State ───────────────────────────────────────────────────────────────────────
cur_bpm    = 0
cur_motion = 0
cur_temp   = 0.0
frame_count = 0
t0 = time.time()

# ── Serial + CSV ────────────────────────────────────────────────────────────────
try:
    ser = serial.Serial(PORT, BAUD, timeout=0.05)
    print(f"Opened {PORT} at {BAUD} baud")
except serial.SerialException as e:
    print(f"ERROR: Cannot open {PORT}: {e}")
    print("Check Device Manager for the correct COM port and edit PORT in this script.")
    raise SystemExit(1)

log_file = open(LOG, 'w', newline='')
log_writer = csv.writer(log_file)
log_writer.writerow(['time_s', 'bpm', 'ax', 'ay', 'az',
                     'gx', 'gy', 'gz', 'temp', 'motion', 'ts_ms'])
print(f"Logging to {LOG}")


def parse(line: str):
    """Parse one CSV line. Returns dict or None on error."""
    try:
        p = line.strip().split(',')
        if len(p) < 11 or p[0] != 'HR':
            return None
        return {
            'bpm':    int(p[1]),
            'ax':     float(p[2]),
            'ay':     float(p[3]),
            'az':     float(p[4]),
            'gx':     float(p[5]),
            'gy':     float(p[6]),
            'gz':     float(p[7]),
            'temp':   float(p[8]),
            'motion': int(p[9]),
            'ts_ms':  int(p[10]),
        }
    except (ValueError, IndexError):
        return None


# ── Figure layout ───────────────────────────────────────────────────────────────
fig = plt.figure(figsize=(14, 8), facecolor='#1a1a2e')
fig.suptitle('STM32U575 Vital-Sign & Motion Logger',
             fontsize=14, fontweight='bold', color='white')

gs = gridspec.GridSpec(2, 2, figure=fig,
                       left=0.07, right=0.97,
                       top=0.91, bottom=0.08,
                       hspace=0.38, wspace=0.28)

ax_hr   = fig.add_subplot(gs[0, 0])   # Heart rate
ax_acc  = fig.add_subplot(gs[1, 0])   # Accelerometer
ax_info = fig.add_subplot(gs[:, 1])   # Info panel

for a in (ax_hr, ax_acc):
    a.set_facecolor('#0d0d1a')
    a.tick_params(colors='#aaaaaa', labelsize=8)
    for spine in a.spines.values():
        spine.set_edgecolor('#333355')

ax_info.set_facecolor('#0d0d1a')
ax_info.axis('off')

x = list(range(N))

# HR plot
l_bpm, = ax_hr.plot(x, list(bpm_q), color='#ff4444', lw=1.5, label='HR (bpm)')
ax_hr.set_ylim(0, 200)
ax_hr.set_xlim(0, N)
ax_hr.set_ylabel('BPM', color='#aaaaaa', fontsize=9)
ax_hr.set_title('Heart Rate', color='#cccccc', fontsize=10)
ax_hr.grid(alpha=0.2, color='#444466')

bpm_txt = ax_hr.text(0.03, 0.82, '', transform=ax_hr.transAxes,
                     fontsize=22, color='#ff4444', fontweight='bold')

# Accel plot
l_ax, = ax_acc.plot(x, list(ax_q), color='#4fc3f7', lw=1, label='ax')
l_ay, = ax_acc.plot(x, list(ay_q), color='#81c784', lw=1, label='ay')
l_az, = ax_acc.plot(x, list(az_q), color='#ffb74d', lw=1, label='az')
ax_acc.set_ylim(-2.5, 2.5)
ax_acc.set_xlim(0, N)
ax_acc.set_ylabel('Accel (g)', color='#aaaaaa', fontsize=9)
ax_acc.set_xlabel(f'Last {N} samples  ({N//100} s)', color='#aaaaaa', fontsize=8)
ax_acc.set_title('Accelerometer', color='#cccccc', fontsize=10)
ax_acc.grid(alpha=0.2, color='#444466')
ax_acc.legend(loc='upper right', fontsize=7, facecolor='#1a1a2e',
              labelcolor='white', framealpha=0.6)

# Info panel text objects
motion_bg = ax_info.add_patch(
    plt.Rectangle((0.05, 0.55), 0.9, 0.35, transform=ax_info.transAxes,
                  color=MOTION_COLORS[0], alpha=0.25, zorder=0))

t_motion = ax_info.text(0.5, 0.73, 'REST', transform=ax_info.transAxes,
                        ha='center', va='center',
                        fontsize=36, fontweight='bold', color=MOTION_COLORS[0])

t_temp = ax_info.text(0.5, 0.48, 'Temp: --.-°C', transform=ax_info.transAxes,
                      ha='center', fontsize=13, color='#aaaaaa')

t_frames = ax_info.text(0.5, 0.38, 'Frames: 0', transform=ax_info.transAxes,
                        ha='center', fontsize=10, color='#666688')

t_log = ax_info.text(0.5, 0.28, f'Log: {os.path.basename(LOG)}',
                     transform=ax_info.transAxes,
                     ha='center', fontsize=8, color='#555577')

t_port = ax_info.text(0.5, 0.12, f'{PORT} @ {BAUD}', transform=ax_info.transAxes,
                      ha='center', fontsize=9, color='#444466')

ax_info.set_title('Status', color='#cccccc', fontsize=10)


def animate(_frame):
    global cur_bpm, cur_motion, cur_temp, frame_count

    new_data = False
    while ser.in_waiting:
        try:
            raw = ser.readline().decode('utf-8', errors='ignore')
        except serial.SerialException:
            break
        d = parse(raw)
        if not d:
            continue

        t_elapsed = time.time() - t0
        log_writer.writerow([f'{t_elapsed:.3f}',
                             d['bpm'], d['ax'], d['ay'], d['az'],
                             d['gx'], d['gy'], d['gz'],
                             d['temp'], d['motion'], d['ts_ms']])

        bpm_q.append(d['bpm'])
        ax_q.append(d['ax'])
        ay_q.append(d['ay'])
        az_q.append(d['az'])

        cur_bpm    = d['bpm']
        cur_motion = d['motion']
        cur_temp   = d['temp']
        frame_count += 1
        new_data = True

    if not new_data:
        return l_bpm, l_ax, l_ay, l_az, bpm_txt, t_motion, t_temp, t_frames

    # Update plots
    ydata = list(bpm_q)
    l_bpm.set_ydata(ydata)
    l_ax.set_ydata(list(ax_q))
    l_ay.set_ydata(list(ay_q))
    l_az.set_ydata(list(az_q))

    # HR annotation
    if cur_bpm > 0:
        bpm_txt.set_text(f'{cur_bpm} BPM')
        bpm_txt.set_color('#ff4444')
    else:
        bpm_txt.set_text('No finger')
        bpm_txt.set_color('#666688')

    # Motion class
    label = MOTION_LABELS.get(cur_motion, '???')
    color = MOTION_COLORS.get(cur_motion, 'white')
    t_motion.set_text(label)
    t_motion.set_color(color)
    motion_bg.set_facecolor(color)

    t_temp.set_text(f'Temp: {cur_temp:.1f}°C')
    t_frames.set_text(f'Frames: {frame_count:,}')

    return l_bpm, l_ax, l_ay, l_az, bpm_txt, t_motion, t_temp, t_frames


ani = animation.FuncAnimation(fig, animate, interval=100,
                              blit=False, cache_frame_data=False)


def on_close(_event):
    log_file.flush()
    log_file.close()
    ser.close()
    print(f"\nClosed. Log saved to {LOG}  ({frame_count} frames)")


fig.canvas.mpl_connect('close_event', on_close)

plt.show()
