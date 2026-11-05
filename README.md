# DAIKIN BRY88AB151K · 甲醛通道换装 ZE08C

大金（DAIKIN）BRY88AB151K 空气传感器的 ESPHome 固件，以及把原 **ZE08-CH2O** 甲醛模块换成 **ZE08C** 后的驱动实现、协议分析和踩坑记录。

> 本仓库的重点是一份**能跑通 ZE08C 的自定义驱动**（`esphome/ze08c.h`）与一份**可复现的编译环境说明**（`docs/03-build-env.md`）。上游项目 <https://github.com/louliangsheng/daikin-air-sensor> 只支持 ZE08-CH2O，两者协议完全不同。

---

## 1. 硬件构成

| 部件 | 型号 | 接口 | 引脚 |
|---|---|---|---|
| 主控 | ESP32-D0WDQ6 rev1.0，4 MB Flash | — | — |
| PM2.5 | PM2105 | UART | I²C 总线 `sda=23 scl=22`（该驱动走 I²C 封装） |
| CO₂ | CM1106 | UART | `rx=16 tx=17`，9600 8N1 |
| 甲醛 | **ZE08C**（原 ZE08-CH2O 位置） | UART | `rx=5`（仅需 RX），9600 8N1 |
| TVOC | SGP30 | I²C | `sda=19 scl=21` |
| 温湿度 | HDC1080 | I²C | `sda=19 scl=21` |

设备 MAC 示例：`c8:c9:a3:e3:32:6c`，主机名 `daikin-air-sensor-e3326c`。

## 2. 实测效果

OTA 到 `ze08c-v4` 后，五个通道全部出数：

```
[D][ze08c]: ZE08C Received CH2O=7 µg/m³ (0.007 mg/m³) 帧 2C 00 07 07 D0 0A
[D][cm1106]: CM1106 Received CO₂=553ppm DF3=01 DF4=69
[D][custom]: PM2.5: 32
[I][ze08c]: 累计 360 字节 / 有效帧 60 / 异常帧 0 / 残留 0｜GPIO5 电平=1
```

| 指标 | 实测值 |
|---|---|
| 甲醛读数 | 5–11 µg/m³（实时波动，非固定值） |
| 帧速率 | 1 帧/秒，30 秒 30 帧 |
| 有效帧 / 异常帧 | 150 / **0** |
| 日志密度 | 与 CM1106 一致（10 秒一行） |
| 资源占用 | Flash 52.7%，RAM 14.4% |

甲醛参考限值：GB/T 18883-2022 室内空气质量标准 **0.08 mg/m³ = 80 µg/m³**，当前读数属优良水平。

## 3. 快速开始

### 3.1 备份原厂固件（务必先做）

ESP32 有 4 MB Flash，整片备份后再动手：

```bash
esptool.py --port /dev/cu.usbserial-11440 --baud 115200 read_flash 0 0x400000 daikin-original-stock-4MB.bin
shasum -a 256 daikin-original-stock-4MB.bin
# 本项目所用设备：298dd196ec3400a7dfd81b35c765e0e5752860e6726e361b2dcefa128db32549
python3 tools/verify_backup.py   # 校验镜像头、分区表、SHA256
```

> 原厂 4 MB 备份**未随仓库提供**（体积大且涉及厂商固件版权），已写入 `.gitignore`。本机留存于 `firmware/daikin-original-stock-4MB.bin`（4,194,304 字节），校验值 `298dd196…2549`。`git clone` 不会包含该文件，请按上面的命令自行备份。

### 3.2 编译

编译环境有硬性约束，**必须用 ESPHome 2024.12.4**（新版已删除 `custom_component`）。完整步骤与六个坑见 [`docs/03-build-env.md`](docs/03-build-env.md)。

```bash
cd esphome          # 必须在 esphome/ 目录下执行（配置用 ${sysenv.PWD} 定位离线库）
esphome compile daikin-ch2o-fixed.yaml
```

> 已验证：本配置在 ESPHome 2024.12.4 上从零编译通过，占用 Flash 52.7%（967 KB / 1.75 MB）、RAM 14.4%（47 KB）。第三方库随仓库分发（`esphome/libs/`），**无需访问 PlatformIO 注册表**。

### 3.3 刷入

```bash
# 首次（USB，偏移 0x0 写 factory 镜像）
esptool.py --port /dev/cu.usbserial-11440 --baud 115200 write_flash 0x0 \
  .esphome/build/daikin-air-sensor/.pioenvs/daikin-air-sensor/firmware.factory.bin

# 后续（OTA）
esphome upload daikin-ch2o-fixed.yaml --device <设备IP>
```

首次刷入后通过 `DAIKIN Fallback Hotspot` 或 `captive_portal` 配网。

### 3.4 接线

ZE08C 的引脚定义与常见模块**顺序相反**，接错会直接烧模块（无防反接保护）。详见 [`docs/01-hardware-wiring.md`](docs/01-hardware-wiring.md)。

| ZE08C 端口 | 定义 | 接到 |
|---|---|---|
| 1 | 电源 **负（−）** | GND（与 ESP32 共地） |
| 2 | 电源 **正（+）** | 5 V（USB 母头） |
| 3 | A / RX | 可不接（主动上传模式） |
| 4 | B / TX | 经 1k+2k 分压 → GPIO5 |

## 4. 三个核心发现

1. **ZE08C ≠ ZE08-CH2O**。前者主动上传 6 字节帧（帧头 `0x2C`），后者需查询、应答 9 字节帧（帧头 `0xFF`）。上游驱动对 ZE08C 完全无效，日志表现为「30 秒只收到 1 个字节」。
2. **规格书的满量程字段与实际模块不符**。规格书写 `B4B5 = 03 E8`（1000 µg/m³），实测模块自报 `07 D0`（2000 µg/m³）。把该字段当固定值做硬校验，会让每一帧正常数据都被判为非法，并触发滑动重同步形成连锁错位——这是「有效帧 0 / 异常帧 361」的真正原因。
3. **`lib_ldf_mode=off` 下框架库头文件不会传给 ESPAsyncWebServer**。依赖图显示 `FS @ 2.0.0` 已解析，但编译时报 `FS.h: No such file or directory`，需要在 `build_flags` 里显式补 `-I` 路径。

## 5. 目录结构

```
.
├── esphome/
│   ├── daikin-ch2o-fixed.yaml   # 主配置（ZE08C 版）
│   ├── ze08c.h                  # ★ ZE08C 驱动（本项目核心）
│   ├── cm1106.h                 # CO2
│   ├── pm2105.h / pm2005.h      # PM2.5
│   ├── ze08-ch2o.h              # 上游 ZE08-CH2O 驱动（仅作对照留存）
│   └── libs/                    # 离线依赖兜底（网络受限时使用）
├── tools/
│   ├── capture_ze08c.py         # ★ 不经 ESP32 直接抓帧并判定协议
│   ├── verify_backup.py         # 原厂备份完整性校验
│   ├── check_sensors.py         # 监听 /events 汇总所有实体
│   └── capture_boot.py          # 抓 ESP32 上电日志
├── firmware/
│   ├── daikin-bry88ab151k-ze08c-v4.bin   # 已验证的 OTA 固件
│   ├── daikin-original-stock-4MB.bin     # 原厂备份：本地留存，.gitignore 排除，不随仓库分发
│   └── 接线参考图.jpg
└── docs/
    ├── 01-hardware-wiring.md
    ├── 02-ze08c-protocol.md
    ├── 03-build-env.md
    ├── 04-troubleshooting.md
    └── datasheets/
        └── ZE08C-formaldehyde-module-datasheet-CN.pdf   # 厂商规格书原件（4 页）
```

## 6. 已知限制

- 配置固定在 **ESPHome 2024.12.4**。若移植到 2025.x+，需把 `custom_component` 改为原生 `Component` 实现 + `sensor: platform: template` 的等价写法，或直接写成外部组件（`external_components`）。
- 驱动只做了「协议层正确」的验证（帧结构、校验和、帧速率、数值动态变化）。**未与标准甲醛检测仪做浓度比对**，绝对精度依赖模块出厂标定。
- 模块上电后有 3–5 分钟预热漂移（日志里读数会从 5 爬到 34 再回落），属正常现象。
- PM2105 驱动沿用了 I²C 通道命名，实际器件为 PM2105，与配置中的 `pm25_bus` 对应。

## 7. 致谢

- 上游项目：<https://github.com/louliangsheng/daikin-air-sensor>（BRY88AB151K 的原始 yaml 与 PM2105/CM1106 驱动）
- 甲醛模块规格书：炜盛（Winsen）《黑-ZE08C 甲醛检测模块技术规格书》，原件留存于 [`docs/datasheets/ZE08C-formaldehyde-module-datasheet-CN.pdf`](docs/datasheets/ZE08C-formaldehyde-module-datasheet-CN.pdf)（SHA256 `9d61defb…8122`）
- 依赖库：[esphome/AsyncTCP](https://github.com/esphome/AsyncTCP)、[esphome/ESPAsyncWebServer](https://github.com/esphome/ESPAsyncWebServer)（LGPL-3.0）

本项目在原厂设备上实测通过，涉及拆机与刷写，**风险自负**：请务必备份原厂固件。
