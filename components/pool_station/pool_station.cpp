#include "pool_station.h"

namespace esphome {
namespace pool_station {

// ============================================================================
// PoolStationComponent
// ============================================================================

void PoolStationComponent::setup() {
  ESP_LOGI(TAG, "Setting up Pool Station component...");
  ESP_LOGI(TAG, "  Update interval: %u ms", this->update_interval_);
  ESP_LOGI(TAG, "  Calibration mode: %d", static_cast<int>(this->calibration_mode_));
  
  if (this->water_temp_sensor_ != nullptr) {
    ESP_LOGI(TAG, "  Water temperature sensor: bound");
  } else {
    ESP_LOGD(TAG, "  Water temperature sensor: not configured");
  }

  ESP_LOGI(TAG, "  Registered sensors: %zu", this->sensors_.size());
  for (const auto &pair : this->sensors_) {
    PoolStationSensor *sensor = pair.second;
    if (sensor != nullptr) {
      ESP_LOGI(TAG, "    - Role %d: %s", pair.first, sensor->get_role_name());
    }
  }

  ESP_LOGI(TAG, "Pool Station setup complete.");
}

void PoolStationComponent::loop() {
  uint32_t now = millis();
  if (now - this->last_update_ < this->update_interval_) {
    return;
  }
  this->last_update_ = now;

  this->update_sensors_();
}

void PoolStationComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Pool Station Component:");
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->update_interval_);
  
  const char *cal_mode_str = "none";
  switch (this->calibration_mode_) {
    case CAL_MODE_LINEAR: cal_mode_str = "linear"; break;
    case CAL_MODE_POLYNOMIAL: cal_mode_str = "polynomial"; break;
    case CAL_MODE_EXPONENTIAL: cal_mode_str = "exponential"; break;
    case CAL_MODE_LOGARITHMIC: cal_mode_str = "logarithmic"; break;
    case CAL_MODE_POWER: cal_mode_str = "power"; break;
    case CAL_MODE_PIECEWISE: cal_mode_str = "piecewise"; break;
    case CAL_MODE_DFROBOT_ORP: cal_mode_str = "dfrobot_orp"; break;
    default: cal_mode_str = "none"; break;
  }
  ESP_LOGCONFIG(TAG, "  Calibration mode: %s", cal_mode_str);
  
  if (this->water_temp_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Water temperature sensor: configured");
  }
  
  ESP_LOGCONFIG(TAG, "  Registered sensors:");
  for (const auto &pair : this->sensors_) {
    PoolStationSensor *sensor = pair.second;
    if (sensor != nullptr) {
      ESP_LOGCONFIG(TAG, "    - %s (role %d)", sensor->get_role_name(), pair.first);
    }
  }
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

void PoolStationComponent::update_sensors_() {
  // Lot 0: Stub implementation
  // In future lots, this will:
  // 1. Read raw values from ADS1115/Dallas/GPIO sensors
  // 2. Apply calibration algorithms
  // 3. Run diagnostic checks (noise, jumps, drift)
  // 4. Handle gated/campaign sampling
  // 5. Publish calibrated values with diagnostics flags

  ESP_LOGV(TAG, "Update cycle (stub) - %zu sensors registered", this->sensors_.size());

  // Example: if water temperature is configured, log it
  if (this->water_temp_sensor_ != nullptr) {
    float water_temp = this->get_water_temperature();
    if (!std::isnan(water_temp)) {
      ESP_LOGV(TAG, "  Water temperature: %.2f C", water_temp);
    }
  }

  // TODO Lot 1+: Actually read and process sensor values
  // For now, sensors don't publish values in the stub
}

// ============================================================================
// PoolStationSensor
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
  // TODO Lot 1+: Apply calibration before publishing
  // TODO Lot 3+: Apply filters (filter_samples, max_jump, streak, min/max)
  // TODO Lot 4+: Run diagnostics and set flags
  
  this->last_raw_value_ = value;
  this->last_calibrated_value_ = value;  // No calibration in Lot 0
  
  this->publish_state(value);
}

}  // namespace pool_station
}  // namespace esphome
