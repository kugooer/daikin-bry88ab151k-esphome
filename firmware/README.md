# 固件说明

| 文件 | 大小 | 说明 |
|---|---|---|
| `daikin-bry88ab151k-ze08c-v4.bin` | 973,536 字节 | **推荐使用**。ZE08C 驱动 + 日志降噪版，已在本机 OTA 验证 |

SHA256：

```
2e35ad74fb34d2d73f76cf03e222aabc60144adc2eb180110beec61b7eacbf03  daikin-bry88ab151k-ze08c-v4.bin
```

## 刷入方式

OTA（设备已在网）：

```bash
esphome upload esphome/daikin-ch2o-fixed.yaml --device <设备IP>
```

USB 首次刷写（需 `firmware.factory.bin`，请自行编译生成，仓库不提供）：

```bash
esptool.py --port /dev/cu.usbserial-11440 --baud 115200 write_flash 0x0 firmware.factory.bin
```

> 仓库里的 `.bin` 是 **OTA 镜像**（应用分区），**不能**直接写到 `0x0`。首次刷写请用编译出的 `firmware.factory.bin`。

## 版本历史（仅保留最终版）

| 版本 | 变更 | 结果 |
|---|---|---|
| v2 | 首个编译成功的 ZE08C 驱动 | CH2O 仍 NA（当时接线错误） |
| v3 | 去掉满量程硬校验；加 hex 打印与引脚电平诊断 | **CH2O 出数**，150 帧全过 |
| **v4** | 读数日志 10 秒限速、统计改 60 秒、格式对齐 CM1106 | 已验证，日常运行版 |

中间的 v2/v3 未随仓库提供，需要时从 commit 历史或自行编译获取。

## 原厂固件

厂商原厂 4 MB 完整备份**未随仓库提供**（涉及厂商固件版权，且体积 4.2 MB）。请自行备份：

```bash
esptool.py --port /dev/cu.usbserial-11440 --baud 115200 read_flash 0 0x400000 daikin-original-stock-4MB.bin
shasum -a 256 daikin-original-stock-4MB.bin
python3 ../tools/verify_backup.py
```

本项目所用设备的原厂备份校验值：

```
298dd196ec3400a7dfd81b35c765e0e5752860e6726e361b2dcefa128db32549
```

仅供你核对是否为同一批次，请勿据此认为可互换。
