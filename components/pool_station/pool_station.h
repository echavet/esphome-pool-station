#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include <vector>
#include <map>

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station";

/**
 * Sensor role identifiers.
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

/**
 * Calibration algorithm modes.
 * Lot 0: stub only, actual implementation in Lot 2.
 */
enum CalibrationMode : uint8_t {
  CAL_MODE_NONE = 0,
  CAL_MODE_LINEAR = 1,
  CAL_MODE_POLYNOMIAL = 2,
  CAL_MODE_EXPONENTIAL = 3,
  CAL_MODE_LOGARITHMIC = 4,
  CAL_MODE_POWER = 5,
  CAL_MODE_PIECEWISE = 6,
  CAL_MODE_DFROBOT_ORP = 7,  // SEN0165-like mid/offset
};

// Forward declaration
class PoolStationSensor;

/**
 * Main pool_station component.
 * 
 * Orchestrates sensor readings, calibration, and diagnostics.
 * Lot 0: skeleton with stub functionality.
 * 
 * Design principles:
 * - Composes ESPHome ads1115/dallas/gpio — does NOT reimplement drivers
 * - Rich N-point calibration with multiple algorithm support
 * - Diagnostics: noise, jumps, drift detection
 * - Gated/campaign sampling for accurate measurements
 * - Water temperature compensation for pH/ORP
 * - Stable entity IDs (anti-swap, see SENSOR-IDENTITY.md)
 */
class PoolStationComponent : public Component {
 public:
  PoolStationComponent() = default;

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  // Configuration setters (called by codegen)
  void set_update_interval(uint32_t interval_ms) { this->update_interval_ = interval_ms; }
  void set_calibration_mode(uint8_t mode) { this->calibration_mode_ = static_cast<CalibrationMode>(mode); }
  void set_water_temperature_sensor(sensor::Sensor *sensor) { this->water_temp_sensor_ = sensor; }

  // Sensor registration
  void register_sensor(PoolStationSensor *sensor, uint8_t role);

  // Accessors for sensors
  float get_water_temperature() const;
  bool has_water_temperature() const { return this->water_temp_sensor_ != nullptr; }

  // Calibration stubs (Lot 2+)
  // TODO: float apply_calibration(float raw_value, SensorRole role);
  // TODO: void start_calibration_capture(SensorRole role, float reference_value);
  // TODO: void commit_calibration(SensorRole role);

  // Diagnostics stubs (Lot 4+)
  // TODO: DiagnosticFlags get_diagnostics(SensorRole role);

  // Gate/Campaign stubs (Lot 6+)
  // TODO: void start_measurement_campaign(CampaignConfig config);

 protected:
  uint32_t update_interval_{1000};
  uint32_t last_update_{0};
  CalibrationMode calibration_mode_{CAL_MODE_NONE};

  // External sensor bindings (composes ESPHome sensors)
  sensor::Sensor *water_temp_sensor_{nullptr};

  // Registered pool_station sensors by role
  std::map<uint8_t, PoolStationSensor *> sensors_;

  // Stub: simulate sensor update cycle
  void update_sensors_();
};

/**
 * Pool Station Sensor.
 * 
 * A sensor that is managed by PoolStationComponent.
 * Supports calibration, filtering, and diagnostics (stubs in Lot 0).
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

  // Calibration state (Lot 2+)
  // TODO: bool is_calibration_valid() const;
  // TODO: float get_raw_value() const;
  // TODO: float get_calibrated_value() const;

 protected:
  SensorRole role_{ROLE_WATER_TEMPERATURE};
  PoolStationComponent *parent_{nullptr};
  
  // Last raw/calibrated values (for diagnostics)
  float last_raw_value_{NAN};
  float last_calibrated_value_{NAN};
};

}  // namespace pool_station
}  // namespace esphome
