#include "calibration_ui.h"

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station.ui";

// ============================================================================
// CalibrationPointXNumber
// ============================================================================

void CalibrationPointXNumber::setup() {
  ESP_LOGD(TAG, "Setting up CalibrationPointXNumber (channel=%d, point=%d)", 
           this->channel_type_, this->point_index_);
  this->update_from_calibration();
}

void CalibrationPointXNumber::dump_config() {
  LOG_NUMBER("", "Calibration Point X", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
  ESP_LOGCONFIG(TAG, "  Point index: %d", this->point_index_);
}

void CalibrationPointXNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  CalibrationPoint pt = engine->get_point(this->point_index_);
  // Publish X whenever it is finite. Capture writes X first; Y may still be NaN.
  // Requiring is_valid() (both axes) left HA at `unknown` after "Capturer".
  if (!std::isnan(pt.x)) {
    this->publish_state(pt.x);
  } else {
    this->publish_state(NAN);
  }
}

void CalibrationPointXNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
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
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Set calibration point %d X=%.4f for channel %d", 
           this->point_index_, value, this->channel_type_);
}

// ============================================================================
// CalibrationPointYNumber
// ============================================================================

void CalibrationPointYNumber::setup() {
  ESP_LOGD(TAG, "Setting up CalibrationPointYNumber (channel=%d, point=%d)", 
           this->channel_type_, this->point_index_);
  this->update_from_calibration();
}

void CalibrationPointYNumber::dump_config() {
  LOG_NUMBER("", "Calibration Point Y", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
  ESP_LOGCONFIG(TAG, "  Point index: %d", this->point_index_);
}

void CalibrationPointYNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  CalibrationPoint pt = engine->get_point(this->point_index_);
  if (!std::isnan(pt.y)) {
    this->publish_state(pt.y);
  } else {
    this->publish_state(NAN);
  }
}

void CalibrationPointYNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
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
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Set calibration point %d Y=%.4f for channel %d", 
           this->point_index_, value, this->channel_type_);
}

// ============================================================================
// DFRobotMidNumber
// ============================================================================

void DFRobotMidNumber::setup() {
  ESP_LOGD(TAG, "Setting up DFRobotMidNumber (channel=%d)", this->channel_type_);
  this->update_from_calibration();
}

void DFRobotMidNumber::dump_config() {
  LOG_NUMBER("", "DFRobot Mid mV", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
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
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  engine->set_dfrobot_mid_mv(value);
  this->publish_state(value);
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Set DFRobot mid_mv=%.1f for channel %d", value, this->channel_type_);
}

// ============================================================================
// DFRobotOffsetNumber
// ============================================================================

void DFRobotOffsetNumber::setup() {
  ESP_LOGD(TAG, "Setting up DFRobotOffsetNumber (channel=%d)", this->channel_type_);
  this->update_from_calibration();
}

void DFRobotOffsetNumber::dump_config() {
  LOG_NUMBER("", "DFRobot Offset mV", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
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
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  engine->set_dfrobot_offset_mv(value);
  this->publish_state(value);
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Set DFRobot offset_mv=%.1f for channel %d", value, this->channel_type_);
}

// ============================================================================
// CalibrationCaptureButton
// ============================================================================

void CalibrationCaptureButton::dump_config() {
  LOG_BUTTON("", "Calibration Capture Button", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
  ESP_LOGCONFIG(TAG, "  Point index: %d", this->point_index_);
}

void CalibrationCaptureButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Capture button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found for capture", this->channel_type_);
    return;
  }
  
  float raw_value = channel->get_raw_value();
  if (std::isnan(raw_value)) {
    ESP_LOGW(TAG, "Cannot capture: no raw value available for channel %d", this->channel_type_);
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
  
  // Refresh all Capturer widgets, then force the captured slot's X number
  // so HA shows the voltage immediately (notify can miss this slot if the
  // engine re-sorts by X or the point is not fully valid yet).
  channel->notify_calibration_updated();
  this->parent_->publish_captured_point_x(this->channel_type_, this->point_index_, raw_value);
  
  ESP_LOGI(TAG, "Captured raw=%.4fV into point %d for channel %d", 
           raw_value, this->point_index_, this->channel_type_);
}

// ============================================================================
// CalibrationSaveButton
// ============================================================================

void CalibrationSaveButton::dump_config() {
  LOG_BUTTON("", "Calibration Save Button", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationSaveButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Save button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found for save", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;

  // Do not persist an invalid draft (would brick live + flash until re-cal).
  if (engine->is_draft_mode() && !engine->is_draft_valid()) {
    ESP_LOGW(TAG,
             "Save refused: draft invalid (type=%s, points=%zu, min=%d) for channel %d — live unchanged",
             calibration_type_name(engine->get_type()), engine->get_point_count(),
             engine->get_minimum_points_for_type(engine->get_type()), this->channel_type_);
    channel->notify_calibration_updated();
    return;
  }
  
  // Lot 5: Capture water temperature at calibration time
  float water_temp = NAN;
  if (this->parent_->has_water_temperature()) {
    water_temp = this->parent_->get_water_temperature();
    engine->set_calibration_temperature(water_temp);
    ESP_LOGI(TAG, "Captured Tw=%.1f°C at calibration time for channel %d", 
             water_temp, this->channel_type_);
  }
  
  // draft_mode: Save = commit draft → live + flash + clear dirty.
  // Legacy: persist the already-live set.
  if (engine->is_draft_mode()) {
    engine->commit_draft();
    ESP_LOGI(TAG, "Save committed draft calibration for channel %d", this->channel_type_);
  } else {
    engine->save_to_preferences();
  }
  
  // Update calibration temp sensor if configured
  channel->publish_calibration_temperature();
  channel->notify_calibration_updated();
  
  if (!std::isnan(water_temp)) {
    ESP_LOGI(TAG, "Saved calibration for channel %d to flash (Tw=%.1f°C)", 
             this->channel_type_, water_temp);
  } else {
    ESP_LOGI(TAG, "Saved calibration for channel %d to flash", this->channel_type_);
  }
}

// ============================================================================
// CalibrationInvalidSensor
// ============================================================================

void CalibrationInvalidSensor::setup() {
  ESP_LOGD(TAG, "Setting up CalibrationInvalidSensor (channel=%d)", this->channel_type_);
  this->publish_state(false);
}

void CalibrationInvalidSensor::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  bool is_invalid = !engine->is_valid();
  const bool changed = is_invalid != this->last_state_;
  this->last_state_ = is_invalid;
  this->publish_state(is_invalid);
  
  if (changed) {
    if (is_invalid) {
      ESP_LOGW(TAG, "Channel %d calibration is INVALID (need %d live points, have %zu)",
               this->channel_type_,
               engine->get_minimum_points_for_type(engine->get_live_type()),
               engine->get_live_point_count());
    } else {
      ESP_LOGI(TAG, "Channel %d calibration is valid", this->channel_type_);
    }
  }
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

  if ((!engine->is_valid()) != this->last_state_) {
    this->update_from_calibration();
  }
}

void CalibrationInvalidSensor::dump_config() {
  LOG_BINARY_SENSOR("", "Calibration Invalid Sensor", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

// ============================================================================
// Lot 7: CalibrationAlgorithmSelect
// ============================================================================

CalibrationType CalibrationAlgorithmSelect::string_to_type(const std::string &str) {
  if (str == "none") return CAL_TYPE_NONE;
  if (str == "linear") return CAL_TYPE_LINEAR;
  if (str == "polynomial") return CAL_TYPE_POLYNOMIAL;
  if (str == "piecewise") return CAL_TYPE_PIECEWISE;
  if (str == "dfrobot_orp") return CAL_TYPE_DFROBOT_ORP;
  if (str == "exponential") return CAL_TYPE_EXPONENTIAL;
  if (str == "logarithmic") return CAL_TYPE_LOGARITHMIC;
  if (str == "power") return CAL_TYPE_POWER;
  return CAL_TYPE_NONE;
}

std::string CalibrationAlgorithmSelect::type_to_string(CalibrationType type) {
  switch (type) {
    case CAL_TYPE_NONE: return "none";
    case CAL_TYPE_LINEAR: return "linear";
    case CAL_TYPE_POLYNOMIAL: return "polynomial";
    case CAL_TYPE_PIECEWISE: return "piecewise";
    case CAL_TYPE_DFROBOT_ORP: return "dfrobot_orp";
    case CAL_TYPE_EXPONENTIAL: return "exponential";
    case CAL_TYPE_LOGARITHMIC: return "logarithmic";
    case CAL_TYPE_POWER: return "power";
    default: return "none";
  }
}

void CalibrationAlgorithmSelect::setup() {
  ESP_LOGD(TAG, "Setting up CalibrationAlgorithmSelect (channel=%d)", this->channel_type_);

  // Belt-and-suspenders: must match algorithm_select_options() (Python codegen).
  // ESPHome 2026.4 SelectTraits::set_options takes initializer_list<const char*>.
  // dfrobot_orp is ORP-only; pressure/pH must not advertise it.
  if (this->channel_type_ == CHANNEL_TYPE_ORP) {
    this->traits.set_options({"none", "linear", "polynomial", "piecewise", "dfrobot_orp"});
  } else {
    this->traits.set_options({"none", "linear", "polynomial", "piecewise"});
  }

  this->update_from_calibration();
}

void CalibrationAlgorithmSelect::dump_config() {
  LOG_SELECT("", "Calibration Algorithm Select", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationAlgorithmSelect::update_from_calibration() {
  if (this->parent_ == nullptr) return;

  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;

  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;

  std::string type_str = type_to_string(engine->get_type());
  const auto &options = this->traits.get_options();
  bool known = false;
  for (const auto &option : options) {
    if (option == type_str) {
      known = true;
      break;
    }
  }
  if (!known) {
    ESP_LOGW(TAG, "Algorithm '%s' is not in select options; publishing 'none'", type_str.c_str());
    type_str = "none";
  }
  this->publish_state(type_str);
}

void CalibrationAlgorithmSelect::control(const std::string &value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  CalibrationType new_type = string_to_type(value);
  engine->set_type_runtime(new_type);
  this->publish_state(value);
  
  // Notify channel that calibration changed
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Changed calibration algorithm to '%s' for channel %d", 
           value.c_str(), this->channel_type_);
}

// ============================================================================
// Lot 7: CalibrationAddPointButton
// ============================================================================

void CalibrationAddPointButton::dump_config() {
  LOG_BUTTON("", "Calibration Add Point Button", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationAddPointButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Add point button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  if (engine->get_point_count() >= MAX_CALIBRATION_POINTS) {
    ESP_LOGW(TAG, "Cannot add point: maximum (%d) reached", MAX_CALIBRATION_POINTS);
    return;
  }
  
  // Add a point with default values (can be edited via number entities)
  engine->add_point(0.0f, 0.0f);
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Added new calibration point for channel %d (total: %zu)", 
           this->channel_type_, engine->get_point_count());
}

// ============================================================================
// Lot 7: CalibrationRemovePointButton
// ============================================================================

void CalibrationRemovePointButton::dump_config() {
  LOG_BUTTON("", "Calibration Remove Point Button", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationRemovePointButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Remove point button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  size_t count = engine->get_point_count();
  if (count == 0) {
    ESP_LOGW(TAG, "Cannot remove point: no points exist");
    return;
  }
  
  // Remove the last point
  engine->remove_point(count - 1);
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Removed last calibration point for channel %d (remaining: %zu)", 
           this->channel_type_, engine->get_point_count());
}

// ============================================================================
// Lot 7: CalibrationCommitButton
// ============================================================================

void CalibrationCommitButton::dump_config() {
  LOG_BUTTON("", "Calibration Commit Button", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationCommitButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Commit button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found for commit", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  if (!engine->is_draft_mode()) {
    ESP_LOGW(TAG, "Cannot commit: draft mode not enabled for channel %d", this->channel_type_);
    return;
  }

  if (!engine->is_draft_valid()) {
    ESP_LOGW(TAG,
             "Commit refused: draft invalid (type=%s, points=%zu, min=%d) for channel %d — live unchanged",
             calibration_type_name(engine->get_type()), engine->get_point_count(),
             engine->get_minimum_points_for_type(engine->get_type()), this->channel_type_);
    channel->notify_calibration_updated();
    return;
  }
  
  // Capture water temperature before commit (Lot 5 compatibility)
  if (this->parent_->has_water_temperature()) {
    float water_temp = this->parent_->get_water_temperature();
    engine->set_calibration_temperature(water_temp);
    ESP_LOGI(TAG, "Captured Tw=%.1f°C at commit time for channel %d", 
             water_temp, this->channel_type_);
  }
  
  engine->commit_draft();
  channel->publish_calibration_temperature();
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Committed draft calibration for channel %d", this->channel_type_);
}

// ============================================================================
// Lot 7: CalibrationDiscardButton
// ============================================================================

void CalibrationDiscardButton::dump_config() {
  LOG_BUTTON("", "Calibration Discard Button", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationDiscardButton::press_action() {
  if (this->parent_ == nullptr) {
    ESP_LOGW(TAG, "Discard button has no parent");
    return;
  }
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found for discard", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  if (!engine->is_draft_mode()) {
    ESP_LOGW(TAG, "Cannot discard: draft mode not enabled for channel %d", this->channel_type_);
    return;
  }
  
  engine->discard_draft();
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Discarded draft changes for channel %d", this->channel_type_);
}

// ============================================================================
// Lot 7: CalibrationPointCountNumber
// ============================================================================

void CalibrationPointCountNumber::setup() {
  ESP_LOGD(TAG, "Setting up CalibrationPointCountNumber (channel=%d)", this->channel_type_);
  this->update_from_calibration();
}

void CalibrationPointCountNumber::dump_config() {
  LOG_NUMBER("", "Calibration Point Count", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

void CalibrationPointCountNumber::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  this->publish_state(static_cast<float>(engine->get_point_count()));
}

void CalibrationPointCountNumber::control(float value) {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) {
    ESP_LOGW(TAG, "Channel %d not found", this->channel_type_);
    return;
  }
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  int target_count = static_cast<int>(value);
  if (target_count < 1) target_count = 1;
  if (target_count > MAX_CALIBRATION_POINTS) target_count = MAX_CALIBRATION_POINTS;
  
  int current_count = static_cast<int>(engine->get_point_count());
  
  // Add or remove points to match target
  while (current_count < target_count) {
    engine->add_point(0.0f, 0.0f);
    current_count++;
  }
  while (current_count > target_count) {
    engine->remove_point(current_count - 1);
    current_count--;
  }
  
  this->publish_state(static_cast<float>(target_count));
  channel->notify_calibration_updated();
  
  ESP_LOGI(TAG, "Set calibration point count to %d for channel %d", 
           target_count, this->channel_type_);
}

// ============================================================================
// Lot 7: DraftPendingSensor
// ============================================================================

void DraftPendingSensor::setup() {
  ESP_LOGD(TAG, "Setting up DraftPendingSensor (channel=%d)", this->channel_type_);
  this->publish_state(false);
}

void DraftPendingSensor::update_from_calibration() {
  if (this->parent_ == nullptr) return;
  
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;
  
  bool has_changes = engine->has_draft_changes();
  const bool changed = has_changes != this->last_state_;
  this->last_state_ = has_changes;
  this->publish_state(has_changes);
  
  if (changed && has_changes) {
    ESP_LOGD(TAG, "Channel %d has uncommitted draft changes", this->channel_type_);
  }
}

void DraftPendingSensor::loop() {
  // Check every 500ms
  uint32_t now = millis();
  if (now - this->last_check_ < 500) {
    return;
  }
  this->last_check_ = now;

  if (this->parent_ == nullptr) return;
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;

  if (engine->has_draft_changes() != this->last_state_) {
    this->update_from_calibration();
  }
}

void DraftPendingSensor::dump_config() {
  LOG_BINARY_SENSOR("", "Draft Pending Sensor", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

// ============================================================================
// Lot 7 / v0.7.18: DraftInvalidSensor
// ============================================================================

void DraftInvalidSensor::setup() {
  ESP_LOGD(TAG, "Setting up DraftInvalidSensor (channel=%d)", this->channel_type_);
  this->publish_state(false);
}

void DraftInvalidSensor::update_from_calibration() {
  if (this->parent_ == nullptr) return;

  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;

  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;

  bool invalid = engine->is_draft_mode() && !engine->is_draft_valid();
  const bool changed = invalid != this->last_state_;
  this->last_state_ = invalid;
  this->publish_state(invalid);

  if (changed && invalid) {
    ESP_LOGW(TAG, "Channel %d draft is INVALID — Save will be refused until points/algo are fixed",
             this->channel_type_);
  }
}

void DraftInvalidSensor::loop() {
  uint32_t now = millis();
  if (now - this->last_check_ < 500) {
    return;
  }
  this->last_check_ = now;

  if (this->parent_ == nullptr) return;
  PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
  if (channel == nullptr) return;
  CalibrationEngine *engine = channel->get_calibration_engine();
  if (engine == nullptr) return;

  bool invalid = engine->is_draft_mode() && !engine->is_draft_valid();
  if (invalid != this->last_state_) {
    this->update_from_calibration();
  }
}

void DraftInvalidSensor::dump_config() {
  LOG_BINARY_SENSOR("", "Draft Invalid Sensor", this);
  ESP_LOGCONFIG(TAG, "  Channel: %d", this->channel_type_);
}

}  // namespace pool_station
}  // namespace esphome
