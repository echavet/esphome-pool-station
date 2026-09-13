#include "calibration_ui.h"

namespace esphome {
namespace pool_station {

static const char *const UI_TAG = "pool_station.ui";

// ============================================================================
// CalibrationPointXNumber
// ============================================================================

void CalibrationPointXNumber::setup() {
  ESP_LOGD(UI_TAG, "Setting up CalibrationPointXNumber (channel=%d, point=%d)", 
           this->channel_type_, this->point_index_);
  this->update_from_calibration();
}

void CalibrationPointXNumber::dump_config() {
  LOG_NUMBER("", "Calibration Point X", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
  ESP_LOGCONFIG(UI_TAG, "  Point index: %d", this->point_index_);
}

void CalibrationPointXNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  CalibrationPoint pt = engine->get_point(this->point_index_);
  if (pt.is_valid()) {
    this->publish_state(pt.x);
  } else {
    this->publish_state(NAN);
  }
}

void CalibrationPointXNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(UI_TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  // Get current Y value for this point
  CalibrationPoint existing = engine->get_point(this->point_index_);
  float y_value = existing.is_valid() ? existing.y : 0.0f;
  
  // Ensure we have enough points
  while (engine->get_point_count() <= this->point_index_) {
    engine->add_point(NAN, NAN);
  }
  
  engine->set_point(this->point_index_, value, y_value);
  this->publish_state(value);
  
  ESP_LOGI(UI_TAG, "Set calibration point %d X=%.4f for channel %d", 
           this->point_index_, value, this->channel_type_);
}

// ============================================================================
// CalibrationPointYNumber
// ============================================================================

void CalibrationPointYNumber::setup() {
  ESP_LOGD(UI_TAG, "Setting up CalibrationPointYNumber (channel=%d, point=%d)", 
           this->channel_type_, this->point_index_);
  this->update_from_calibration();
}

void CalibrationPointYNumber::dump_config() {
  LOG_NUMBER("", "Calibration Point Y", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
  ESP_LOGCONFIG(UI_TAG, "  Point index: %d", this->point_index_);
}

void CalibrationPointYNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  CalibrationPoint pt = engine->get_point(this->point_index_);
  if (pt.is_valid()) {
    this->publish_state(pt.y);
  } else {
    this->publish_state(NAN);
  }
}

void CalibrationPointYNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(UI_TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  // Get current X value for this point
  CalibrationPoint existing = engine->get_point(this->point_index_);
  float x_value = existing.is_valid() ? existing.x : 0.0f;
  
  // Ensure we have enough points
  while (engine->get_point_count() <= this->point_index_) {
    engine->add_point(NAN, NAN);
  }
  
  engine->set_point(this->point_index_, x_value, value);
  this->publish_state(value);
  
  ESP_LOGI(UI_TAG, "Set calibration point %d Y=%.4f for channel %d", 
           this->point_index_, value, this->channel_type_);
}

// ============================================================================
// DFRobotMidNumber
// ============================================================================

void DFRobotMidNumber::setup() {
  ESP_LOGD(UI_TAG, "Setting up DFRobotMidNumber (channel=%d)", this->channel_type_);
  this->update_from_calibration();
}

void DFRobotMidNumber::dump_config() {
  LOG_NUMBER("", "DFRobot Mid mV", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
}

void DFRobotMidNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  this->publish_state(engine->get_dfrobot_mid_mv());
}

void DFRobotMidNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(UI_TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  engine->set_dfrobot_mid_mv(value);
  this->publish_state(value);
  
  ESP_LOGI(UI_TAG, "Set DFRobot mid_mv=%.1f for channel %d", value, this->channel_type_);
}

// ============================================================================
// DFRobotOffsetNumber
// ============================================================================

void DFRobotOffsetNumber::setup() {
  ESP_LOGD(UI_TAG, "Setting up DFRobotOffsetNumber (channel=%d)", this->channel_type_);
  this->update_from_calibration();
}

void DFRobotOffsetNumber::dump_config() {
  LOG_NUMBER("", "DFRobot Offset mV", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
}

void DFRobotOffsetNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  this->publish_state(engine->get_dfrobot_offset_mv());
}

void DFRobotOffsetNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(UI_TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  engine->set_dfrobot_offset_mv(value);
  this->publish_state(value);
  
  ESP_LOGI(UI_TAG, "Set DFRobot offset_mv=%.1f for channel %d", value, this->channel_type_);
}

// ============================================================================
// CalibrationCaptureButton
// ============================================================================

void CalibrationCaptureButton::dump_config() {
  LOG_BUTTON("", "Calibration Capture Button", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
  ESP_LOGCONFIG(UI_TAG, "  Point index: %d", this->point_index_);
}

void CalibrationCaptureButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(UI_TAG, "Capture button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(UI_TAG, "Channel %d not found for capture", this->channel_type_);
    return;
  }
  
  float raw_value = channel->get_raw_value();
  if (std::isnan(raw_value)) {
    ESP_LOGW(UI_TAG, "Cannot capture: no raw value available for channel %d", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  // Get existing Y value (preserve it) or use 0
  CalibrationPoint existing = engine->get_point(this->point_index_);
  float y_value = existing.is_valid() ? existing.y : 0.0f;
  
  // Ensure we have enough points
  while (engine->get_point_count() <= this->point_index_) {
    engine->add_point(NAN, NAN);
  }
  
  engine->set_point(this->point_index_, raw_value, y_value);
  
  // Notify the parent to update any linked number entities
  channel->notify_calibration_updated();
  
  ESP_LOGI(UI_TAG, "Captured raw=%.4fV into point %d for channel %d", 
           raw_value, this->point_index_, this->channel_type_);
}

// ============================================================================
// CalibrationSaveButton
// ============================================================================

void CalibrationSaveButton::dump_config() {
  LOG_BUTTON("", "Calibration Save Button", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationSaveButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(UI_TAG, "Save button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(UI_TAG, "Channel %d not found for save", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  engine->save_to_preferences();
  
  ESP_LOGI(UI_TAG, "Saved calibration for channel %d to flash", this->channel_type_);
}

// ============================================================================
// CalibrationInvalidSensor
// ============================================================================

void CalibrationInvalidSensor::setup() {
  ESP_LOGD(UI_TAG, "Setting up CalibrationInvalidSensor (channel=%d)", this->channel_type_);
  this->publish_state(false);
}

void CalibrationInvalidSensor::loop() {
  // Check every 1 second to avoid excessive updates
  uint32_t now = millis();
  if (now - this->last_check_ < 1000) {
    return;
  }
  this->last_check_ = now;
  
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  // Check if calibration is invalid (insufficient points or type=none)
  bool is_invalid = !engine->is_valid();
  
  // Only publish if state changed
  if (is_invalid != this->last_state_) {
    this->last_state_ = is_invalid;
    this->publish_state(is_invalid);
    
    if (is_invalid) {
      ESP_LOGW(UI_TAG, "Channel %d calibration is INVALID (need %d points, have %zu)",
               this->channel_type_, engine->get_minimum_points(), engine->get_point_count());
    } else {
      ESP_LOGI(UI_TAG, "Channel %d calibration is valid", this->channel_type_);
    }
  }
}

void CalibrationInvalidSensor::dump_config() {
  LOG_BINARY_SENSOR("", "Calibration Invalid Sensor", this);
  ESP_LOGCONFIG(UI_TAG, "  Channel: %d", this->channel_type_);
}

}  // namespace pool_station
}  // namespace esphome
