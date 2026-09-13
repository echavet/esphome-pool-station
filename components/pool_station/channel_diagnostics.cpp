#include "channel_diagnostics.h"
#include <algorithm>
#include <numeric>
#include <cmath>

namespace esphome {
namespace pool_station {

// ============================================================================
// DiagnosticsWindow
// ============================================================================

void DiagnosticsWindow::set_size(uint8_t size) {
  this->size_ = size > 0 ? size : 1;
  this->buffer_.resize(this->size_, NAN);
  this->head_ = 0;
  this->count_ = 0;
}

void DiagnosticsWindow::push(float value, uint32_t timestamp_ms) {
  if (this->size_ == 0) {
    return;
  }
  
  this->buffer_[this->head_] = value;
  this->head_ = (this->head_ + 1) % this->size_;
  
  if (this->count_ < this->size_) {
    this->count_++;
  }
  
  this->last_value_ = value;
  this->last_timestamp_ = timestamp_ms;
}

void DiagnosticsWindow::clear() {
  this->head_ = 0;
  this->count_ = 0;
  std::fill(this->buffer_.begin(), this->buffer_.end(), NAN);
  this->last_value_ = NAN;
  this->last_timestamp_ = 0;
}

DiagnosticsStats DiagnosticsWindow::compute_stats() const {
  DiagnosticsStats stats;
  
  if (this->count_ == 0) {
    return stats;
  }
  
  std::vector<float> valid;
  valid.reserve(this->count_);
  
  for (size_t i = 0; i < this->count_; i++) {
    float v = this->buffer_[i];
    if (!std::isnan(v)) {
      valid.push_back(v);
    }
  }
  
  if (valid.empty()) {
    return stats;
  }
  
  stats.sample_count = valid.size();
  
  float sum = 0.0f;
  stats.min = std::numeric_limits<float>::max();
  stats.max = std::numeric_limits<float>::lowest();
  
  for (float v : valid) {
    sum += v;
    stats.min = std::min(stats.min, v);
    stats.max = std::max(stats.max, v);
  }
  
  stats.mean = sum / static_cast<float>(valid.size());
  stats.ptp = stats.max - stats.min;
  stats.sigma = diagnostic_functions::compute_stddev(valid);
  
  return stats;
}

// ============================================================================
// StuckDetector
// ============================================================================

void StuckDetector::update(float value, uint32_t timestamp_ms) {
  if (std::isnan(value)) {
    return;
  }
  
  if (!this->initialized_) {
    this->last_change_value_ = value;
    this->last_change_time_ = timestamp_ms;
    this->initialized_ = true;
    this->is_stuck_ = false;
    return;
  }
  
  float delta = std::abs(value - this->last_change_value_);
  
  if (delta > this->threshold_) {
    this->last_change_value_ = value;
    this->last_change_time_ = timestamp_ms;
    this->is_stuck_ = false;
  } else {
    if (this->timeout_ms_ > 0) {
      uint32_t elapsed = timestamp_ms - this->last_change_time_;
      this->is_stuck_ = (elapsed >= this->timeout_ms_);
    }
  }
}

void StuckDetector::reset() {
  this->last_change_value_ = NAN;
  this->last_change_time_ = 0;
  this->is_stuck_ = false;
  this->initialized_ = false;
}

uint32_t StuckDetector::get_stuck_duration_ms() const {
  if (!this->is_stuck_ || !this->initialized_) {
    return 0;
  }
  return 0;
}

// ============================================================================
// ChannelDiagnostics
// ============================================================================

void ChannelDiagnostics::set_config(const DiagnosticsConfig &config) {
  this->config_ = config;
  
  if (config.noise_window > 0) {
    this->window_.set_size(config.noise_window);
  }
  
  this->stuck_detector_.set_timeout_ms(config.stuck_timeout_ms);
  this->stuck_detector_.set_threshold(config.stuck_threshold);
}

void ChannelDiagnostics::process(float calibrated_value, uint32_t timestamp_ms) {
  if (std::isnan(calibrated_value)) {
    return;
  }
  
  this->window_.push(calibrated_value, timestamp_ms);
  
  if (this->config_.is_noise_detection_enabled() && this->window_.is_full()) {
    this->stats_ = this->window_.compute_stats();
  }
  
  if (this->config_.is_stuck_detection_enabled()) {
    this->stuck_detector_.update(calibrated_value, timestamp_ms);
  }
  
  this->update_jump_state_(calibrated_value);
  this->update_flags_(calibrated_value);
  
  this->last_value_ = calibrated_value;
}

void ChannelDiagnostics::reset() {
  this->window_.clear();
  this->stuck_detector_.reset();
  this->stats_ = DiagnosticsStats();
  this->flags_ = DiagnosticsFlags();
  this->last_value_ = NAN;
  this->last_jump_magnitude_ = 0.0f;
  this->jump_streak_ = 0;
}

void ChannelDiagnostics::update_flags_(float value) {
  this->flags_.noisy = false;
  if (this->config_.is_noise_detection_enabled() && this->stats_.is_valid()) {
    if (this->config_.noise_warn_ptp > 0.0f && this->stats_.ptp > this->config_.noise_warn_ptp) {
      this->flags_.noisy = true;
    }
    if (this->config_.noise_warn_sigma > 0.0f && this->stats_.sigma > this->config_.noise_warn_sigma) {
      this->flags_.noisy = true;
    }
  }
  
  this->flags_.stuck = this->config_.is_stuck_detection_enabled() && this->stuck_detector_.is_stuck();
  
  this->flags_.out_of_range = false;
  if (this->config_.is_range_check_enabled()) {
    this->flags_.out_of_range = diagnostic_functions::is_out_of_range(
        value, this->config_.range_min, this->config_.range_max);
  }
}

void ChannelDiagnostics::update_jump_state_(float value) {
  if (std::isnan(this->last_value_)) {
    this->last_jump_magnitude_ = 0.0f;
    this->jump_streak_ = 0;
    return;
  }
  
  float jump = std::abs(value - this->last_value_);
  
  if (jump > this->last_jump_magnitude_ * 0.5f && jump > 0.001f) {
    this->jump_streak_++;
    if (this->jump_streak_ > 255) {
      this->jump_streak_ = 255;
    }
  } else {
    this->jump_streak_ = 0;
  }
  
  this->last_jump_magnitude_ = jump;
}

bool ChannelDiagnostics::should_log_now(uint32_t now_ms) const {
  if (this->config_.log_rate_limit_ms == 0) {
    return true;
  }
  return (now_ms - this->last_log_time_) >= this->config_.log_rate_limit_ms;
}

void ChannelDiagnostics::mark_logged(uint32_t now_ms) {
  this->last_log_time_ = now_ms;
}

// ============================================================================
// Pure diagnostic functions
// ============================================================================

namespace diagnostic_functions {

float compute_stddev(const std::vector<float> &values) {
  if (values.size() < 2) {
    return 0.0f;
  }
  
  double mean = 0.0;
  double m2 = 0.0;
  size_t count = 0;
  
  for (float v : values) {
    if (!std::isnan(v)) {
      count++;
      double delta = v - mean;
      mean += delta / static_cast<double>(count);
      double delta2 = v - mean;
      m2 += delta * delta2;
    }
  }
  
  if (count < 2) {
    return 0.0f;
  }
  
  double variance = m2 / static_cast<double>(count - 1);
  return static_cast<float>(std::sqrt(variance));
}

float compute_ptp(const std::vector<float> &values) {
  if (values.empty()) {
    return NAN;
  }
  
  float min_val = std::numeric_limits<float>::max();
  float max_val = std::numeric_limits<float>::lowest();
  bool found = false;
  
  for (float v : values) {
    if (!std::isnan(v)) {
      min_val = std::min(min_val, v);
      max_val = std::max(max_val, v);
      found = true;
    }
  }
  
  return found ? (max_val - min_val) : NAN;
}

bool is_out_of_range(float value, float range_min, float range_max) {
  if (std::isnan(value)) {
    return false;
  }
  
  if (!std::isnan(range_min) && value < range_min) {
    return true;
  }
  
  if (!std::isnan(range_max) && value > range_max) {
    return true;
  }
  
  return false;
}

}  // namespace diagnostic_functions

}  // namespace pool_station
}  // namespace esphome
