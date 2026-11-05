/*********************************************************************
  ZE08C 甲醛（HCHO）检测模块 —— ESPHome 驱动
  依据厂商规格书《黑-ZE08C甲醛检测模块技术规格书》

  【与 ZE08-CH2O 的关键差异】本驱动专用于 ZE08C，不能混用：
    1. 主动上传模式，主控不需要发送任何查询命令
    2. 帧长 6 字节（ZE08-CH2O 为 9 字节，帧头 0xFF）
       B1 = 0x2C  模块固定地址
       B2 = 甲醛浓度高字节
       B3 = 甲醛浓度低字节
       B4 = 量程高字节     实测 0x07，与 B5 合为 0x07D0 = 2000 µg/m³
       B5 = 量程低字节     规格书写 0x03E8(1000)，与实际模块不符，故不校验
       B6 = 校验和
       实测样例：2C 00 0E 07 D0 11 → 14 µg/m³，量程 2000 µg/m³
    3. 校验和 B6 = unit_8(B1+B2+B3+B4+B5)
    4. 浓度(mg/m³) = (B2*256+B3)/1000
       换算成 µg/m³ 后数值恰好等于 (B2*256+B3)，故直接以该整数上报

  【接线】
    模块端口 1 = 电源负极 (-)     ← 注意本模块是「先负后正」，与常见模块相反
    模块端口 2 = 电源正极 (+)     5V DC，无电压反接保护
    模块端口 3 = A (RX)          接 ESP32 TX（主动上传模式下可不接）
    模块端口 4 = B (TX)          接 ESP32 RX，但为 5V 电平，必须分压(1k/2k)后再接

  【诊断输出】
    - 每帧有效数据打一行 D 级日志（帧字节 + 换算结果）
    - 原始字节 hex 分段打印需手动开启 ZE08C_RAW_LOG
    - 每 60 秒打印累计统计 + RX 引脚实际电平（正常 D 级；无数据/有异常帧时 W 级）
      RX 电平判读：恒为 0 → 线被拉低或短路到 GND；恒为 1 → 线路空闲（模块未发数据）
********************************************************************/

#include "esphome.h"

#define ZE08C_DEBUG
// 原始字节逐段 hex 打印：排障时取消注释；正常运行时保持注释，避免 1 帧/秒刷爆日志。
// #define ZE08C_RAW_LOG

static const char *TAG_ZE08C = "ze08c";

static const uint8_t ZE08C_FRAME_LEN = 6;
static const uint8_t ZE08C_HEADER = 0x2C;
// 注：B4/B5 是模块自报的满量程，实测为 0x07D0 = 2000 µg/m³，
// 与规格书的 0x03E8 不符，故不参与校验，仅作信息输出。
static const uint32_t ZE08C_PUBLISH_INTERVAL_MS = 10000;  // 数值无变化时的心跳上报间隔
static const uint32_t ZE08C_LOG_INTERVAL_MS = 10000;      // 读数日志最快打印间隔（对齐 CM1106 的 10 秒节奏）
static const uint32_t ZE08C_STATS_INTERVAL_MS = 60000;    // 累计统计周期
static const uint32_t ZE08C_RAW_FLUSH_IDLE_MS = 300;      // 原始字节空闲多久后成行打印
static const uint8_t ZE08C_RAW_CHUNK = 24;                // 单行最多打印的字节数

class WinsenZE08CSensor : public Component, public UARTDevice {
 public:
  WinsenZE08CSensor(UARTComponent *parent, Sensor *out, int rx_pin = -1)
      : UARTDevice(parent), out_(out), rx_pin_(rx_pin) {}

  // UART 是 BUS 优先级，DATA 在其后初始化
  float get_setup_priority() const override { return setup_priority::DATA; }

  void setup() override {
    this->len_ = 0;
    this->rx_bytes_ = 0;
    this->good_frames_ = 0;
    this->bad_frames_ = 0;
    this->raw_len_ = 0;
    this->last_report_ = millis();
    this->last_pub_ = millis();
    this->last_value_ = 0xFFFF;  // 哨兵值，保证首帧一定上报
    this->first_byte_ms_ = 0;
    this->last_log_ = 0;
    this->last_bad_ = 0;
    this->range_reported_ = false;
    if (this->rx_pin_ >= 0) {
      ESP_LOGI(TAG_ZE08C, "诊断已启用：RX=GPIO%d，每 60 秒打印一次状态", this->rx_pin_);
    }
  }

  // 主动上传模式：持续读取，滑动窗口组帧
  void loop() override {
    while (this->available() > 0) {
      uint8_t b = this->read();
      uint32_t now = millis();

      if (this->rx_bytes_ == 0) {
        this->first_byte_ms_ = now;
        ESP_LOGI(TAG_ZE08C, "收到首个字节 %02X（上电后 %u ms）", b, (unsigned) now);
      }
      this->rx_bytes_++;

      // 原始报文抓取：不参与组帧，只用于诊断
      this->raw_ts_ = now;
      this->raw_push_(b);

      // 帧同步：缓冲区为空时必须收到帧头 0x2C，否则丢弃
      if (this->len_ == 0 && b != ZE08C_HEADER) {
        continue;
      }
      if (this->len_ >= ZE08C_FRAME_LEN) {
        this->drop_(1);
      }
      this->buf_[this->len_++] = b;
      if (this->len_ == ZE08C_FRAME_LEN) {
        this->parse_();
      }
    }

    // 空闲超时即把已抓到的字节成行输出，便于与串口助手抓包对照
    this->raw_flush_(false);

    // 每 60 秒自检一次；仅在异常时才用 W 级，正常状态用 D 级
    if (millis() - this->last_report_ > ZE08C_STATS_INTERVAL_MS) {
      this->last_report_ = millis();
      int level = this->rx_pin_ >= 0 ? digitalRead(this->rx_pin_) : -1;

      if (this->rx_bytes_ == 0) {
        ESP_LOGW(TAG_ZE08C,
                 "60秒内未收到任何字节｜GPIO%d 电平=%d｜检查：模块5V供电、端口1接GND、端口4(TX)接GPIO%d、是否共地",
                 this->rx_pin_, level, this->rx_pin_);
      } else if (this->bad_frames_ > this->last_bad_) {
        ESP_LOGW(TAG_ZE08C, "累计 %u 字节 / 有效帧 %u / 异常帧 %u(本周期新增 %u) / 残留 %u｜GPIO%d 电平=%d",
                 (unsigned) this->rx_bytes_, (unsigned) this->good_frames_, (unsigned) this->bad_frames_,
                 (unsigned) (this->bad_frames_ - this->last_bad_), (unsigned) this->len_, this->rx_pin_, level);
      } else {
        ESP_LOGD(TAG_ZE08C, "累计 %u 字节 / 有效帧 %u / 异常帧 %u / 残留 %u｜GPIO%d 电平=%d",
                 (unsigned) this->rx_bytes_, (unsigned) this->good_frames_, (unsigned) this->bad_frames_,
                 (unsigned) this->len_, this->rx_pin_, level);
      }
      this->last_bad_ = this->bad_frames_;
    }
  }

 private:
  Sensor *out_{nullptr};
  int rx_pin_{-1};
  uint8_t buf_[ZE08C_FRAME_LEN]{0};
  uint8_t len_{0};
  uint32_t rx_bytes_{0};
  uint32_t good_frames_{0};
  uint32_t bad_frames_{0};
  uint32_t last_report_{0};
  uint32_t last_pub_{0};
  uint16_t last_value_{0xFFFF};
  uint32_t first_byte_ms_{0};
  uint32_t last_log_{0};
  uint32_t last_bad_{0};
  bool range_reported_{false};

  // 原始报文行缓冲
  uint8_t raw_[ZE08C_RAW_CHUNK]{0};
  uint8_t raw_len_{0};
  uint32_t raw_ts_{0};

  void raw_push_(uint8_t b) {
    if (this->raw_len_ >= ZE08C_RAW_CHUNK) {
      this->raw_flush_(true);
    }
    this->raw_[this->raw_len_++] = b;
  }

  void raw_flush_(bool force) {
    if (this->raw_len_ == 0) {
      return;
    }
    if (!force && millis() - this->raw_ts_ < ZE08C_RAW_FLUSH_IDLE_MS) {
      return;
    }
#ifdef ZE08C_RAW_LOG
    char line[ZE08C_RAW_CHUNK * 3 + 1];
    size_t pos = 0;
    for (uint8_t i = 0; i < this->raw_len_; i++) {
      pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", this->raw_[i]);
    }
    ESP_LOGD(TAG_ZE08C, "RX %u 字节: %s", (unsigned) this->raw_len_, line);
#endif
    this->raw_len_ = 0;
  }

  void drop_(uint8_t n) {
    if (n >= this->len_) {
      this->len_ = 0;
      return;
    }
    for (uint8_t i = n; i < this->len_; i++) {
      this->buf_[i - n] = this->buf_[i];
    }
    this->len_ -= n;
  }

  void parse_() {
    // 帧头复核：滑动重同步后 buf_[0] 未必仍是 0x2C，必须显式检查，
    // 否则数据字节可能被误当成帧头并凑出"合法"帧。
    if (this->buf_[0] != ZE08C_HEADER) {
      this->bad_frames_++;
      this->drop_(1);
      return;
    }

    // 校验和 = unit_8(B1+B2+B3+B4+B5)
    uint8_t sum = 0;
    for (uint8_t i = 0; i < 5; i++) {
      sum += this->buf_[i];
    }
    if (this->buf_[5] != sum) {
      this->bad_frames_++;
#ifdef ZE08C_DEBUG
      ESP_LOGD(TAG_ZE08C, "校验失败: %02X %02X %02X %02X %02X %02X (计算=%02X)，滑动重新同步",
               this->buf_[0], this->buf_[1], this->buf_[2], this->buf_[3], this->buf_[4], this->buf_[5], sum);
#endif
      this->drop_(1);
      return;
    }
    // B4B5 是模块自报的满量程，只作信息输出，不参与合法性判定。
    // 实测本模块为 0x07D0 = 2000 µg/m³，与规格书写的 0x03E8（=1000）不一致；
    // 不同批次/量程版本取值不同，硬校验会把正常帧全部判死。
    uint16_t range = (uint16_t) this->buf_[3] * 256 + this->buf_[4];
    if (!this->range_reported_) {
      this->range_reported_ = true;
      ESP_LOGI(TAG_ZE08C, "模块标称满量程 %u µg/m³ (0x%04X)", range, range);
    }

    uint16_t value = (uint16_t) this->buf_[1] * 256 + this->buf_[2];  // µg/m³
    this->good_frames_++;
#ifdef ZE08C_DEBUG
    // 模块约 1 帧/秒，逐帧打印会把日志刷满；限速到最快 10 秒一行，
    // 格式对齐 CM1106 的 "Received CO₂=xxx" 单行风格。
    if (millis() - this->last_log_ >= ZE08C_LOG_INTERVAL_MS) {
      this->last_log_ = millis();
      ESP_LOGD(TAG_ZE08C, "ZE08C Received CH2O=%u µg/m³ (%.3f mg/m³) 帧 %02X %02X %02X %02X %02X %02X", value,
               value / 1000.0, this->buf_[0], this->buf_[1], this->buf_[2], this->buf_[3], this->buf_[4],
               this->buf_[5]);
    }
#endif
    // 模块默认约 1 秒一帧，若逐帧上报会给 API 造成无谓流量：
    // 数值变化立即上报，否则每 10 秒心跳一次。
    bool changed = (value != this->last_value_);
    bool heartbeat = (millis() - this->last_pub_ >= ZE08C_PUBLISH_INTERVAL_MS);
    if (this->out_ != nullptr && (changed || heartbeat)) {
      this->last_value_ = value;
      this->last_pub_ = millis();
      this->out_->publish_state(value);
    }
    this->len_ = 0;
  }
};
