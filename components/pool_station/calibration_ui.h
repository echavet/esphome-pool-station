#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
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

  void update_from_calibration();

 protected:
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  bool last_state_{false};
  uint32_t last_check_{0};
};

// ============================================================================
// Lot 7: Runtime Algorithm Select
// ============================================================================

/**
 * Select entity for runtime calibration algorithm selection.
 * Allows changing the calibration type from Home Assistant.
 * Changing algorithm triggers validation of existing points.
 * Options are per-channel (dfrobot_orp is ORP-only); must match
 * algorithm_select_options() in Python codegen.
 */
class CalibrationAlgorithmSelect : public select::Select, public Component {
 public:
  CalibrationAlgorithmSelect() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  
  // Update select state from engine
  void update_from_calibration();

 protected:
  void control(const std::string &value) override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  
  // Maps option strings to CalibrationType values
  static CalibrationType string_to_type(const std::string &str);
  static std::string type_to_string(CalibrationType type);
};

// ============================================================================
// Lot 7: Add/Remove Point Buttons
// ============================================================================

/**
 * Button to add a new calibration point.
 * Adds a point with default values that can then be edited via numbers.
 */
class CalibrationAddPointButton : public button::Button, public Component {
 public:
  CalibrationAddPointButton() = default;
  
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
 * Button to remove the last calibration point.
 * Removes the highest-indexed point from the current set.
 */
class CalibrationRemovePointButton : public button::Button, public Component {
 public:
  CalibrationRemovePointButton() = default;
  
  void setup() override {}
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

 protected:
  void press_action() override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

// ============================================================================
// Lot 7: Draft/Commit Workflow Buttons
// ============================================================================

/**
 * Button to commit draft calibration changes to live.
 * Also saves to preferences.
 */
class CalibrationCommitButton : public button::Button, public Component {
 public:
  CalibrationCommitButton() = default;
  
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
 * Button to discard draft calibration changes.
 * Reverts to the live/committed calibration.
 */
class CalibrationDiscardButton : public button::Button, public Component {
 public:
  CalibrationDiscardButton() = default;
  
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
 * Number entity to control the number of calibration points.
 * Resizing adds/removes points as needed.
 */
class CalibrationPointCountNumber : public number::Number, public Component {
 public:
  CalibrationPointCountNumber() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  
  // Update from calibration engine
  void update_from_calibration();

 protected:
  void control(float value) override;
  
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
};

/**
 * Binary sensor indicating draft has uncommitted changes.
 * ON when there are pending draft changes that haven't been committed.
 */
class DraftPendingSensor : public binary_sensor::BinarySensor, public Component {
 public:
  DraftPendingSensor() = default;
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }

  void update_from_calibration();

 protected:
  PoolStationComponent *parent_{nullptr};
  uint8_t channel_type_{0};
  bool last_state_{false};
  uint32_t last_check_{0};
};

}  // namespace pool_station
}  // namespace esphome
