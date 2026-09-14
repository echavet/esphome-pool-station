#pragma once

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <functional>
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace pool_station {

static const char *const CAL_TAG = "pool_station.cal";

/**
 * Calibration algorithm types.
 * Must match Python CALIBRATION_TYPE dict.
 */
enum CalibrationType : uint8_t {
  CAL_TYPE_NONE = 0,
  CAL_TYPE_LINEAR = 1,
  CAL_TYPE_POLYNOMIAL = 2,
  CAL_TYPE_PIECEWISE = 3,
  CAL_TYPE_DFROBOT_ORP = 4,
  CAL_TYPE_EXPONENTIAL = 5,  // TODO: implement
  CAL_TYPE_LOGARITHMIC = 6,  // TODO: implement
  CAL_TYPE_POWER = 7,        // TODO: implement
};

/**
 * Single calibration point (x=raw voltage, y=calibrated value).
 */
struct CalibrationPoint {
  float x{NAN};  // Raw voltage
  float y{NAN};  // Calibrated value (pH, bar, mV, etc.)
  
  bool is_valid() const {
    return !std::isnan(x) && !std::isnan(y);
  }
  
  static bool compare_by_x(const CalibrationPoint &a, const CalibrationPoint &b) {
    return a.x < b.x;
  }
};

/**
 * DFRobot ORP (SEN0165-like) parameters.
 * Formula: ORP_mV = mid_mv - (raw_voltage * 1000) - offset_mv
 * 
 * - mid_mv: midpoint voltage in mV (default 2500 for 2.5V reference)
 * - offset_mv: offset calibration from known solution
 */
struct DFRobotORPParams {
  float mid_mv{2500.0f};     // Midpoint reference (mV)
  float offset_mv{0.0f};     // Offset correction (mV)
};

/**
 * Calibration configuration for a channel.
 */
struct CalibrationConfig {
  CalibrationType type{CAL_TYPE_NONE};
  uint8_t polynomial_order{2};      // For polynomial: order (2=quadratic, 3=cubic, etc.)
  uint8_t precision_decimals{2};    // Output precision
  DFRobotORPParams dfrobot_params;  // DFRobot ORP specific params
  
  // YAML-seeded points (default calibration, can be overridden at runtime)
  std::vector<CalibrationPoint> seed_points;
};

// Maximum points we can persist (memory constraint on ESP32)
static constexpr uint8_t MAX_CALIBRATION_POINTS = 10;

// Preference storage structure for a single channel
struct CalibrationPrefsData {
  uint32_t magic{0};          // Magic number to validate data
  uint8_t point_count{0};
  float points_x[MAX_CALIBRATION_POINTS];
  float points_y[MAX_CALIBRATION_POINTS];
  float dfrobot_mid_mv{2500.0f};
  float dfrobot_offset_mv{0.0f};
  uint8_t calibration_type{0};
  // Lot 5: Water temperature at calibration time
  float calibration_temperature{NAN};  // Tw in °C when calibration was saved
};

static constexpr uint32_t CALIBRATION_PREFS_MAGIC_V2 = 0xCA110002;  // Lot 2 magic (legacy)
static constexpr uint32_t CALIBRATION_PREFS_MAGIC = 0xCA110005;     // Lot 5 magic (with Tw)

/**
 * Calibration Engine - computes calibrated values from raw readings.
 * 
 * Supports multiple algorithms:
 * - LINEAR: 2-point linear interpolation/extrapolation
 * - POLYNOMIAL: N-point polynomial regression (order configurable)
 * - PIECEWISE: Linear interpolation between adjacent points
 * - DFROBOT_ORP: SEN0165-like mid/offset formula
 * 
 * Design:
 * - Points can be seeded from YAML and overridden at runtime
 * - Runtime changes persist to flash (ESPHome preferences)
 * - Validation ensures minimum points for algorithm
 * 
 * Lot 7 / v0.7.17:
 * - Draft/Save workflow: Capturer edits draft; live used for publishing
 * - Save = commit_draft (draft → live + flash); Discard restores live
 * - Runtime algorithm selection is drafted when draft_mode is on
 * - Add/remove point functionality
 */
class CalibrationEngine {
 public:
  CalibrationEngine() = default;
  
  // Configuration
  void set_type(CalibrationType type);
  // Working type: draft when draft_mode is on, otherwise live.
  CalibrationType get_type() const { return this->type_; }
  // Committed type used by calibrate() / is_valid() / flash save.
  CalibrationType get_live_type() const { return this->live_type_; }
  const char *get_type_name() const;
  
  void set_polynomial_order(uint8_t order) { this->polynomial_order_ = order; }
  uint8_t get_polynomial_order() const { return this->polynomial_order_; }
  
  void set_precision(uint8_t decimals) { this->precision_decimals_ = decimals; }
  uint8_t get_precision() const { return this->precision_decimals_; }
  
  // DFRobot ORP specific (working/draft values; published calibrate uses live)
  void set_dfrobot_mid_mv(float mid);
  float get_dfrobot_mid_mv() const { return this->dfrobot_mid_mv_; }
  
  void set_dfrobot_offset_mv(float offset);
  float get_dfrobot_offset_mv() const { return this->dfrobot_offset_mv_; }
  
  // Calibration temperature (Lot 5) - Tw at last save
  void set_calibration_temperature(float temp) { this->calibration_temperature_ = temp; }
  float get_calibration_temperature() const { return this->calibration_temperature_; }
  bool has_calibration_temperature() const { return !std::isnan(this->calibration_temperature_); }
  
  // =========================================================================
  // Point management (Lot 7: operates on draft set when draft mode enabled)
  // =========================================================================
  void add_point(float x, float y);
  void set_point(size_t index, float x, float y);
  void remove_point(size_t index);
  void clear_points();
  
  size_t get_point_count() const;
  CalibrationPoint get_point(size_t index) const;
  const std::vector<CalibrationPoint> &get_points() const;
  
  // Seed points from YAML (initial calibration, can be replaced)
  void set_seed_points(const std::vector<CalibrationPoint> &points);
  
  // =========================================================================
  // Draft/Commit workflow (Lot 7)
  // =========================================================================
  // Enable draft mode - subsequent edits go to draft set
  void enable_draft_mode();
  // Disable draft mode - edits go directly to live set (legacy behavior)
  void disable_draft_mode();
  // Check if draft mode is active
  bool is_draft_mode() const { return this->draft_mode_enabled_; }
  // Check if there are uncommitted draft changes
  bool has_draft_changes() const { return this->has_draft_changes_; }
  
  // Commit draft changes to live set (also saves to preferences)
  void commit_draft();
  // Discard draft changes and revert to live set
  void discard_draft();
  // Copy live → draft without logging (prefs load / enable)
  void sync_draft_from_live();
  
  // Get the live/committed point count (for calibration)
  size_t get_live_point_count() const { return this->live_points_.size(); }
  const std::vector<CalibrationPoint> &get_live_points() const { return this->live_points_; }
  
  // =========================================================================
  // Runtime algorithm change (Lot 7)
  // =========================================================================
  // Change algorithm at runtime and revalidate points
  void set_type_runtime(CalibrationType type);
  // Callback type for validation state changes
  using ValidationCallback = std::function<void(bool is_valid)>;
  void set_validation_callback(ValidationCallback callback) { this->validation_callback_ = callback; }
  
  // Validation
  bool is_valid() const;
  bool is_draft_valid() const;
  uint8_t get_minimum_points() const;
  uint8_t get_minimum_points_for_type(CalibrationType type) const;
  // Stub algorithms (exponential/logarithmic/power) are not implemented.
  // is_valid() is false for those types so cal_invalid reflects reality.
  static bool is_type_implemented(CalibrationType type);
  // Published (live) implementation state — draft algo stubs do not flip this.
  bool is_implemented() const { return is_type_implemented(this->live_type_); }
  
  // Main calibration function (uses LIVE points, not draft)
  float calibrate(float raw_voltage) const;
  
  // Persistence
  void set_preferences_key(uint32_t key) { this->prefs_key_ = key; }
  void load_from_preferences();
  void save_to_preferences();
  
  // Dump configuration
  void dump_config() const;

 protected:
  // Algorithm implementations
  float calibrate_linear_(float x) const;
  float calibrate_polynomial_(float x) const;
  float calibrate_piecewise_(float x) const;
  float calibrate_dfrobot_orp_(float x) const;
  
  // Polynomial helper: least squares fit
  void compute_polynomial_coefficients_() const;

  // Apply YAML precision_decimals_ to a calibrated value.
  float apply_precision_(float value) const;

  // Working copy of valid points. Sort only this copy (never live/draft slots).
  std::vector<CalibrationPoint> valid_points_copy_(
      const std::vector<CalibrationPoint> &src, bool sort_by_x) const;
  
  CalibrationType type_{CAL_TYPE_NONE};       // working / draft UI type
  CalibrationType live_type_{CAL_TYPE_NONE};  // committed type for publish
  uint8_t polynomial_order_{2};
  uint8_t precision_decimals_{2};
  
  // DFRobot ORP parameters (working / draft)
  float dfrobot_mid_mv_{2500.0f};
  float dfrobot_offset_mv_{0.0f};
  // Committed DFRobot params used by calibrate()
  float live_dfrobot_mid_mv_{2500.0f};
  float live_dfrobot_offset_mv_{0.0f};
  
  // Calibration temperature (Lot 5) - Tw at last save
  float calibration_temperature_{NAN};
  
  // =========================================================================
  // Lot 7: Draft/Commit workflow
  // =========================================================================
  // Live/committed points (used for actual calibration)
  std::vector<CalibrationPoint> live_points_;
  // Draft points (edited by Capturer UI, not used until committed)
  std::vector<CalibrationPoint> draft_points_;
  // Draft mode state
  bool draft_mode_enabled_{false};
  bool has_draft_changes_{false};
  
  // For backward compatibility, points_ is an alias to current working set
  std::vector<CalibrationPoint> &points_() { 
    return draft_mode_enabled_ ? draft_points_ : live_points_; 
  }
  const std::vector<CalibrationPoint> &points_() const { 
    return draft_mode_enabled_ ? draft_points_ : live_points_; 
  }
  
  // Validation state change callback (Lot 7)
  ValidationCallback validation_callback_{nullptr};
  void notify_validation_changed_();
  void mark_draft_dirty_();
  void commit_working_params_to_live_();
  bool is_points_valid_(const std::vector<CalibrationPoint> &points, CalibrationType type) const;
  
  // Polynomial coefficients (computed on demand, uses live points)
  mutable std::vector<float> poly_coeffs_;
  mutable bool poly_coeffs_valid_{false};
  
  // Preferences
  uint32_t prefs_key_{0};
  ESPPreferenceObject prefs_;
};

/**
 * Get readable name for calibration type.
 */
inline const char *calibration_type_name(CalibrationType type) {
  switch (type) {
    case CAL_TYPE_NONE: return "none";
    case CAL_TYPE_LINEAR: return "linear";
    case CAL_TYPE_POLYNOMIAL: return "polynomial";
    case CAL_TYPE_PIECEWISE: return "piecewise";
    case CAL_TYPE_DFROBOT_ORP: return "dfrobot_orp";
    case CAL_TYPE_EXPONENTIAL: return "exponential";
    case CAL_TYPE_LOGARITHMIC: return "logarithmic";
    case CAL_TYPE_POWER: return "power";
    default: return "unknown";
  }
}

}  // namespace pool_station
}  // namespace esphome
