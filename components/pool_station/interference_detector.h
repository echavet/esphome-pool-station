#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "interference_math.h"
#include <cstdint>

namespace esphome {
namespace pool_station {

static constexpr uint8_t INTERF_CH_PRESSURE = 0;
static constexpr uint8_t INTERF_CH_PH = 1;
static constexpr uint8_t INTERF_CH_ORP = 2;
static constexpr uint8_t INTERF_CH_WATER_TEMP = 255;
static constexpr uint8_t INTERF_WINDOW = 32;

struct InterferenceConfig {
  bool enable_coincident_jump{true};
  bool enable_shared_noise{true};
  bool enable_tw_coupling{false};
  uint32_t hold_ms{30000};
  uint32_t jump_window_ms{90000};
  uint32_t tw_window_ms{180000};
  uint32_t log_rate_limit_ms{60000};
  float orp_jump{40.0f};
  float pressure_jump{0.15f};
  float ph_sigma{0.08f};
  float orp_sigma{10.0f};
  float ph_jump{0.12f};
  float tw_jump{1.0f};
};

struct InterferenceFlags {
  bool coincident_jump{false};
  bool shared_noise{false};
  bool tw_coupling{false};
  bool suspected{false};

  bool any() const { return this->coincident_jump || this->shared_noise || this->tw_coupling; }
};

/**
 * Station-level interference detector (Lot 8).
 *
 * Independent of Lot 4: keeps its own rolling window for σ and pH/Tw delta.
 * This is a suspected-interference flag, not a chemistry oracle.
 */
class InterferenceDetector {
 public:
  void set_config(const InterferenceConfig &config) { this->config_ = config; }
  const InterferenceConfig &get_config() const { return this->config_; }
  void set_enable_coincident_jump(bool v) { this->config_.enable_coincident_jump = v; }
  void set_enable_shared_noise(bool v) { this->config_.enable_shared_noise = v; }
  void set_enable_tw_coupling(bool v) { this->config_.enable_tw_coupling = v; }
  void set_hold_ms(uint32_t v) { this->config_.hold_ms = v; }
  void set_jump_window_ms(uint32_t v) { this->config_.jump_window_ms = v; }
  void set_tw_window_ms(uint32_t v) { this->config_.tw_window_ms = v; }
  void set_log_rate_limit_ms(uint32_t v) { this->config_.log_rate_limit_ms = v; }
  void set_orp_jump(float v) { this->config_.orp_jump = v; }
  void set_pressure_jump(float v) { this->config_.pressure_jump = v; }
  void set_ph_sigma(float v) { this->config_.ph_sigma = v; }
  void set_orp_sigma(float v) { this->config_.orp_sigma = v; }
  void set_ph_jump(float v) { this->config_.ph_jump = v; }
  void set_tw_jump(float v) { this->config_.tw_jump = v; }

  void reset();
  void push_channel(uint8_t channel_type, float value, uint32_t now_ms);
  void push_water_temp(float value, uint32_t now_ms);

  InterferenceFlags evaluate(uint32_t now_ms, bool calibration_mode);

  const InterferenceFlags &get_flags() const { return this->flags_; }
  const char *active_reason() const;

  bool should_log_now(uint32_t now_ms) const;
  void mark_logged(uint32_t now_ms) { this->last_log_ms_ = now_ms; }

 protected:
  struct Track {
    float prev{NAN};
    float value{NAN};
    uint32_t t{0};
    float window[INTERF_WINDOW]{};
    uint32_t window_t[INTERF_WINDOW]{};
    uint8_t count{0};
    uint8_t head{0};
    bool has_sample{false};
    bool has_jump{false};
    uint32_t last_jump_ms{0};

    void push(float v, uint32_t ts);
    float sigma() const;
    float delta_over(uint32_t now_ms, uint32_t window_ms) const;
  };

  Track *track_for_(uint8_t channel_type);

  InterferenceConfig config_{};
  Track pressure_{};
  Track ph_{};
  Track orp_{};
  Track water_temp_{};
  InterferenceFlags flags_{};
  uint32_t last_hit_ms_{0};
  uint32_t last_log_ms_{0};
  bool hold_active_{false};
};

class InterferenceSuspectedSensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
};

class InterferenceCoincidentJumpSensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
};

class InterferenceSharedNoiseSensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
};

class InterferenceTwCouplingSensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;
};

}  // namespace pool_station
}  // namespace esphome
