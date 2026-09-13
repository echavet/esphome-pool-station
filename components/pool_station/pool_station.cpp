#include "pool_station.h"
#include "calibration_ui.h"

namespace esphome {
namespace pool_station {

// ============================================================================
// PoolStationComponent
// ============================================================================

void PoolStationComponent::setup() {
  ESP_LOGI(TAG, "Setting up Pool Station component...");
  ESP_LOGI(TAG, "  Update interval: %u ms", this->get_update_interval());
  ESP_LOGI(TAG, "  Calibration interval: %u ms", this->calibration_interval_);
  
  if (this->calibration_mode_switch_ != nullptr) {
    ESP_LOGI(TAG, "  Calibration mode switch: configured");
  }
  
  if (this->water_temp_sensor_ != nullptr) {
    ESP_LOGI(TAG, "  Water temperature sensor: bound");
  }

  ESP_LOGI(TAG, "  Registered channels: %zu", this->channels_.size());
  for (const auto &pair : this->channels_) {
    PoolStationChannelSensor *channel = pair.second;
    if (channel != nullptr) {
      ESP_LOGI(TAG, "    - %s (type %d)", channel->get_channel_type_name(), pair.first);
    }
  }

  ESP_LOGI(TAG, "  Legacy sensors: %zu", this->sensors_.size());
  
  ESP_LOGI(TAG, "Pool Station setup complete.");
}

void PoolStationComponent::update() {
  ESP_LOGV(TAG, "Update cycle - calibration mode: %s", 
           this->calibration_mode_active_ ? "ON" : "OFF");

  if (this->water_temp_sensor_ != nullptr) {
    float water_temp = this->get_water_temperature();
    if (!std::isnan(water_temp)) {
      ESP_LOGV(TAG, "  Water temperature: %.2f C", water_temp);
    }
  }
}

void PoolStationComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pool Station Component:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->get_update_interval());
  ESP_LOGCONFIG(TAG, "  Calibration interval: %u ms", this->calibration_interval_);
  ESP_LOGCONFIG(TAG, "  Calibration mode: %s", this->calibration_mode_active_ ? "ON" : "OFF");
  
  if (this->calibration_mode_switch_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Calibration mode switch: configured");
  }
  
  if (this->water_temp_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Water temperature sensor: configured");
  }
  
  ESP_LOGCONFIG(TAG, "  Channels:");
  for (const auto &pair : this->channels_) {
    PoolStationChannelSensor *channel = pair.second;
    if (channel != nullptr) {
      ESP_LOGCONFIG(TAG, "    - %s", channel->get_channel_type_name());
    }
  }
}

void PoolStationComponent::set_calibration_mode_switch(CalibrationModeSwitch *sw) {
  this->calibration_mode_switch_ = sw;
}

void PoolStationComponent::register_channel(PoolStationChannelSensor *channel, uint8_t type) {
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Attempted to register null channel for type %d", type);
    return;
  }
  
  if (this->channels_.count(type) > 0) {
    ESP_LOGW(TAG, "Channel type %d already registered, replacing", type);
  }
  
  this->channels_[type] = channel;
  ESP_LOGD(TAG, "Registered channel for type %d: %s", type, channel->get_channel_type_name());
}

void PoolStationComponent::register_sensor(PoolStationSensor *sensor, uint8_t role) {
  if (sensor == nullptr) {
    ESP_LOGW(TAG, "Attempted to register null sensor for role %d", role);
    return;
  }
  
  if (this->sensors_.count(role) > 0) {
    ESP_LOGW(TAG, "Sensor role %d already registered, replacing", role);
  }
  
  this->sensors_[role] = sensor;
  ESP_LOGD(TAG, "Registered sensor for role %d: %s", role, sensor->get_role_name());
}

float PoolStationComponent::get_water_temperature() const {
  if (this->water_temp_sensor_ == nullptr) {
    return NAN;
  }
  return this->water_temp_sensor_->state;
}

void PoolStationComponent::set_calibration_mode_active(bool active) {
  if (this->calibration_mode_active_ != active) {
    this->calibration_mode_active_ = active;
    ESP_LOGI(TAG, "Calibration mode %s", active ? "ENABLED" : "DISABLED");
  }
}

PoolStationChannelSensor *PoolStationComponent::get_channel(uint8_t type) {
  auto it = this->channels_.find(type);
  if (it != this->channels_.end()) {
    return it->second;
  }
  return nullptr;
}

void PoolStationComponent::register_point_x_number(CalibrationPointXNumber *num, uint8_t channel_type, uint8_t point_index) {
  if (num == nullptr) return;
  this->point_x_numbers_[channel_type][point_index] = num;
}

void PoolStationComponent::register_point_y_number(CalibrationPointYNumber *num, uint8_t channel_type, uint8_t point_index) {
  if (num == nullptr) return;
  this->point_y_numbers_[channel_type][point_index] = num;
}

void PoolStationComponent::register_dfrobot_mid_number(DFRobotMidNumber *num, uint8_t channel_type) {
  if (num == nullptr) return;
  this->dfrobot_mid_numbers_[channel_type] = num;
}

void PoolStationComponent::register_dfrobot_offset_number(DFRobotOffsetNumber *num, uint8_t channel_type) {
  if (num == nullptr) return;
  this->dfrobot_offset_numbers_[channel_type] = num;
}

void PoolStationComponent::register_cal_invalid_sensor(CalibrationInvalidSensor *sensor, uint8_t channel_type) {
  if (sensor == nullptr) return;
  this->cal_invalid_sensors_[channel_type] = sensor;
}


// ============================================================================
// CalibrationModeSwitch
// ============================================================================

void CalibrationModeSwitch::setup() {
  ESP_LOGD(TAG, "Setting up Calibration Mode Switch");
  
  bool initial_state = false;
  this->publish_state(initial_state);
  
  if (this->parent_ != nullptr) {
    this->parent_->set_calibration_mode_active(initial_state);
  }
}

void CalibrationModeSwitch::dump_config() {
  LOG_SWITCH("", "Calibration Mode Switch", this);
}

void CalibrationModeSwitch::write_state(bool state) {
  this->publish_state(state);
  
  if (this->parent_ != nullptr) {
    this->parent_->set_calibration_mode_active(state);
  }
  
  ESP_LOGI(TAG, "Calibration mode switched %s", state ? "ON" : "OFF");
}


// ============================================================================
// PoolStationChannelSensor
// ============================================================================

void PoolStationChannelSensor::setup() {
  ESP_LOGD(TAG, "Setting up Pool Station Channel: %s", this->get_channel_type_name());
  
  if (this->source_sensor_ == nullptr) {
    ESP_LOGE(TAG, "Channel %s has no source sensor configured!", this->get_channel_type_name());
    this->mark_failed();
    return;
  }
  
  // Load calibration from preferences (if available)
  this->calibration_.load_from_preferences();
  
  // Subscribe to source sensor
  this->source_sensor_->add_on_state_callback([this](float value) {
    this->on_source_value_(value);
  });
  
  ESP_LOGD(TAG, "Channel %s subscribed to source sensor", this->get_channel_type_name());
  
  // Log calibration status
  if (this->calibration_.get_type() != CAL_TYPE_NONE) {
    ESP_LOGI(TAG, "Channel %s calibration: type=%s, valid=%s, points=%zu",
             this->get_channel_type_name(),
             this->calibration_.get_type_name(),
             this->calibration_.is_valid() ? "YES" : "NO",
             this->calibration_.get_point_count());
  }
}

void PoolStationChannelSensor::loop() {
}

void PoolStationChannelSensor::dump_config() {
  LOG_SENSOR("", "Pool Station Channel", this);
  ESP_LOGCONFIG(TAG, "  Type: %s", this->get_channel_type_name());
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->configured_interval_);
  if (this->raw_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Raw sensor: configured");
  }
  
  // Dump calibration config
  this->calibration_.dump_config();
}

const char *PoolStationChannelSensor::get_channel_type_name() const {
  switch (this->channel_type_) {
    case CHANNEL_TYPE_PRESSURE: return "pressure";
    case CHANNEL_TYPE_PH: return "ph";
    case CHANNEL_TYPE_ORP: return "orp";
    default: return "unknown";
  }
}

void PoolStationChannelSensor::set_calibration_type(uint8_t type) {
  this->calibration_.set_type(static_cast<CalibrationType>(type));
}

void PoolStationChannelSensor::set_polynomial_order(uint8_t order) {
  this->calibration_.set_polynomial_order(order);
}

void PoolStationChannelSensor::set_calibration_precision(uint8_t decimals) {
  this->calibration_.set_precision(decimals);
}

void PoolStationChannelSensor::set_dfrobot_mid_mv(float mid) {
  this->calibration_.set_dfrobot_mid_mv(mid);
}

void PoolStationChannelSensor::set_dfrobot_offset_mv(float offset) {
  this->calibration_.set_dfrobot_offset_mv(offset);
}

void PoolStationChannelSensor::add_calibration_point(float x, float y) {
  this->calibration_.add_point(x, y);
}

void PoolStationChannelSensor::set_preferences_key(uint32_t key) {
  this->calibration_.set_preferences_key(key);
}

void PoolStationChannelSensor::notify_calibration_updated() {
  // This is called after a capture button press or manual point edit
  // We could trigger number entity updates here if needed
  ESP_LOGD(TAG, "Calibration updated for channel %s", this->get_channel_type_name());
}

void PoolStationChannelSensor::on_source_value_(float value) {
  uint32_t now = millis();
  
  uint32_t effective_interval = this->configured_interval_;
  if (this->parent_ != nullptr && this->parent_->is_calibration_mode()) {
    effective_interval = this->parent_->get_calibration_interval();
  }
  
  if (now - this->last_update_ < effective_interval) {
    return;
  }
  this->last_update_ = now;
  
  this->last_raw_value_ = value;
  
  // Publish raw value to diagnostic sensor
  if (this->raw_sensor_ != nullptr) {
    this->raw_sensor_->publish_state(value);
  }
  
  // Apply calibration
  float calibrated;
  if (this->calibration_.get_type() != CAL_TYPE_NONE && this->calibration_.is_valid()) {
    calibrated = this->calibration_.calibrate(value);
  } else {
    // Fallback: use channel-specific default transforms
    switch (this->channel_type_) {
      case CHANNEL_TYPE_PRESSURE:
        // Pass-through voltage (user should configure calibration)
        calibrated = value;
        break;
        
      case CHANNEL_TYPE_PH:
        // Pass-through voltage (user should configure calibration)
        calibrated = value;
        break;
        
      case CHANNEL_TYPE_ORP:
        // Default: convert V to mV
        calibrated = value * 1000.0f;
        break;
        
      default:
        calibrated = value;
        break;
    }
  }
  
  this->last_calibrated_value_ = calibrated;
  
  // Publish calibrated value
  this->publish_state(calibrated);
  
  ESP_LOGV(TAG, "Channel %s: raw=%.4f V, calibrated=%.2f (type=%s, valid=%s)", 
           this->get_channel_type_name(), value, calibrated,
           this->calibration_.get_type_name(),
           this->calibration_.is_valid() ? "yes" : "no");
}


// ============================================================================
// PoolStationSensor (Legacy Lot 0 compatibility)
// ============================================================================

void PoolStationSensor::setup() {
  ESP_LOGD(TAG, "Setting up Pool Station Sensor: %s", this->get_role_name());
}

void PoolStationSensor::dump_config() {
  LOG_SENSOR("", "Pool Station Sensor", this);
  ESP_LOGCONFIG(TAG, "  Role: %s (%d)", this->get_role_name(), static_cast<int>(this->role_));
}

const char *PoolStationSensor::get_role_name() const {
  switch (this->role_) {
    case ROLE_WATER_TEMPERATURE: return "water_temperature";
    case ROLE_AIR_PUMP: return "air_pump";
    case ROLE_AIR_LOCAL: return "air_local";
    case ROLE_PH_RAW: return "ph_raw";
    case ROLE_PH_CALIBRATED: return "ph_calibrated";
    case ROLE_ORP_RAW: return "orp_raw";
    case ROLE_ORP_CALIBRATED: return "orp_calibrated";
    case ROLE_PRESSURE_RAW: return "pressure_raw";
    case ROLE_PRESSURE_CALIBRATED: return "pressure_calibrated";
    default: return "unknown";
  }
}

void PoolStationSensor::publish_value(float value) {
  this->last_raw_value_ = value;
  this->last_calibrated_value_ = value;  // No calibration in legacy mode
  
  this->publish_state(value);
}

}  // namespace pool_station
}  // namespace esphome
