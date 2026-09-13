#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "pool_station.h"
#include "calibration_engine.h"

namespace esphome {
namespace pool_station {

// Forward declaration
class PoolStationComponent;
class PoolStationChannelSensor;

/**
 * Number entity for calibration point X value (raw voltage).
 * Allows editing the raw voltage value of a calibration point from HA.
 */
class CalibrationPointXNumber : public number::Number, public Component {
 public:
  CalibrationPointXNumber() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  void set_point_index(uint8_t index) { this->point_index_ = index; }
  
  // Called when channel sensor updates its calibration
  void update_from_calibration();

 protected:
  void control(float value) override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  uint8_t point_index_{0};
};

/**
 * Number entity for calibration point Y value (calibrated value).
 * Allows editing the calibrated value of a calibration point from HA.
 */
class CalibrationPointYNumber : public number::Number, public Component {
 public:
  CalibrationPointYNumber() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  void set_point_index(uint8_t index) { this->point_index_ = index; }
  
  // Called when channel sensor updates its calibration
  void update_from_calibration();

 protected:
  void control(float value) override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  uint8_t point_index_{0};
};

/**
 * Number entity for DFRobot ORP mid_mv parameter.
 * Allows runtime adjustment of the midpoint reference voltage.
 */
class DFRobotMidNumber : public number::Number, public Component {
 public:
  DFRobotMidNumber() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  
  void update_from_calibration();

 protected:
  void control(float value) override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

/**
 * Number entity for DFRobot ORP offset_mv parameter.
 * Allows runtime adjustment of the calibration offset.
 */
class DFRobotOffsetNumber : public number::Number, public Component {
 public:
  DFRobotOffsetNumber() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  
  void update_from_calibration();

 protected:
  void control(float value) override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

/**
 * Button to capture current raw voltage into calibration point X.
 * Press to copy the current ADC reading into the specified calibration point.
 */
class CalibrationCaptureButton : public button::Button, public Component {
 public:
  CalibrationCaptureButton() = default;
  
  void setup() override {}
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  void set_point_index(uint8_t index) { this->point_index_ = index; }

 protected:
  void press_action() override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  uint8_t point_index_{0};
};

/**
 * Button to save calibration data to flash.
 * Persists current calibration points and parameters to preferences.
 */
class CalibrationSaveButton : public button::Button, public Component {
 public:
  CalibrationSaveButton() = default;
  
  void setup() override {}
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  void press_action() override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

/**
 * Binary sensor indicating calibration is invalid.
 * ON when calibration has insufficient points for selected algorithm.
 */
class CalibrationInvalidSensor : public binary_sensor::BinarySensor, public Component {
 public:
  CalibrationInvalidSensor() = default;
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  bool last_state_{false};
  uint32_t last_check_{0};
};

}  // namespace pool_station
}  // namespace esphome
