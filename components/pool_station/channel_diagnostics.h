#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace esphome {
namespace pool_station {

/**
 * Diagnostics Configuration.
 * Per-channel diagnostic thresholds and settings (Lot 4).
 */
struct DiagnosticsConfig {
  uint8_t noise_window{10};          // Sliding window size for noise stats
  float noise_warn_ptp{0.0f};        // Peak-to-peak threshold for noisy flag (0 = disabled)
  float noise_warn_sigma{0.0f};      // Standard deviation threshold for noisy flag (0 = disabled)
  uint32_t stuck_timeout_ms{0};      // Timeout for stuck detection in ms (0 = disabled)
  float stuck_threshold{0.001f};     // Minimum change to consider "not stuck"
  float range_min{NAN};              // Out-of-range minimum (NAN = no check)
  float range_max{NAN};              // Out-of-range maximum (NAN = no check)
  uint32_t log_rate_limit_ms{60000}; // Rate limit for diagnostic log messages (default 1 min)
  
  bool is_noise_detection_enabled() const { 
    return noise_window > 1 && (noise_warn_ptp > 0.0f || noise_warn_sigma > 0.0f); 
  }
  bool is_stuck_detection_enabled() const { return stuck_timeout_ms > 0; }
  bool is_range_check_enabled() const { return !std::isnan(range_min) || !std::isnan(range_max); }
};

/**
 * Diagnostic Statistics.
 * Computed statistics from the sliding window.
 */
struct DiagnosticsStats {
  float mean{NAN};           // Mean of values in window
  float sigma{NAN};          // Standard deviation (σ)
  float ptp{NAN};            // Peak-to-peak (max - min)
  float min{NAN};            // Minimum value in window
  float max{NAN};            // Maximum value in window
  size_t sample_count{0};    // Number of valid samples in window
  
  bool is_valid() const { return sample_count > 0 && !std::isnan(mean); }
};

/**
 * Diagnostic Flags.
 * Boolean indicators for abnormal conditions.
 */
struct DiagnosticsFlags {
  bool noisy{false};         // True if noise exceeds threshold (ptp or sigma)
  bool stuck{false};         // True if no meaningful change for stuck_timeout
  bool out_of_range{false};  // True if value outside configured range
  
  // Stubs for future Lot 6 integration
  bool cal_invalid{false};      // Placeholder: calibration invalid (managed by CalibrationEngine)
  bool gate_blocked{false};     // Placeholder: gate conditions not met (Lot 6)
  bool campaign_running{false}; // Placeholder: campaign in progress (Lot 6)
  
  bool has_any_problem() const { return noisy || stuck || out_of_range; }
};

/**
 * Diagnostics Window.
 * Specialized sliding window for computing diagnostic statistics.
 * Uses Welford's online algorithm for mean/variance.
 */
class DiagnosticsWindow {
 public:
  DiagnosticsWindow() = default;
  
  void set_size(uint8_t size);
  uint8_t get_size() const { return this->size_; }
  
  void push(float value, uint32_t timestamp_ms);
  void clear();
  
  bool is_full() const { return this->count_ >= this->size_; }
  size_t count() const { return this->count_; }
  
  DiagnosticsStats compute_stats() const;
  
  float get_last_value() const { return this->last_value_; }
  uint32_t get_last_timestamp() const { return this->last_timestamp_; }

 protected:
  uint8_t size_{10};
  size_t head_{0};
  size_t count_{0};
  std::vector<float> buffer_;
  
  float last_value_{NAN};
  uint32_t last_timestamp_{0};
};

/**
 * Stuck Detector.
 * Detects when a sensor value hasn't meaningfully changed for a period.
 */
class StuckDetector {
 public:
  StuckDetector() = default;
  
  void set_timeout_ms(uint32_t timeout) { this->timeout_ms_ = timeout; }
  void set_threshold(float threshold) { this->threshold_ = threshold; }
  
  void update(float value, uint32_t timestamp_ms);
  void reset();
  
  bool is_stuck() const { return this->is_stuck_; }
  uint32_t get_stuck_duration_ms() const;
  float get_last_change_value() const { return this->last_change_value_; }

 protected:
  uint32_t timeout_ms_{0};
  float threshold_{0.001f};
  
  float last_change_value_{NAN};
  uint32_t last_change_time_{0};
  bool is_stuck_{false};
  bool initialized_{false};
};

/**
 * Channel Diagnostics Manager.
 * Coordinates diagnostic statistics and flags for a single channel.
 */
class ChannelDiagnostics {
 public:
  ChannelDiagnostics() = default;
  
  void set_config(const DiagnosticsConfig &config);
  const DiagnosticsConfig &get_config() const { return this->config_; }
  
  void process(float calibrated_value, uint32_t timestamp_ms);
  void reset();
  
  const DiagnosticsStats &get_stats() const { return this->stats_; }
  const DiagnosticsFlags &get_flags() const { return this->flags_; }
  
  float get_last_jump_magnitude() const { return this->last_jump_magnitude_; }
  uint8_t get_jump_streak() const { return this->jump_streak_; }
  
  bool should_log_now(uint32_t now_ms) const;
  void mark_logged(uint32_t now_ms);

 protected:
  void update_flags_(float value);
  void update_jump_state_(float value);
  
  DiagnosticsConfig config_;
  DiagnosticsWindow window_;
  StuckDetector stuck_detector_;
  
  DiagnosticsStats stats_;
  DiagnosticsFlags flags_;
  
  float last_value_{NAN};
  float last_jump_magnitude_{0.0f};
  uint8_t jump_streak_{0};
  
  uint32_t last_log_time_{0};
};

/**
 * Pure functions for diagnostic computations.
 * Stateless and unit-testable.
 */
namespace diagnostic_functions {

/**
 * Compute standard deviation using Welford's algorithm.
 */
float compute_stddev(const std::vector<float> &values);

/**
 * Compute peak-to-peak (max - min).
 */
float compute_ptp(const std::vector<float> &values);

/**
 * Check if value is out of configured range.
 */
bool is_out_of_range(float value, float range_min, float range_max);

}  // namespace diagnostic_functions

}  // namespace pool_station
}  // namespace esphome
