#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = [
#   "pyserial",
# ]
# ///
"""
Sesami RTC 時刻合わせスクリプト

Usage:
  uv run set_rtc_time.py <PORT>
  uv run set_rtc_time.py COM3
  uv run set_rtc_time.py /dev/ttyUSB0

オプションで unlock/lock 時刻も設定できる:
  uv run set_rtc_time.py COM3 --unlock 08:00 --lock 20:00
"""

import argparse
import datetime
import sys
import time

import serial


def wait_for_prompt(ser: serial.Serial, timeout: float = 8.0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            print(f"  < {line}")
        if ">" in line or "Shell" in line or "help" in line.lower():
            return True
    return False


def send_command(ser: serial.Serial, cmd: str, wait: float = 0.5) -> list[str]:
    print(f"  > {cmd}")
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode(errors="ignore").strip()
        if line:
            print(f"  < {line}")
            lines.append(line)
    return lines


def main() -> None:
    parser = argparse.ArgumentParser(description="Sesami RTC 時刻合わせ")
    parser.add_argument("port", help="シリアルポート (例: COM3, /dev/ttyUSB0)")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--unlock", metavar="HH:MM", help="unlock時刻 (例: 08:00)")
    parser.add_argument("--lock",   metavar="HH:MM", help="lock時刻   (例: 20:00)")
    args = parser.parse_args()

    now = datetime.datetime.now()
    time_str = now.strftime("%Y-%m-%d %H:%M:%S")

    print(f"Connecting to {args.port} ({args.baud} baud)...")
    with serial.Serial(args.port, args.baud, timeout=2) as ser:
        time.sleep(1)
        ser.reset_input_buffer()

        print("Waiting for shell prompt...")
        if not wait_for_prompt(ser):
            print("Warning: shell prompt not detected, sending commands anyway.")

        send_command(ser, f"time set {time_str}")

        if args.unlock:
            send_command(ser, f"unlock {args.unlock}")
        if args.lock:
            send_command(ser, f"lock {args.lock}")

        send_command(ser, "schedule")
        send_command(ser, "run")

    print(f"\nDone. RTC set to {time_str}")


if __name__ == "__main__":
    main()
