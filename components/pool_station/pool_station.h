#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "calibration_engine.h"
#include "channel_filter.h"
#include <vector>
#include <map>
#include <functional>

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station";

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

 protected:
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

  // Calibration configuration
  void set_calibration_type(uint8_t type);
  void set_polynomial_order(uint8_t order);
  void set_calibration_precision(uint8_t decimals);
  void set_dfrobot_mid_mv(float mid);
  void set_dfrobot_offset_mv(float offset);
  void add_calibration_point(float x, float y);
  void set_preferences_key(uint32_t key);

  // Accessors
  ChannelType get_channel_type() const { return this->channel_type_; }
  const char *get_channel_type_name() const;
  float get_raw_value() const { return this->last_raw_value_; }
  
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
  
  // Callback ID for source sensor subscription
  optional<CallbackManager<void(float)>::CancelToken> source_callback_;
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

}  // namespace pool_station
}  // namespace esphome
