import json
import subprocess
import sys
import time
from collections import OrderedDict

URL = "http://192.168.2.149/events"
DURATION = 40

print(f"监听 {URL}，持续 {DURATION} 秒收集所有实体 ...\n")

proc = subprocess.Popen(
    ["curl", "-s", "-N", "-m", str(DURATION), URL],
    stdout=subprocess.PIPE,
    text=True,
    errors="replace",
)

entities = OrderedDict()
start = time.time()
try:
    for line in proc.stdout:
        line = line.strip()
        if not line.startswith("data: "):
            continue
        raw = line[6:]
        if not raw.startswith("{"):
            continue
        try:
            d = json.loads(raw)
        except json.JSONDecodeError:
            continue
        eid = d.get("id")
        if not eid:
            continue
        # 只记录 sensor / binary_sensor / switch 等有状态的实体
        if "state" in d or "value" in d:
            entities[eid] = d.get("state", d.get("value"))
        if time.time() - start > DURATION:
            break
finally:
    proc.terminate()

proc.wait(timeout=5)

print(f"共采集到 {len(entities)} 个实体状态：\n")
for k, v in entities.items():
    print(f"  {k:48s} = {v}")

print("\n--- 关键传感器检查 ---")
checks = {
    "CO2 (CM1106)": "sensor-daikin_co2_sensor",
    "PM2.5 (PM2105)": "sensor-daikin_pm2_5_sensor",
    "TVOC (SGP30)": "sensor-daikin_tvoc_sensor",
    "CH2O (ZE08)": "sensor-daikin_ch2o_sensor",
    "温度 (HDC1080)": "sensor-daikin_temperature_sensor",
    "湿度 (HDC1080)": "sensor-daikin_humidity_sensor",
    "WiFi 信号": "sensor-daikin_signal",
}
for label, eid in checks.items():
    mark = "OK  " if eid in entities else "缺失"
    val = entities.get(eid, "-")
    print(f"  [{mark}] {label:18s} {val}")
