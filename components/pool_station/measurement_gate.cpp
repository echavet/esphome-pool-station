#include "measurement_gate.h"
#include <cmath>

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station.gate";  // Required by LOG_* macros

// ============================================================================
// Gate Condition
// ============================================================================

bool GateCondition::evaluate() const {
  switch (this->type) {
    case GATE_COND_BINARY_SENSOR:
      return gate_functions::evaluate_binary_sensor(this->binary_sensor_ref, this->desired_state);
    
    case GATE_COND_SWITCH:
      return gate_functions::evaluate_switch(this->switch_ref, this->desired_state);
    
    case GATE_COND_SENSOR_THRESHOLD:
      return gate_functions::evaluate_threshold(
          this->sensor_ref, this->threshold_op, 
          this->threshold_value, this->threshold_tolerance);
    
    default:
      return true;  // Unknown type, allow sampling
  }
}

std::string GateCondition::describe() const {
  std::string desc = this->name.empty() ? "condition" : this->name;
  
  switch (this->type) {
    case GATE_COND_BINARY_SENSOR:
      desc += " (binary_sensor ";
      desc += this->desired_state ? "ON" : "OFF";
      desc += ")";
      break;
    
    case GATE_COND_SWITCH:
      desc += " (switch ";
      desc += this->desired_state ? "ON" : "OFF";
      desc += ")";
      break;
    
    case GATE_COND_SENSOR_THRESHOLD:
      desc += " (sensor ";
      desc += gate_functions::threshold_op_to_string(this->threshold_op);
      desc += " ";
      char buf[32];
      snprintf(buf, sizeof(buf), "%.2f", this->threshold_value);
      desc += buf;
      desc += ")";
      break;
  }
  
  return desc;
}

// ============================================================================
// MeasurementGate
// ============================================================================

void MeasurementGate::add_condition(const GateCondition &condition) {
  this->config_.conditions.push_back(condition);
}

void MeasurementGate::add_binary_sensor_condition(binary_sensor::BinarySensor *sensor, bool desired_state, const std::string &name) {
  GateCondition cond;
  cond.type = GATE_COND_BINARY_SENSOR;
  cond.binary_sensor_ref = sensor;
  cond.desired_state = desired_state;
  cond.name = name;
  this->config_.conditions.push_back(cond);
}

void MeasurementGate::add_switch_condition(switch_::Switch *sw, bool desired_state, const std::string &name) {
  GateCondition cond;
  cond.type = GATE_COND_SWITCH;
  cond.switch_ref = sw;
  cond.desired_state = desired_state;
  cond.name = name;
  this->config_.conditions.push_back(cond);
}

void MeasurementGate::add_sensor_threshold_condition(sensor::Sensor *sensor, uint8_t op, float threshold, float tolerance, const std::string &name) {
  GateCondition cond;
  cond.type = GATE_COND_SENSOR_THRESHOLD;
  cond.sensor_ref = sensor;
  cond.threshold_op = static_cast<ThresholdOperator>(op);
  cond.threshold_value = threshold;
  cond.threshold_tolerance = tolerance;
  cond.name = name;
  this->config_.conditions.push_back(cond);
}

bool MeasurementGate::is_open() const {
  if (!this->enabled_) {
    return true;  // Gate disabled = always open
  }
  
  if (this->config_.conditions.empty()) {
    return true;  // No conditions = always open
  }
  
  // All conditions must be satisfied (AND logic)
  for (const auto &cond : this->config_.conditions) {
    if (!cond.evaluate()) {
      // Condition not met
      return this->config_.invert;  // If inverted, blocked = open
    }
  }
  
  // All conditions met
  return !this->config_.invert;  // If inverted, open = blocked
}

const GateCondition *MeasurementGate::get_blocking_condition() const {
  if (!this->enabled_) {
    return nullptr;
  }
  
  for (const auto &cond : this->config_.conditions) {
    bool result = cond.evaluate();
    if (this->config_.invert) {
      // Inverted: blocking when condition IS met
      if (result) {
        return &cond;
      }
    } else {
      // Normal: blocking when condition is NOT met
      if (!result) {
        return &cond;
      }
    }
  }
  
  return nullptr;
}

std::string MeasurementGate::get_status_string() const {
  if (!this->enabled_) {
    return "disabled (always open)";
  }
  
  if (this->is_open()) {
    return "OPEN (sampling allowed)";
  }
  
  const GateCondition *blocker = this->get_blocking_condition();
  if (blocker != nullptr) {
    return "BLOCKED by " + blocker->describe();
  }
  
  return "BLOCKED";
}

void MeasurementGate::dump_config() const {
  ESP_LOGCONFIG(GATE_TAG, "  Gate: %s", 
                this->config_.name.empty() ? "(unnamed)" : this->config_.name.c_str());
  ESP_LOGCONFIG(GATE_TAG, "    Enabled: %s", this->enabled_ ? "YES" : "NO");
  ESP_LOGCONFIG(GATE_TAG, "    Invert: %s", this->config_.invert ? "YES" : "NO");
  ESP_LOGCONFIG(GATE_TAG, "    Conditions: %zu", this->config_.conditions.size());
  
  for (size_t i = 0; i < this->config_.conditions.size(); i++) {
    const auto &cond = this->config_.conditions[i];
    ESP_LOGCONFIG(GATE_TAG, "      [%zu] %s", i, cond.describe().c_str());
  }
}

// ============================================================================
// GateBlockedBinarySensor
// ============================================================================

void GateBlockedBinarySensor::setup() {
  ESP_LOGD(GATE_TAG, "Setting up Gate Blocked Binary Sensor");
  this->publish_state(false);  // Default: not blocked
}

void GateBlockedBinarySensor::dump_config() {
  LOG_BINARY_SENSOR("", "Gate Blocked", this);
}

void GateBlockedBinarySensor::update_state() {
  if (this->gate_ == nullptr) {
    return;
  }
  
  bool blocked = this->gate_->is_blocked();
  if (this->state != blocked) {
    ESP_LOGD(GATE_TAG, "Gate blocked state changed: %s → %s",
             this->state ? "BLOCKED" : "OPEN",
             blocked ? "BLOCKED" : "OPEN");
    this->publish_state(blocked);
  }
}

// ============================================================================
// Pure Gate Functions
// ============================================================================

namespace gate_functions {

bool evaluate_binary_sensor(binary_sensor::BinarySensor *sensor, bool desired_state) {
  if (sensor == nullptr) {
    ESP_LOGW(GATE_TAG, "Gate condition has null binary_sensor reference");
    return true;  // No sensor = condition satisfied (fail-open)
  }
  
  // Check if sensor has a valid state
  if (!sensor->has_state()) {
    ESP_LOGV(GATE_TAG, "Binary sensor %s has no state yet", 
             sensor->get_name().c_str());
    return false;  // No state yet = condition not satisfied (fail-closed)
  }
  
  return sensor->state == desired_state;
}

bool evaluate_switch(switch_::Switch *sw, bool desired_state) {
  if (sw == nullptr) {
    ESP_LOGW(GATE_TAG, "Gate condition has null switch reference");
    return true;  // No switch = condition satisfied (fail-open)
  }
  
  return sw->state == desired_state;
}

bool evaluate_threshold(sensor::Sensor *sensor, ThresholdOperator op, 
                       float threshold, float tolerance) {
  if (sensor == nullptr) {
    ESP_LOGW(GATE_TAG, "Gate condition has null sensor reference");
    return true;  // No sensor = condition satisfied (fail-open)
  }
  
  // Check if sensor has a valid state
  if (!sensor->has_state() || std::isnan(sensor->state)) {
    ESP_LOGV(GATE_TAG, "Sensor %s has no valid state", 
             sensor->get_name().c_str());
    return false;  // No state = condition not satisfied (fail-closed)
  }
  
  float value = sensor->state;
  
  switch (op) {
    case THRESHOLD_OP_GT:
      return value > threshold;
    case THRESHOLD_OP_GTE:
      return value >= threshold;
    case THRESHOLD_OP_LT:
      return value < threshold;
    case THRESHOLD_OP_LTE:
      return value <= threshold;
    case THRESHOLD_OP_EQ:
      return std::abs(value - threshold) <= tolerance;
    default:
      return true;
  }
}

const char *threshold_op_to_string(ThresholdOperator op) {
  switch (op) {
    case THRESHOLD_OP_GT: return ">";
    case THRESHOLD_OP_GTE: return ">=";
    case THRESHOLD_OP_LT: return "<";
    case THRESHOLD_OP_LTE: return "<=";
    case THRESHOLD_OP_EQ: return "==";
    default: return "?";
  }
}

}  // namespace gate_functions

}  // namespace pool_station
}  // namespace esphome
