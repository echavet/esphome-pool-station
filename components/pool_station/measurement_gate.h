#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include <string>
#include <vector>

namespace esphome {
namespace pool_station {

static const char *const GATE_TAG = "pool_station.gate";

/**
 * Gate condition type enumeration.
 */
enum GateConditionType : uint8_t {
  GATE_COND_BINARY_SENSOR = 0,
  GATE_COND_SWITCH = 1,
  GATE_COND_SENSOR_THRESHOLD = 2,
};

/**
 * Threshold comparison operator.
 */
enum ThresholdOperator : uint8_t {
  THRESHOLD_OP_GT = 0,   // Greater than
  THRESHOLD_OP_GTE = 1,  // Greater than or equal
  THRESHOLD_OP_LT = 2,   // Less than
  THRESHOLD_OP_LTE = 3,  // Less than or equal
  THRESHOLD_OP_EQ = 4,   // Equal (with tolerance)
};

/**
 * Configuration for a single gate condition.
 * 
 * Examples:
 *   - binary_sensor: flow_switch, desired_state: true  (sample when flow detected)
 *   - switch: filtration_relay, desired_state: false   (sample when filtration OFF)
 *   - sensor: pressure, operator: GTE, threshold: 0.5  (sample when pressure >= 0.5 bar)
 */
struct GateCondition {
  GateConditionType type{GATE_COND_BINARY_SENSOR};
  
  // For binary_sensor and switch conditions
  binary_sensor::BinarySensor *binary_sensor_ref{nullptr};
  switch_::Switch *switch_ref{nullptr};
  bool desired_state{true};
  
  // For sensor threshold conditions
  sensor::Sensor *sensor_ref{nullptr};
  ThresholdOperator threshold_op{THRESHOLD_OP_GT};
  float threshold_value{0.0f};
  float threshold_tolerance{0.001f};  // For equality comparison
  
  // Condition name for logging
  std::string name;
  
  /**
   * Evaluate this condition.
   * @return true if condition is satisfied (gate should open)
   */
  bool evaluate() const;
  
  /**
   * Get human-readable description of the condition.
   */
  std::string describe() const;
};

/**
 * Configuration for a measurement gate.
 * 
 * A gate can have multiple conditions combined with AND logic.
 * All conditions must be satisfied for the gate to be open.
 */
struct GateConfig {
  std::string name;
  std::vector<GateCondition> conditions;
  bool invert{false};  // If true, gate is open when conditions are NOT met
  
  /**
   * Check if this gate configuration is valid.
   */
  bool is_valid() const { return !conditions.empty(); }
};

/**
 * MeasurementGate - Controls sampling based on external conditions.
 * 
 * Gates block sampling/publishing unless all configured conditions are met.
 * Use cases:
 *   - Sample pH only when flow is detected (flow_switch binary_sensor)
 *   - Sample pressure only when filtration is ON (switch state)
 *   - Block sampling when pressure is below threshold (sensor threshold)
 * 
 * The gate exposes an optional `gate_blocked` binary_sensor to Home Assistant.
 */
class MeasurementGate {
 public:
  MeasurementGate() = default;
  
  /**
   * Configure the gate.
   */
  void set_config(const GateConfig &config) { this->config_ = config; }
  const GateConfig &get_config() const { return this->config_; }
  
  /**
   * Set the name for this gate.
   */
  void set_name(const std::string &name) { this->config_.name = name; }
  const std::string &get_name() const { return this->config_.name; }
  
  /**
   * Add a condition to this gate.
   */
  void add_condition(const GateCondition &condition);
  
  /**
   * Check if the gate is open (all conditions satisfied).
   * @return true if sampling is allowed
   */
  bool is_open() const;
  
  /**
   * Check if the gate is blocked (sampling not allowed).
   * @return true if sampling is blocked
   */
  bool is_blocked() const { return !this->is_open(); }
  
  /**
   * Get the first failing condition (for diagnostics).
   * @return nullptr if gate is open, otherwise pointer to first failing condition
   */
  const GateCondition *get_blocking_condition() const;
  
  /**
   * Get human-readable gate status for logging.
   */
  std::string get_status_string() const;
  
  /**
   * Enable/disable the gate entirely.
   */
  void set_enabled(bool enabled) { this->enabled_ = enabled; }
  bool is_enabled() const { return this->enabled_; }
  
  /**
   * Dump configuration to log.
   */
  void dump_config() const;

 protected:
  GateConfig config_;
  bool enabled_{true};
};

/**
 * GateBlockedBinarySensor - Exposes gate_blocked state to Home Assistant.
 * 
 * ON = gate is blocked (sampling not allowed)
 * OFF = gate is open (sampling allowed)
 */
class GateBlockedBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  GateBlockedBinarySensor() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_gate(MeasurementGate *gate) { this->gate_ = gate; }
  
  /**
   * Update the sensor state based on gate status.
   */
  void update_state();

 protected:
  MeasurementGate *gate_{nullptr};
};

/**
 * Namespace for pure gate evaluation functions (unit-testable).
 */
namespace gate_functions {

/**
 * Evaluate a binary sensor condition.
 */
bool evaluate_binary_sensor(binary_sensor::BinarySensor *sensor, bool desired_state);

/**
 * Evaluate a switch condition.
 */
bool evaluate_switch(switch_::Switch *sw, bool desired_state);

/**
 * Evaluate a sensor threshold condition.
 */
bool evaluate_threshold(sensor::Sensor *sensor, ThresholdOperator op, 
                       float threshold, float tolerance);

/**
 * Get string representation of threshold operator.
 */
const char *threshold_op_to_string(ThresholdOperator op);

}  // namespace gate_functions

}  // namespace pool_station
}  // namespace esphome
