#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace esphome {
namespace pool_station {

/**
 * Channel Filter Configuration.
 * Per-channel filter settings for the signal processing pipeline.
 */
struct ChannelFilterConfig {
  uint8_t filter_samples{0};       // Median window size (0 or 1 = off)
  float max_jump{0.0f};            // Max allowed jump (0 = disabled)
  uint8_t max_jump_streak{3};      // Accept new plateau after N consecutive jumps
  float value_min{NAN};            // Min clamp value (NAN = no min)
  float value_max{NAN};            // Max clamp value (NAN = no max)
  
  bool is_median_enabled() const { return filter_samples > 1; }
  bool is_jump_guard_enabled() const { return max_jump > 0.0f; }
  bool is_clamp_enabled() const { return !std::isnan(value_min) || !std::isnan(value_max); }
};

/**
 * Sliding window buffer for median/sample filtering.
 * Maintains a circular buffer of the last N samples.
 */
class SlidingWindow {
 public:
  SlidingWindow() = default;
  
  void set_size(uint8_t size);
  uint8_t get_size() const { return this->size_; }
  
  void push(float value);
  void clear();
  
  bool is_full() const { return this->size_ > 0 && this->count_ >= this->size_; }
  size_t count() const { return this->count_; }
  
  float median() const;
  float mean() const;
  float min() const;
  float max() const;
  
  const std::vector<float> &values() const { return this->buffer_; }

 protected:
  uint8_t size_{0};
  size_t head_{0};
  size_t count_{0};
  std::vector<float> buffer_;
  // Reused by median() so hot-path sampling does not allocate every call (v0.10.6 / PR #35).
  mutable std::vector<float> scratch_;
};

/**
 * Jump guard state machine.
 * Rejects outlier jumps but accepts new plateau after streak threshold.
 */
class JumpGuard {
 public:
  JumpGuard() = default;
  
  void set_max_jump(float max_jump) { this->max_jump_ = max_jump; }
  void set_streak_threshold(uint8_t threshold) { this->streak_threshold_ = threshold; }
  
  float get_max_jump() const { return this->max_jump_; }
  uint8_t get_streak_threshold() const { return this->streak_threshold_; }
  
  void reset();
  
  /**
   * Process a new value through the jump guard.
   * Returns the accepted value (may be last_accepted if jump rejected).
   * Sets rejected_out to true if the input value was rejected.
   */
  float process(float value, bool &rejected_out);
  
  float get_last_accepted() const { return this->last_accepted_; }
  uint8_t get_current_streak() const { return this->streak_count_; }
  bool has_baseline() const { return this->has_baseline_; }

 protected:
  float max_jump_{0.0f};
  uint8_t streak_threshold_{3};
  
  float last_accepted_{NAN};
  float pending_value_{NAN};
  uint8_t streak_count_{0};
  bool has_baseline_{false};
};

/**
 * Pure functions for filter operations.
 * These are stateless and unit-testable.
 */
namespace filter_functions {

/**
 * Compute median of a vector of values.
 * Returns NAN if empty.
 */
/** Compute median in-place (mutates values). Prefer over a by-value copy on the hot path. */
float compute_median(std::vector<float> &values);

/**
 * Compute mean of a vector of values.
 * Returns NAN if empty.
 */
float compute_mean(const std::vector<float> &values);

/**
 * Clamp a value between min and max.
 * NAN bounds are treated as unbounded.
 */
float clamp_value(float value, float min_val, float max_val);

/**
 * Check if a jump exceeds threshold.
 */
bool is_jump_exceeded(float current, float previous, float threshold);

}  // namespace filter_functions

}  // namespace pool_station
}  // namespace esphome
