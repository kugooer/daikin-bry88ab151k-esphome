# 编译环境搭建

这份文档记录的是**实际踩过的坑**，不是官方流程的复述。按顺序执行可以避免重复摸索。

## 0. 版本约束（先看这里）

| 组件 | 版本 | 为什么必须是这个 |
|---|---|---|
| **ESPHome** | **2024.12.4** | 更新的版本已移除 `custom_component`，本配置会直接报 `[custom_component] is an invalid option` |
| PlatformIO | 6.x（ESPHome 自带） | — |
| platform-espressif32 | 5.4.0 | 由 ESPHome 2024.12.4 固定 |
| Arduino-ESP32 | 2.0.6 | 同上 |

> 上游 README 用的是 `sensor: platform: custom`（更老的写法）。本配置已改为 `custom_component` + `template sensor` 的组合，这是 **2024.12.4 能用的最后一种写法**。再往上升级需要重写为原生组件。

```bash
python3 -m venv .venv
.venv/bin/pip install "esphome==2024.12.4"
.venv/bin/esphome version   # 应输出 2024.12.4
```

## 1. 六个坑

### 坑 1：新版 ESPHome 删掉了 custom_component

**症状**

```
Failed config
[custom_component] is an invalid option for [esphome].
```

或更早的写法：

```
[sensor] platform: custom  →  Platform not found
```

**处理**：降级到 2024.12.4。若必须用新版，把三个驱动改成外部组件（`external_components`），并在 `components/` 里实现 `Component` 类，通过 `App.register_sensor()` 暴露。

### 坑 2：`FS.h: No such file or directory`

**症状**（编译到 Library 阶段）

```
.piolibdeps/.../ESPAsyncWebServer-esphome/src/ESPAsyncWebServer.h:27:10:
fatal error: FS.h: No such file or directory
```

**根因**：ESPHome 默认 `lib_ldf_mode = off`。此时 PlatformIO 的 `piolib.py → LibBuilderBase.build()` **只把「已构建完成（is_built）」的库的 include 路径复制给当前库**。框架内置库（`FS` / `WiFi` / `Update` / `DNSServer` / `ESPmDNS` / `Wire`）的构建顺序排在 `ESPAsyncWebServer` 之后，所以它们的头文件路径还没进 `CPPPATH`。

注意这是**易误判点**：依赖图里明明能看到 `FS @ 2.0.0` 已解析，但 include 就是不生效。

**处理**：在 `esphome.platformio_options.build_flags` 里显式补路径（本项目已内置）：

```yaml
esphome:
  platformio_options:
    build_flags:
      - "-I${platformio.packages_dir}/framework-arduinoespressif32/libraries/FS/src"
      # WiFi / Update / DNSServer / ESPmDNS / Wire 同理
```

`${platformio.packages_dir}` 由 PlatformIO 插值，跨机器通用（已实测）。**不要**用 `$PROJECT_PACKAGES_DIR`，它不会被展开。

ESPHome 的 `add_platformio_option` 对列表做拼接而非覆盖，所以不会和自动生成的 `-D` 宏冲突。

### 坑 3：`ESPAsyncWebServer.h: No such file or directory`

**症状**（本次第二次踩到，和坑 2 长得不一样）

```
src/esphome/components/captive_portal/captive_portal.cpp:16:10:
fatal error: ESPAsyncWebServer.h: No such file or directory
```

**根因**：ESPHome 2024.12.4 只自动加入**框架内置库**（`WiFi`/`FS`/`Update`/`DNSServer`/`ESPmDNS`/`Wire`），**不会**加 `AsyncTCP` 和 `ESPAsyncWebServer`，而 `captive_portal` 与 `web_server` 都依赖后者。原来的 yaml 靠 `platformio_options.lib_deps` 显式指定，一旦漏掉就报错。

**处理**：在 `esphome.platformio_options.lib_deps` 里显式声明。三种写法，按网络环境选：

```yaml
# ① 默认（本项目采用，已实测编译通过）：仓库内置的离线副本，不依赖网络
#    ⚠ 必须在 esphome/ 目录下执行 esphome compile —— ${sysenv.PWD} 取的是当前工作目录
lib_deps:
  - "file://${sysenv.PWD}/libs/AsyncTCP-esphome"
  - "file://${sysenv.PWD}/libs/ESPAsyncWebServer-esphome"

# ② 走 PlatformIO 注册表（官方途径，网络正常时最省事）
lib_deps:
  - esphome/AsyncTCP-esphome@2.1.4
  - esphome/ESPAsyncWebServer-esphome@3.2.2

# ③ 走 GitHub tag（tag 必须带 v 前缀）
lib_deps:
  - https://github.com/esphome/AsyncTCP.git#v2.1.4
  - https://github.com/esphome/ESPAsyncWebServer.git#v3.2.2
```

踩过的细节，按踩的顺序：

1. **`file://./libs/...` 这种相对路径不行**。看着像相对配置文件，实际 PlatformIO 相对**构建目录**（`.esphome/build/<name>/`）解析，报 `FileNotFoundError: ./libs/AsyncTCP-esphome`。改用 `${sysenv.PWD}` 后正常——这是把库随仓库分发且不用手改路径的关键。
2. ② 在部分网络下报 `PackageException: Could not install package ... SSL: UNEXPECTED_EOF_WHILE_READING`。
3. ③ 能绕开注册表本体，但装完后 PlatformIO 仍要解析 `ESPAsyncWebServer` 自身 `library.json` 里声明的 `esphome/AsyncTCP-esphome` 依赖，**这一步同样回注册表**，网络不通时依旧失败。tag 名漏写 `v` 会报 `fatal: Remote branch 2.1.4 not found in upstream origin`。
4. 用离线副本时，需要**从库的 `library.json` 里删掉 `dependencies` 声明**。原声明指向 `FS`/`WiFi`/`Update`（框架内置库），留着会让 PlatformIO 每次都去注册表解析一遍，离线环境直接失败。仓库里的副本已处理，头文件由 `build_flags` 的 `-I` 兜底。
5. 依赖顺序有意义：`AsyncTCP` 必须排在 `ESPAsyncWebServer` 之前，前者先构建完，后者的 `#include "AsyncTCP.h"` 才能被解析。

### 坑 4：esptool 只能装到源码包，编译失败

**症状**：`pip install esptool==4.7.0` 或 `platformio` 装包时报编译错误，或 `OSError: EEXIST`。

**处理**：用 `uv` 加 override 指向有 wheel 的版本：

```bash
uv pip install --override overrides.txt ...
# overrides.txt 内容：
esptool==4.12.0
```

（Python 3.13 环境下 4.7.0 没有 wheel，只能源码构建。）

### 坑 5：framework 版本声明不匹配

**症状**

```
UnknownPackageError: Could not find the package with 'platformio/espressif32 @ ~3.20005.0' requirements
```

**根因**：`~/.platformio/packages/framework-arduinoespressif32/.piopm` 里声明的版本号与实际内容不一致（内容为 2.0.6，`.piopm` 却要求 `~3.20005.0`）。

**处理**：改 `.piopm` 的 `version` 字段让它满足约束（内容仍是 2.0.6，API 兼容）。属于治标方案，但比重新下载整套 toolchain 快得多。

### 坑 6：工具包缺入口脚本

**症状**：`tool-scons`、`tool-esptoolpy` 装好后仍报找不到 `scons.py` / `esptool.py`。

**处理**：手工补入口脚本，指向包内真正的模块入口。这类错误通常出现在网络中断导致的「半安装」状态，重新安装整包亦可。

## 2. 编译与产物

```bash
cd esphome
../.venv/bin/esphome compile daikin-ch2o-fixed.yaml
```

产物位置：

```
.esphome/build/daikin-air-sensor/.pioenvs/daikin-air-sensor/
├── firmware.bin           # OTA 用（应用镜像）
└── firmware.factory.bin   # USB 首次刷入用（含 bootloader + 分区表，写 0x0）
```

参考占用：**Flash 52.7%（967 KB / 1.75 MB）、RAM 14.4%（47 KB）**。

## 3. 刷写

```bash
# 首次：USB，写 0x0
esptool.py --port /dev/cu.usbserial-11440 --baud 115200 write_flash 0x0 \
  .esphome/build/daikin-air-sensor/.pioenvs/daikin-air-sensor/firmware.factory.bin

# 后续：OTA
esphome upload daikin-ch2o-fixed.yaml --device 192.168.x.x
```

进入下载模式的判据：板载 LED 熄灭（正常运行时为常亮）。若 `No serial data received`，手动把 GPIO0 短接 GND 后碰一下 RST。

波特率建议 **115200**。460800 在部分 CH340 上会报 `Invalid head of packet`。

## 4. 校验固件里是否真的编入了驱动

OTA 前可以先用字符串确认，避免白刷一轮：

```bash
strings firmware.bin | grep -E "ZE08C Received|模块标称满量程|校验失败"
```

命中即说明驱动参与编译（`ESP_LOGD` 的字符串在 `logger` 级别过滤后仍保留在镜像里）。
