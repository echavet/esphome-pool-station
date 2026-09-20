#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
#include "esphome/core/application.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "calibration_engine.h"
#include "channel_filter.h"
#include "channel_diagnostics.h"
#include "temperature_compensation.h"
#include "measurement_gate.h"
#include "measurement_campaign.h"
#include "interference_detector.h"
#include "ads_runtime.h"
#ifdef USE_ADS1115
#include "esphome/components/ads1115/sensor/ads1115_sensor.h"
#endif
#include <vector>
#include <map>
#include <functional>

namespace esphome {
namespace pool_station {

/**
 * Channel types for ADS1115-backed sensors.
 * Must match Python CHANNEL_TYPE_* constants in __init__.py
 */
enum ChannelType : uint8_t {
  CHANNEL_TYPE_PRESSURE = 0,
  CHANNEL_TYPE_PH = 1,
  CHANNEL_TYPE_ORP = 2,
};

/**
 * Legacy sensor role identifiers (Lot 0 compatibility).
 * Must match Python SENSOR_ROLE dict in sensor.py
 * 
 * IMPORTANT: Anti-swap rule - sensor roles are bound to physical addresses,
 * not scan order. See docs/SENSOR-IDENTITY.md
 */
enum SensorRole : uint8_t {
  ROLE_WATER_TEMPERATURE = 0,
  ROLE_AIR_PUMP = 1,
  ROLE_AIR_LOCAL = 2,
  ROLE_PH_RAW = 10,
  ROLE_PH_CALIBRATED = 11,
  ROLE_ORP_RAW = 20,
  ROLE_ORP_CALIBRATED = 21,
  ROLE_PRESSURE_RAW = 30,
  ROLE_PRESSURE_CALIBRATED = 31,
};

// Forward declarations
class PoolStationComponent;
class PoolStationSensor;
class PoolStationChannelSensor;
class CalibrationModeSwitch;

// Calibration UI forward declarations
class CalibrationPointXNumber;
class CalibrationPointYNumber;
class DFRobotMidNumber;
class DFRobotOffsetNumber;
class CalibrationCaptureButton;
class CalibrationSaveButton;
class CalibrationInvalidSensor;

// Lot 7: New UI forward declarations
class CalibrationAlgorithmSelect;
class CalibrationAddPointButton;
class CalibrationRemovePointButton;
class CalibrationCommitButton;
class CalibrationDiscardButton;
class CalibrationPointCountNumber;
class DraftPendingSensor;
class DraftInvalidSensor;

// Diagnostic flag forward declarations (Lot 4)
class DiagnosticNoisyFlag;
class DiagnosticStuckFlag;
class DiagnosticOutOfRangeFlag;

// Temperature compensation forward declarations (Lot 5)
class TempCompensationSwitch;

// Gates and campaigns forward declarations (Lot 6)
class MeasurementGate;
class MeasurementCampaign;
class GateBlockedBinarySensor;
class CampaignStartButton;
class CampaignAbortButton;
class CampaignRunningSensor;
class CampaignResultSensor;

// Interference detection forward declarations (Lot 8)
class InterferenceDetector;
class InterferenceSuspectedSensor;
class InterferenceCoincidentJumpSensor;
class InterferenceSharedNoiseSensor;
class InterferenceTwCouplingSensor;

// Lot A: runtime ADS gain / saturation overlay
class AdsGainSelect;
class AdsSaturatedBinarySensor;
class AdsGainMismatchSensor;
class AdsResetYamlButton;
class FilterRuntimeNumber;
class FiltersResetYamlButton;

/**
 * Main pool_station component.
 * 
 * Orchestrates sensor readings, calibration, and diagnostics.
 * Lot 2: N-point calibration with persistence and HA UI.
 * 
 * Design principles:
 * - Composes ESPHome ads1115/dallas/gpio — does NOT reimplement drivers
 * - Channels listen to source sensors and publish transformed values
 * - Calibration mode switch enables fast update intervals
 * - Calibration engine per channel with persistence to flash
 */
class PoolStationComponent : public PollingComponent {
 public:
  PoolStationComponent() = default;

  void setup() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Configuration setters
  void set_calibration_interval(uint32_t interval_ms) { this->calibration_interval_ = interval_ms; }
  void set_water_temperature_sensor(sensor::Sensor *sensor) { this->water_temp_sensor_ = sensor; }
  void set_calibration_mode_switch(CalibrationModeSwitch *sw);

  // Channel registration
  void register_channel(PoolStationChannelSensor *channel, uint8_t type);

  // Legacy sensor registration (Lot 0 compatibility)
  void register_sensor(PoolStationSensor *sensor, uint8_t role);

  // Accessors
  float get_water_temperature() const;
  bool has_water_temperature() const { return this->water_temp_sensor_ != nullptr; }
  bool is_calibration_mode() const { return this->calibration_mode_active_; }
  uint32_t get_calibration_interval() const { return this->calibration_interval_; }

  // Channel access (for UI components)
  PoolStationChannelSensor *get_channel(uint8_t type);

  // Called by calibration switch
  void set_calibration_mode_active(bool active);

  // Calibration UI registration
  void register_point_x_number(CalibrationPointXNumber *num, uint8_t channel_type, uint8_t point_index);
  void register_point_y_number(CalibrationPointYNumber *num, uint8_t channel_type, uint8_t point_index);
  void register_dfrobot_mid_number(DFRobotMidNumber *num, uint8_t channel_type);
  void register_dfrobot_offset_number(DFRobotOffsetNumber *num, uint8_t channel_type);
  void register_cal_invalid_sensor(CalibrationInvalidSensor *sensor, uint8_t channel_type);
  void register_algorithm_select(CalibrationAlgorithmSelect *sel, uint8_t channel_type);
  void register_point_count_number(CalibrationPointCountNumber *num, uint8_t channel_type);
  void register_draft_pending_sensor(DraftPendingSensor *sensor, uint8_t channel_type);
  void register_draft_invalid_sensor(DraftInvalidSensor *sensor, uint8_t channel_type);

  // Push engine state to all registered Capturer widgets for this channel.
  void refresh_calibration_ui(uint8_t channel_type);

  // Publish a captured raw voltage onto the HA X number for that slot.
  // Used after Capturer so the Étalonnage X field updates even if the
  // engine point is not yet fully valid (Y still NaN) or was re-sorted.
  void publish_captured_point_x(uint8_t channel_type, uint8_t point_index, float x);

  // Campaign registration (Lot 6)
  void register_campaign(MeasurementCampaign *campaign);
  MeasurementCampaign *get_campaign(const std::string &name);
  const std::map<std::string, MeasurementCampaign *> &get_campaigns() const { return this->campaigns_; }

  // Interference detection (Lot 8)
  void set_interference_enabled(bool enabled) { this->interference_enabled_ = enabled; }
  void set_interference_enable_coincident_jump(bool v) { this->interference_.set_enable_coincident_jump(v); }
  void set_interference_enable_shared_noise(bool v) { this->interference_.set_enable_shared_noise(v); }
  void set_interference_enable_tw_coupling(bool v) { this->interference_.set_enable_tw_coupling(v); }
  void set_interference_hold_ms(uint32_t v) { this->interference_.set_hold_ms(v); }
  void set_interference_jump_window_ms(uint32_t v) { this->interference_.set_jump_window_ms(v); }
  void set_interference_tw_window_ms(uint32_t v) { this->interference_.set_tw_window_ms(v); }
  void set_interference_log_rate_limit_ms(uint32_t v) { this->interference_.set_log_rate_limit_ms(v); }
  void set_interference_orp_jump(float v) { this->interference_.set_orp_jump(v); }
  void set_interference_pressure_jump(float v) { this->interference_.set_pressure_jump(v); }
  void set_interference_ph_sigma(float v) { this->interference_.set_ph_sigma(v); }
  void set_interference_orp_sigma(float v) { this->interference_.set_orp_sigma(v); }
  void set_interference_ph_jump(float v) { this->interference_.set_ph_jump(v); }
  void set_interference_tw_jump(float v) { this->interference_.set_tw_jump(v); }
  void set_interference_suspected_sensor(InterferenceSuspectedSensor *sensor) {
    this->interference_suspected_sensor_ = sensor;
  }
  void set_interference_coincident_jump_sensor(InterferenceCoincidentJumpSensor *sensor) {
    this->interference_coincident_jump_sensor_ = sensor;
  }
  void set_interference_shared_noise_sensor(InterferenceSharedNoiseSensor *sensor) {
    this->interference_shared_noise_sensor_ = sensor;
  }
  void set_interference_tw_coupling_sensor(InterferenceTwCouplingSensor *sensor) {
    this->interference_tw_coupling_sensor_ = sensor;
  }
  void on_channel_sample(uint8_t channel_type, float value, uint32_t now_ms);

 protected:
  void publish_interference_(uint32_t now_ms);
  uint32_t calibration_interval_{1000};
  bool calibration_mode_active_{false};
  
  // Calibration mode switch
  CalibrationModeSwitch *calibration_mode_switch_{nullptr};

  // External sensor bindings
  sensor::Sensor *water_temp_sensor_{nullptr};

  // Registered channels
  std::map<uint8_t, PoolStationChannelSensor *> channels_;

  // Legacy registered sensors (Lot 0)
  std::map<uint8_t, PoolStationSensor *> sensors_;

  // Calibration UI components (indexed by channel type, then point index)
  std::map<uint8_t, std::map<uint8_t, CalibrationPointXNumber *>> point_x_numbers_;
  std::map<uint8_t, std::map<uint8_t, CalibrationPointYNumber *>> point_y_numbers_;
  std::map<uint8_t, DFRobotMidNumber *> dfrobot_mid_numbers_;
  std::map<uint8_t, DFRobotOffsetNumber *> dfrobot_offset_numbers_;
  std::map<uint8_t, CalibrationInvalidSensor *> cal_invalid_sensors_;
  std::map<uint8_t, CalibrationAlgorithmSelect *> algorithm_selects_;
  std::map<uint8_t, CalibrationPointCountNumber *> point_count_numbers_;
  std::map<uint8_t, DraftPendingSensor *> draft_pending_sensors_;
  std::map<uint8_t, DraftInvalidSensor *> draft_invalid_sensors_;

  // Campaigns (Lot 6)
  std::map<std::string, MeasurementCampaign *> campaigns_;

  // Interference detection (Lot 8)
  bool interference_enabled_{false};
  InterferenceDetector interference_{};
  InterferenceSuspectedSensor *interference_suspected_sensor_{nullptr};
  InterferenceCoincidentJumpSensor *interference_coincident_jump_sensor_{nullptr};
  InterferenceSharedNoiseSensor *interference_shared_noise_sensor_{nullptr};
  InterferenceTwCouplingSensor *interference_tw_coupling_sensor_{nullptr};
};


/**
 * Calibration Mode Switch.
 * 
 * When ON, forces short update intervals on all channels for calibration.
 * When OFF, uses configured intervals.
 */
class CalibrationModeSwitch : public switch_::Switch, public Component {
 public:
  CalibrationModeSwitch() = default;

  void setup() override;
  void dump_config() override;

  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }

 protected:
  void write_state(bool state) override;

  PoolStationComponent *parent_{nullptr};
};


/**
 * Pool Station Channel Sensor.
 * 
 * Wraps an ADS1115 source sensor and provides:
 * - Raw voltage exposure (diagnostic sensor)
 * - Calibrated value via CalibrationEngine
 * - Calibration mode interval switching
 * - Persistence of calibration to flash
 * 
 * Lot 2: Full N-point calibration support.
 */
class PoolStationChannelSensor : public sensor::Sensor, public Component {
 public:
  PoolStationChannelSensor() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA - 1.0f; }

  // Configuration setters
  void set_channel_type(uint8_t type) { this->channel_type_ = static_cast<ChannelType>(type); }
  void set_source_sensor(sensor::Sensor *sensor) { this->source_sensor_ = sensor; }
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_raw_sensor(sensor::Sensor *sensor) { this->raw_sensor_ = sensor; }
  void set_calibrated_sensor(sensor::Sensor *sensor) { this->calibrated_sensor_ = sensor; }
  void set_update_interval(uint32_t interval_ms) { this->configured_interval_ = interval_ms; }

  // Filter configuration (Lot 3)
  void set_filter_samples(uint8_t samples);
  void set_max_jump(float max_jump);
  void set_max_jump_streak(uint8_t streak);
  void set_value_min(float min);
  void set_value_max(float max);

  // Diagnostics configuration (Lot 4)
  void set_diagnostics_enabled(bool enabled);
  void set_noise_window(uint8_t size);
  void set_noise_warn_ptp(float threshold);
  void set_noise_warn_sigma(float threshold);
  void set_stuck_timeout_ms(uint32_t timeout);
  void set_stuck_threshold(float threshold);
  void set_diagnostics_range_min(float min);
  void set_diagnostics_range_max(float max);
  void set_log_rate_limit_ms(uint32_t rate);
  
  // Diagnostic sensor setters (Lot 4)
  void set_diag_mean_sensor(sensor::Sensor *sensor) { this->diag_mean_sensor_ = sensor; }
  void set_diag_sigma_sensor(sensor::Sensor *sensor) { this->diag_sigma_sensor_ = sensor; }
  void set_diag_ptp_sensor(sensor::Sensor *sensor) { this->diag_ptp_sensor_ = sensor; }
  
  // Diagnostic flag setters (Lot 4)
  void set_diag_noisy_flag(DiagnosticNoisyFlag *flag) { this->diag_noisy_flag_ = flag; }
  void set_diag_stuck_flag(DiagnosticStuckFlag *flag) { this->diag_stuck_flag_ = flag; }
  void set_diag_out_of_range_flag(DiagnosticOutOfRangeFlag *flag) { this->diag_out_of_range_flag_ = flag; }
  
  // Diagnostics accessors (Lot 4)
  const DiagnosticsStats &get_diagnostics_stats() const;
  const DiagnosticsFlags &get_diagnostics_flags() const;
  bool is_diagnostics_enabled() const { return this->diagnostics_enabled_; }

  // Temperature compensation configuration (Lot 5)
  void set_temp_compensation_enabled(bool enabled);
  void set_temp_comp_reference_temperature(float temp);
  void set_temp_comp_neutral_ph(float ph);
  void set_temp_comp_orp_coefficient(float coeff);
  void set_temp_compensation_switch(TempCompensationSwitch *sw) { this->temp_comp_switch_ = sw; }
  void set_calibration_temp_sensor(sensor::Sensor *sensor) { this->cal_temp_sensor_ = sensor; }
  
  // Gate configuration (Lot 6)
  void set_gate(MeasurementGate *gate) { this->gate_ = gate; }
  MeasurementGate *get_gate() const { return this->gate_; }
  void set_gate_blocked_sensor(GateBlockedBinarySensor *sensor) { this->gate_blocked_sensor_ = sensor; }
  
  // Check if sampling is currently gated (blocked)
  bool is_gated() const;
  
  // Temperature compensation accessors (Lot 5)
  bool is_temp_compensation_enabled() const { return this->temp_compensator_.is_enabled(); }
  const TemperatureCompensator &get_temp_compensator() const { return this->temp_compensator_; }
  
  // Set temperature compensation enabled at runtime (called by switch)
  void set_temp_compensation_enabled_runtime(bool enabled);
  
  // Publish calibration temperature to diagnostic sensor (Lot 5)
  void publish_calibration_temperature();

  // Calibration configuration
  void set_calibration_type(uint8_t type);
  void set_polynomial_order(uint8_t order);
  void set_calibration_precision(uint8_t decimals);
  void set_dfrobot_mid_mv(float mid);
  void set_dfrobot_offset_mv(float offset);
  void add_calibration_point(float x, float y);
  void set_preferences_key(uint32_t key);
  
  // Lot 7: Draft mode configuration
  void set_draft_mode_enabled(bool enabled);

  // Lot A: runtime ADS overlay (opt-in). Does not own the ADC driver.
  void set_ads1115_sensor(sensor::Sensor *sensor);
  void set_ads_overlay_enabled(bool enabled);
  void set_ads_yaml_gain(uint8_t gain_code) { this->yaml_gain_ = gain_code; }
  void set_saturation_on(float ratio) { this->sat_on_ = ratio; }
  void set_saturation_off(float ratio) { this->sat_off_ = ratio; }
  void set_saturation_hold_ms(uint32_t ms) { this->sat_hold_ms_ = ms; }
  void set_runtime_preferences_key(uint32_t key);
  void set_gain_select(AdsGainSelect *sel) { this->gain_select_ = sel; }
  void set_saturated_sensor(AdsSaturatedBinarySensor *sensor) { this->saturated_sensor_ = sensor; }
  void set_fsr_percent_sensor(sensor::Sensor *sensor) { this->fsr_percent_sensor_ = sensor; }
  void set_gain_mismatch_sensor(AdsGainMismatchSensor *sensor) { this->gain_mismatch_sensor_ = sensor; }

  void apply_gain_runtime(uint8_t gain_code, bool persist = true);
  void reset_ads_to_yaml();
  void load_runtime_preferences();
  // stamp_* : persist current filter/interval into the sidecar. Gain Save /
  // remember_gain must leave unmanaged Lot B sentinels intact (design §4.2).
  void save_runtime_preferences(bool stamp_filters = false, bool stamp_interval = false);
  void remember_gain_at_cal_save();

  // Lot B: filter / interval overlay (opt-in). Outside Capturer draft/Save.
  // Distinct from codegen set_filter_* which do not resize the median window.
  void set_filters_persist(bool persist) { this->filters_persist_ = persist; }
  void set_interval_persist(bool persist) { this->interval_persist_ = persist; }
  void register_filter_runtime_number(FilterRuntimeNumber *num);
  void apply_filter_samples_runtime(uint8_t n, bool persist = true);
  void apply_max_jump_runtime(float j, bool persist = true);
  void apply_max_jump_streak_runtime(uint8_t s, bool persist = true);
  void apply_value_min_runtime(float v, bool persist = true);
  void apply_value_max_runtime(float v, bool persist = true);
  void apply_update_interval_runtime(uint32_t ms, bool persist = true);
  void reset_filters_to_yaml();

  uint8_t get_filter_samples() const { return this->filter_config_.filter_samples; }
  float get_max_jump() const { return this->filter_config_.max_jump; }
  uint8_t get_max_jump_streak() const { return this->filter_config_.max_jump_streak; }
  float get_value_min() const { return this->filter_config_.value_min; }
  float get_value_max() const { return this->filter_config_.value_max; }
  uint32_t get_configured_interval_ms() const { return this->configured_interval_; }

  float get_fsr_volts() const;
  float get_fsr_percent(float raw_v) const;
  bool is_adc_saturated(float raw_v) const;
  bool is_adc_saturated() const { return this->is_adc_saturated(this->last_raw_value_); }
  bool refuse_calibration_save_if_saturated() const;
  uint8_t get_gain_shadow() const { return this->gain_shadow_; }
  float get_saturation_on() const { return this->sat_on_; }
  bool is_ads_overlay_enabled() const { return this->ads_overlay_enabled_; }
  bool is_gain_mismatch() const;

  // Accessors
  ChannelType get_channel_type() const { return this->channel_type_; }
  const char *get_channel_type_name() const;
  float get_raw_value() const { return this->last_raw_value_; }
  
  /**
   * Get the last valid guarded value (for campaign sampling).
   * Returns the most recent successfully processed value, or NAN if none.
   * This is safer than accessing state directly which may be stale.
   */
  float get_last_valid_value() const { return this->last_guarded_value_; }
  
  /**
   * Check if the channel has a valid current value.
   * @return true if last_guarded_value_ is not NaN
   */
  bool has_valid_value() const { return !std::isnan(this->last_guarded_value_); }
  
  // Calibration engine access (for UI components)
  CalibrationEngine *get_calibration_engine() { return &this->calibration_; }

  // Called when calibration is updated (to refresh UI number entities)
  void notify_calibration_updated();

 protected:
  void on_source_value_(float value);
  float apply_filters_(float raw_value);

  ChannelType channel_type_{CHANNEL_TYPE_PRESSURE};
  PoolStationComponent *parent_{nullptr};
  sensor::Sensor *source_sensor_{nullptr};
  sensor::Sensor *raw_sensor_{nullptr};
  sensor::Sensor *calibrated_sensor_{nullptr};
  
  uint32_t configured_interval_{1000};
  uint32_t last_update_{0};
  
  float last_raw_value_{NAN};
  float last_filtered_raw_{NAN};
  float last_calibrated_value_{NAN};
  float last_guarded_value_{NAN};
  
  // Calibration engine
  CalibrationEngine calibration_;
  
  // Filter configuration and state (Lot 3)
  ChannelFilterConfig filter_config_;
  SlidingWindow raw_window_;
  JumpGuard jump_guard_;
  
  // Diagnostics configuration and state (Lot 4)
  bool diagnostics_enabled_{false};
  DiagnosticsConfig diagnostics_config_;
  ChannelDiagnostics diagnostics_;
  
  // Diagnostic sensors (Lot 4, opt-in)
  sensor::Sensor *diag_mean_sensor_{nullptr};
  sensor::Sensor *diag_sigma_sensor_{nullptr};
  sensor::Sensor *diag_ptp_sensor_{nullptr};
  
  // Diagnostic flags (Lot 4, opt-in)
  DiagnosticNoisyFlag *diag_noisy_flag_{nullptr};
  DiagnosticStuckFlag *diag_stuck_flag_{nullptr};
  DiagnosticOutOfRangeFlag *diag_out_of_range_flag_{nullptr};
  
  // Temperature compensation (Lot 5)
  TemperatureCompensator temp_compensator_;
  TempCompensationSwitch *temp_comp_switch_{nullptr};
  sensor::Sensor *cal_temp_sensor_{nullptr};  // Shows Tw at last calibration
  
  // Gate (Lot 6)
  MeasurementGate *gate_{nullptr};
  GateBlockedBinarySensor *gate_blocked_sensor_{nullptr};

  // Lot A: ADS overlay + sidecar NVS (not CalibrationPrefsData)
  void apply_ads_gain_to_source_();
  void ensure_runtime_prefs_obj_();
  void update_saturation_(uint32_t now);
  void expire_saturation_hold_(uint32_t now);
  void publish_ads_ui_();
  void publish_gain_mismatch_();
  void snapshot_yaml_filter_seeds_();
  void sync_median_window_();
  void sync_jump_guard_(bool reset);
  void publish_filter_ui_();
  void persist_filters_if_(bool persist);
  void persist_interval_if_(bool persist);
  void apply_nvs_filter_fields_(const ads_runtime::ChannelRuntimePrefsData &data);

  // NVS deferred flush (v0.10.4): coalesce rapid changes into one save
  void mark_nvs_dirty_(bool stamp_filters, bool stamp_interval);
  void flush_nvs_if_idle_(uint32_t now);
  void flush_nvs_now_();
  void register_shutdown_hook_();

  bool ads_overlay_enabled_{false};
#ifdef USE_ADS1115
  ads1115::ADS1115Sensor *ads_{nullptr};
#endif
  uint8_t yaml_gain_{ads_runtime::GAIN_DEFAULT};
  uint8_t gain_shadow_{ads_runtime::GAIN_DEFAULT};
  uint8_t gain_at_last_cal_save_{ads_runtime::GAIN_UNMANAGED};
  float sat_on_{ads_runtime::SAT_ON_DEFAULT};
  float sat_off_{ads_runtime::SAT_OFF_DEFAULT};
  uint32_t sat_hold_ms_{ads_runtime::SAT_HOLD_MS_DEFAULT};
  bool sat_raw_latched_{false};
  bool sat_published_{false};
  uint32_t sat_last_on_ms_{0};
  uint32_t runtime_prefs_key_{0};
  bool runtime_prefs_obj_ready_{false};
  bool runtime_prefs_loaded_{false};
  ads_runtime::ChannelRuntimePrefsData runtime_prefs_cache_{};
  ESPPreferenceObject runtime_prefs_;
  AdsGainSelect *gain_select_{nullptr};
  AdsSaturatedBinarySensor *saturated_sensor_{nullptr};
  sensor::Sensor *fsr_percent_sensor_{nullptr};
  AdsGainMismatchSensor *gain_mismatch_sensor_{nullptr};

  // Lot B: YAML seeds + HA numbers. NVS sidecar is the Lot A blob.
  bool filters_persist_{false};
  bool interval_persist_{false};
  uint8_t yaml_filter_samples_{0};
  uint8_t yaml_max_jump_streak_{3};
  float yaml_max_jump_{0.0f};
  float yaml_value_min_{NAN};
  float yaml_value_max_{NAN};
  uint32_t yaml_update_interval_ms_{1000};
  std::vector<FilterRuntimeNumber *> filter_runtime_numbers_{};

  // NVS deferred flush state (v0.10.4): coalesce rapid HA changes
  bool nvs_dirty_{false};
  uint32_t nvs_dirty_since_ms_{0};
  bool nvs_pending_stamp_filters_{false};
  bool nvs_pending_stamp_interval_{false};
  bool nvs_shutdown_hook_registered_{false};
};


/**
 * Legacy Pool Station Sensor (Lot 0 compatibility).
 * 
 * A sensor that is managed by PoolStationComponent.
 * Supports calibration, filtering, and diagnostics (stubs in Lot 0/1).
 */
class PoolStationSensor : public sensor::Sensor, public Component {
 public:
  PoolStationSensor() = default;

  void setup() override;
  void dump_config() override;

  // Configuration setters
  void set_role(uint8_t role) { this->role_ = static_cast<SensorRole>(role); }
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }

  // Accessors
  SensorRole get_role() const { return this->role_; }
  const char *get_role_name() const;

  // Called by parent component to publish value
  void publish_value(float value);

 protected:
  SensorRole role_{ROLE_WATER_TEMPERATURE};
  PoolStationComponent *parent_{nullptr};
  
  float last_raw_value_{NAN};
  float last_calibrated_value_{NAN};
};

/**
 * Diagnostic flag binary sensors (Lot 4).
 * Expose diagnostic flags as Home Assistant binary_sensor entities.
 */
class DiagnosticNoisyFlag : public binary_sensor::BinarySensor, public Component {
 public:
  DiagnosticNoisyFlag() = default;

  void setup() override;
  void dump_config() override;

  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

class DiagnosticStuckFlag : public binary_sensor::BinarySensor, public Component {
 public:
  DiagnosticStuckFlag() = default;

  void setup() override;
  void dump_config() override;

  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

class DiagnosticOutOfRangeFlag : public binary_sensor::BinarySensor, public Component {
 public:
  DiagnosticOutOfRangeFlag() = default;

  void setup() override;
  void dump_config() override;

  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

/**
 * Temperature Compensation Switch (Lot 5).
 * 
 * Runtime enable/disable for per-channel temperature compensation.
 * When ON, calibrated values are temperature-corrected using water Tw.
 * When OFF, raw calibrated values are used (no correction).
 */
class TempCompensationSwitch : public switch_::Switch, public Component {
 public:
  TempCompensationSwitch() = default;

  void setup() override;
  void dump_config() override;

  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  void write_state(bool state) override;

  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

}  // namespace pool_station
}  // namespace esphome
