import hashlib
import sys

path = "daikin-original-stock-4MB.bin"
d = open(path, "rb").read()
BLANK = b"\xff" * 0x10000

print(f"文件大小      : {len(d)} 字节 (期望 4194304)")
print(f"0x1000 bootldr: 0x{d[0x1000]:02X}   (0xE9 = 有效 ESP32 镜像头)")
print(f"0x8000 分区表 : 0x{d[0x8000]:02X}{d[0x8001]:02X}   (0xAA50 = 有效分区表)")
print(f"0x10000 app   : 0x{d[0x10000]:02X}   (0xE9 = 有效应用镜像)")
print(f"SHA256        : {hashlib.sha256(d).hexdigest()}")

nz = sum(1 for i in range(0, len(d), 0x10000) if d[i:i + 0x10000] != BLANK)
print(f"有数据的 64KB 块: {nz} / 64")

# 一致性检查：magic 全部正确才算可用备份
ok = (
    len(d) == 4194304
    and d[0x1000] == 0xE9
    and d[0x8000] == 0xAA
    and d[0x8001] == 0x50
    and d[0x10000] == 0xE9
)
print("判定: " + ("备份完整可用" if ok else "备份异常，需重读"))
sys.exit(0 if ok else 1)
