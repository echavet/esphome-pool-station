#include "interference_detector.h"

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station.interf";

void InterferenceDetector::Track::push(float v, uint32_t ts) {
  this->prev = this->has_sample ? this->value : NAN;
  this->value = v;
  this->t = ts;
  this->has_sample = true;
  this->window[this->head] = v;
  this->window_t[this->head] = ts;
  this->head = static_cast<uint8_t>((this->head + 1) % INTERF_WINDOW);
  if (this->count < INTERF_WINDOW)
    this->count++;
}

float InterferenceDetector::Track::sigma() const {
  if (this->count < 2)
    return NAN;
  // After wrap, all INTERF_WINDOW slots are live. Before wrap, only [0, count).
  return interference_functions::stddev(this->window, this->count);
}

float InterferenceDetector::Track::delta_over(uint32_t now_ms, uint32_t window_ms) const {
  if (!this->has_sample || std::isnan(this->value))
    return NAN;
  bool found = false;
  uint32_t oldest_t = 0;
  float oldest_v = NAN;
  for (uint8_t i = 0; i < this->count; i++) {
    if (interference_functions::time_delta_ms(now_ms, this->window_t[i]) > window_ms)
      continue;
    if (!found || interference_functions::is_not_later(this->window_t[i], oldest_t)) {
      found = true;
      oldest_t = this->window_t[i];
      oldest_v = this->window[i];
    }
  }
  if (!found || std::isnan(oldest_v))
    return NAN;
  return this->value - oldest_v;
}

InterferenceDetector::Track *InterferenceDetector::track_for_(uint8_t channel_type) {
  switch (channel_type) {
    case INTERF_CH_PRESSURE:
      return &this->pressure_;
    case INTERF_CH_PH:
      return &this->ph_;
    case INTERF_CH_ORP:
      return &this->orp_;
    case INTERF_CH_WATER_TEMP:
      return &this->water_temp_;
    default:
      return nullptr;
  }
}

void InterferenceDetector::reset() {
  this->pressure_ = Track{};
  this->ph_ = Track{};
  this->orp_ = Track{};
  this->water_temp_ = Track{};
  this->flags_ = InterferenceFlags{};
  this->last_hit_ms_ = 0;
  this->hold_active_ = false;
}

void InterferenceDetector::push_channel(uint8_t channel_type, float value, uint32_t now_ms) {
  Track *tr = this->track_for_(channel_type);
  if (tr == nullptr || std::isnan(value))
    return;
  tr->push(value, now_ms);
}

void InterferenceDetector::push_water_temp(float value, uint32_t now_ms) {
  this->push_channel(INTERF_CH_WATER_TEMP, value, now_ms);
}

InterferenceFlags InterferenceDetector::evaluate(uint32_t now_ms, bool calibration_mode) {
  if (calibration_mode) {
    this->flags_ = InterferenceFlags{};
    this->hold_active_ = false;
    return this->flags_;
  }

  InterferenceFlags live{};
  if (this->config_.enable_coincident_jump) {
    if (interference_functions::is_jump(this->orp_.prev, this->orp_.value, this->config_.orp_jump)) {
      this->orp_.has_jump = true;
      this->orp_.last_jump_ms = this->orp_.t;
    }
    if (interference_functions::is_jump(this->pressure_.prev, this->pressure_.value,
                                        this->config_.pressure_jump)) {
      this->pressure_.has_jump = true;
      this->pressure_.last_jump_ms = this->pressure_.t;
    }
    live.coincident_jump = interference_functions::coincident_jump(
        this->orp_.has_jump, this->orp_.last_jump_ms, this->pressure_.has_jump,
        this->pressure_.last_jump_ms, now_ms, this->config_.jump_window_ms);
  }
  if (this->config_.enable_shared_noise) {
    live.shared_noise = interference_functions::shared_noise(
        this->ph_.sigma(), this->orp_.sigma(), this->config_.ph_sigma, this->config_.orp_sigma);
  }
  if (this->config_.enable_tw_coupling) {
    live.tw_coupling = interference_functions::tw_coupling(
        this->ph_.delta_over(now_ms, this->config_.tw_window_ms),
        this->water_temp_.delta_over(now_ms, this->config_.tw_window_ms), this->config_.ph_jump,
        this->config_.tw_jump);
  }

  if (live.any()) {
    this->last_hit_ms_ = now_ms;
    this->hold_active_ = true;
    this->flags_ = live;
    this->flags_.suspected = true;
  } else if (this->hold_active_) {
    if (interference_functions::time_delta_ms(now_ms, this->last_hit_ms_) >= this->config_.hold_ms) {
      this->hold_active_ = false;
      this->flags_ = InterferenceFlags{};
    } else {
      this->flags_.suspected = true;
    }
  } else {
    this->flags_ = InterferenceFlags{};
  }

  return this->flags_;
}

const char *InterferenceDetector::active_reason() const {
  if (this->flags_.coincident_jump)
    return "coincident_jump";
  if (this->flags_.shared_noise)
    return "shared_noise";
  if (this->flags_.tw_coupling)
    return "tw_coupling";
  if (this->flags_.suspected)
    return "hold";
  return "none";
}

bool InterferenceDetector::should_log_now(uint32_t now_ms) const {
  if (!this->flags_.suspected)
    return false;
  return this->last_log_ms_ == 0 ||
         interference_functions::time_delta_ms(now_ms, this->last_log_ms_) >= this->config_.log_rate_limit_ms;
}

void InterferenceSuspectedSensor::setup() { this->publish_state(false); }
void InterferenceSuspectedSensor::dump_config() { LOG_BINARY_SENSOR("", "Interference Suspected", this); }

void InterferenceCoincidentJumpSensor::setup() { this->publish_state(false); }
void InterferenceCoincidentJumpSensor::dump_config() { LOG_BINARY_SENSOR("", "Interference Coincident Jump", this); }

void InterferenceSharedNoiseSensor::setup() { this->publish_state(false); }
void InterferenceSharedNoiseSensor::dump_config() { LOG_BINARY_SENSOR("", "Interference Shared Noise", this); }

void InterferenceTwCouplingSensor::setup() { this->publish_state(false); }
void InterferenceTwCouplingSensor::dump_config() { LOG_BINARY_SENSOR("", "Interference Tw Coupling", this); }

}  // namespace pool_station
}  // namespace esphome
