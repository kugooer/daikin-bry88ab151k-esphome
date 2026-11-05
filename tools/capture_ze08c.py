#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ZE08C 甲醛模块独立验证脚本（不经过 ESP32）

用途：直接把 USB-TTL 接到 ZE08C 的 TX 上，抓原始字节，判断
      1) 模块是否还在工作（有无数据）
      2) 数据是否为 ZE08C 的 6 字节主动上传帧

接线（只接两根）：
    USB-TTL GND  <->  ZE08C 端口1 (-)
    USB-TTL RX   <->  ZE08C 端口4 (TX)
    ZE08C 端口2 (+) 由 USB 母头 5V 单独供电
    ⚠ 模块与 USB-TTL 必须共地；不要给 USB-TTL 接 VCC

用法：
    ~/.local/share/uv/tools/esptool/bin/python capture_ze08c.py [秒数] [端口]
"""

import sys
import time
import glob

try:
    import serial
except ImportError:
    sys.exit("缺少 pyserial，请用 esptool 环境的 python 运行本脚本")

FRAME_LEN = 6
HEADER = 0x2C


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbserial*"))
    return ports[0] if ports else None


def parse_frames(data):
    """在字节流中滑动查找合法 ZE08C 帧

    只用「帧头 + 校验和」判定合法性。B4B5 是模块自报量程，
    实测为 0x07D0(2000 µg/m³)，而规格书写 0x03E8(1000)，
    不同批次取值不同，因此不能作为校验条件。
    """
    frames, bad = [], 0
    i = 0
    while i <= len(data) - FRAME_LEN:
        if data[i] != HEADER:
            i += 1
            continue
        chunk = data[i:i + FRAME_LEN]
        if chunk[5] == (sum(chunk[:5]) & 0xFF):
            value = chunk[1] * 256 + chunk[2]
            rng = chunk[3] * 256 + chunk[4]
            frames.append((bytes(chunk), value, rng))
            i += FRAME_LEN
        else:
            bad += 1
            i += 1
    return frames, bad


def main():
    duration = int(sys.argv[1]) if len(sys.argv) > 1 else 25
    port = sys.argv[2] if len(sys.argv) > 2 else find_port()
    if not port:
        sys.exit("未找到 /dev/cu.usbserial*，请确认 USB-TTL 已插好")

    print(f"端口: {port}   波特率: 9600 8N1   采集: {duration}s")
    print("接线应为：TTL GND→端口1(-)，TTL RX→端口4(TX)，模块5V由外部供给，且必须共地")
    print("-" * 68)

    try:
        ser = serial.Serial(port, 9600, timeout=0.2)
    except Exception as e:
        sys.exit(f"打开串口失败: {type(e).__name__}: {e}")

    ser.dtr = False
    ser.rts = False
    time.sleep(0.2)
    ser.reset_input_buffer()

    buf = bytearray()
    t0 = time.time()
    while time.time() - t0 < duration:
        chunk = ser.read(256)
        if chunk:
            buf += chunk
    ser.close()

    elapsed = time.time() - t0
    print(f"收到 {len(buf)} 字节，耗时 {elapsed:.1f}s")
    if not buf:
        print()
        print("=== 结论：模块完全没有输出 ===")
        print("按可能性从高到低排查：")
        print("  1. 电源极性接反  —— 本模块端口1是负极、端口2是正极，且无防反接保护")
        print("  2. 模块未上电    —— 用万用表量端口1与端口2之间应为 5.0V")
        print("  3. TX 接错引脚   —— 模块 TX 是端口4(B)，不是端口3(A/RX)")
        print("  4. 未共地        —— USB-TTL 的 GND 必须与模块端口1 相连")
        print("  5. 模块为 I²C 输出模式，或已被 5V 反接损坏")
        return

    head = bytes(buf[:120])
    print()
    print("=== 原始字节（前 120 字节）===")
    for off in range(0, len(head), 16):
        piece = head[off:off + 16]
        hexs = " ".join(f"{b:02X}" for b in piece)
        print(f"  {off:04X}  {hexs}")

    frames, bad = parse_frames(bytes(buf))
    print()
    print("=== 帧解析 ===")
    print(f"  合法 ZE08C 帧: {len(frames)}   校验失败候选: {bad}")
    if frames:
        for raw, value, rng in frames[:8]:
            print(f"    {' '.join(f'{b:02X}' for b in raw)}  ->  {value} µg/m³ ({value/1000:.3f} mg/m³)  量程 {rng}")
        if len(frames) > 8:
            print(f"    ... 共 {len(frames)} 帧")
        period = elapsed / len(frames)
        latest = frames[-1][1]
        print()
        print("=== 结论：模块正常，且确认为 ZE08C 协议 ===")
        print(f"  最新读数: {latest} µg/m³ ({latest/1000:.3f} mg/m³)")
        print(f"  上传周期: 约 {period:.2f} 秒/帧")
        return

    print()
    print("=== 有数据但无法解析为 ZE08C 帧 ===")
    print("说明模块在输出，但波特率/协议不匹配。可能原因：")
    print("  1. 模块实际是 ZE08-CH2O（9 字节帧、帧头 0xFF），并非 ZE08C")
    print("  2. 波特率不是 9600（可试 4800 / 19200）")
    print("  3. 输出为 I²C 模式，串口上只是噪声")


if __name__ == "__main__":
    main()
