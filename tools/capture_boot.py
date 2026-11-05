import serial
import time

PORT = "/dev/cu.usbserial-11440"
DURATION = 10

s = serial.Serial(PORT, 115200, timeout=0.3)
s.dtr = False
s.rts = False
time.sleep(0.2)
s.reset_input_buffer()

print(f"监听 {PORT} @115200，持续 {DURATION} 秒 ...\n")

buf = bytearray()
end = time.time() + DURATION
while time.time() < end:
    chunk = s.read(512)
    if chunk:
        buf += chunk

s.close()

text = buf.decode("utf-8", errors="replace")
print(text if text.strip() else "(未收到任何数据)")
print(f"\n--- 共收到 {len(buf)} 字节 ---")
for kw in ("DAIKIN", "Fallback", "ESPHome", "WiFi", "wifi", "AP", "boot"):
    if kw in text:
        print(f"命中关键字: {kw}")
