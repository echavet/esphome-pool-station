#include "pool_station.h"
#include "calibration_ui.h"
#include <cmath>

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station";

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
      if (this->interference_enabled_ && !this->calibration_mode_active_) {
        this->interference_.push_water_temp(water_temp, millis());
      }
    }
  }

  if (this->interference_enabled_) {
    this->publish_interference_(millis());
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

  if (this->interference_enabled_) {
    const InterferenceConfig &ic = this->interference_.get_config();
    ESP_LOGCONFIG(TAG, "  Interference detection: ON");
    ESP_LOGCONFIG(TAG, "    coincident_jump: %s (orp>=%.1f, pressure>=%.2f, window=%u ms)",
                  ic.enable_coincident_jump ? "YES" : "NO", ic.orp_jump, ic.pressure_jump, ic.jump_window_ms);
    ESP_LOGCONFIG(TAG, "    shared_noise: %s (ph_sigma>=%.3f, orp_sigma>=%.1f)",
                  ic.enable_shared_noise ? "YES" : "NO", ic.ph_sigma, ic.orp_sigma);
    ESP_LOGCONFIG(TAG, "    tw_coupling: %s (ph>=%.3f, tw>=%.2f, window=%u ms)",
                  ic.enable_tw_coupling ? "YES" : "NO", ic.ph_jump, ic.tw_jump, ic.tw_window_ms);
    ESP_LOGCONFIG(TAG, "    hold_time: %u ms", ic.hold_ms);
    ESP_LOGCONFIG(TAG, "    samples: pre-guard (compensated); jump_window must cover slower channel interval");
    if (ic.enable_coincident_jump &&
        (this->channels_.count(CHANNEL_TYPE_PRESSURE) == 0 || this->channels_.count(CHANNEL_TYPE_ORP) == 0)) {
      ESP_LOGCONFIG(TAG, "    WARNING: coincident_jump needs both pressure and orp channels");
    }
    if (ic.enable_shared_noise &&
        (this->channels_.count(CHANNEL_TYPE_PH) == 0 || this->channels_.count(CHANNEL_TYPE_ORP) == 0)) {
      ESP_LOGCONFIG(TAG, "    WARNING: shared_noise needs both ph and orp channels");
    }
    if (ic.enable_tw_coupling && this->water_temp_sensor_ == nullptr) {
      ESP_LOGCONFIG(TAG, "    WARNING: tw_coupling enabled but no water_temperature bound");
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
    if (this->interference_enabled_) {
      // Enter and leave Capturer with a clean window so buffer tours
      // (pH 4/7/10) cannot trip shared_noise afterwards.
      this->interference_.reset();
      this->publish_interference_(millis());
    }
  }
}

void PoolStationComponent::on_channel_sample(uint8_t channel_type, float value, uint32_t now_ms) {
  if (!this->interference_enabled_)
    return;
  if (this->calibration_mode_active_)
    return;
  this->interference_.push_channel(channel_type, value, now_ms);
  this->publish_interference_(now_ms);
}

void PoolStationComponent::publish_interference_(uint32_t now_ms) {
  const InterferenceFlags flags = this->interference_.evaluate(now_ms, this->calibration_mode_active_);
  if (this->interference_suspected_sensor_ != nullptr) {
    this->interference_suspected_sensor_->publish_state(flags.suspected);
  }
  if (this->interference_coincident_jump_sensor_ != nullptr) {
    this->interference_coincident_jump_sensor_->publish_state(flags.coincident_jump);
  }
  if (this->interference_shared_noise_sensor_ != nullptr) {
    this->interference_shared_noise_sensor_->publish_state(flags.shared_noise);
  }
  if (this->interference_tw_coupling_sensor_ != nullptr) {
    this->interference_tw_coupling_sensor_->publish_state(flags.tw_coupling);
  }
  if (flags.suspected && this->interference_.should_log_now(now_ms)) {
    ESP_LOGW(TAG, "[pool_station] Interference suspected (%s)", this->interference_.active_reason());
    this->interference_.mark_logged(now_ms);
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

void PoolStationComponent::register_algorithm_select(CalibrationAlgorithmSelect *sel, uint8_t channel_type) {
  if (sel == nullptr) return;
  this->algorithm_selects_[channel_type] = sel;
}

void PoolStationComponent::register_point_count_number(CalibrationPointCountNumber *num, uint8_t channel_type) {
  if (num == nullptr) return;
  this->point_count_numbers_[channel_type] = num;
}

void PoolStationComponent::register_draft_pending_sensor(DraftPendingSensor *sensor, uint8_t channel_type) {
  if (sensor == nullptr) return;
  this->draft_pending_sensors_[channel_type] = sensor;
}

void PoolStationComponent::register_draft_invalid_sensor(DraftInvalidSensor *sensor, uint8_t channel_type) {
  if (sensor == nullptr) return;
  this->draft_invalid_sensors_[channel_type] = sensor;
}

void PoolStationComponent::refresh_calibration_ui(uint8_t channel_type) {
  auto x_it = this->point_x_numbers_.find(channel_type);
  if (x_it != this->point_x_numbers_.end()) {
    for (auto &kv : x_it->second) {
      if (kv.second != nullptr) {
        kv.second->update_from_calibration();
      }
    }
  }

  auto y_it = this->point_y_numbers_.find(channel_type);
  if (y_it != this->point_y_numbers_.end()) {
    for (auto &kv : y_it->second) {
      if (kv.second != nullptr) {
        kv.second->update_from_calibration();
      }
    }
  }

  auto algo_it = this->algorithm_selects_.find(channel_type);
  if (algo_it != this->algorithm_selects_.end() && algo_it->second != nullptr) {
    algo_it->second->update_from_calibration();
  }

  auto count_it = this->point_count_numbers_.find(channel_type);
  if (count_it != this->point_count_numbers_.end() && count_it->second != nullptr) {
    count_it->second->update_from_calibration();
  }

  auto mid_it = this->dfrobot_mid_numbers_.find(channel_type);
  if (mid_it != this->dfrobot_mid_numbers_.end() && mid_it->second != nullptr) {
    mid_it->second->update_from_calibration();
  }

  auto offset_it = this->dfrobot_offset_numbers_.find(channel_type);
  if (offset_it != this->dfrobot_offset_numbers_.end() && offset_it->second != nullptr) {
    offset_it->second->update_from_calibration();
  }

  auto invalid_it = this->cal_invalid_sensors_.find(channel_type);
  if (invalid_it != this->cal_invalid_sensors_.end() && invalid_it->second != nullptr) {
    invalid_it->second->update_from_calibration();
  }

  auto draft_it = this->draft_pending_sensors_.find(channel_type);
  if (draft_it != this->draft_pending_sensors_.end() && draft_it->second != nullptr) {
    draft_it->second->update_from_calibration();
  }

  auto draft_invalid_it = this->draft_invalid_sensors_.find(channel_type);
  if (draft_invalid_it != this->draft_invalid_sensors_.end() && draft_invalid_it->second != nullptr) {
    draft_invalid_it->second->update_from_calibration();
  }
}

void PoolStationComponent::publish_captured_point_x(uint8_t channel_type, uint8_t point_index, float x) {
  auto ch_it = this->point_x_numbers_.find(channel_type);
  if (ch_it == this->point_x_numbers_.end()) {
    return;
  }
  auto num_it = ch_it->second.find(point_index);
  if (num_it == ch_it->second.end() || num_it->second == nullptr) {
    return;
  }
  num_it->second->publish_state(x);
}

void PoolStationComponent::register_campaign(MeasurementCampaign *campaign) {
  if (campaign == nullptr) return;
  
  const std::string &name = campaign->get_name();
  if (this->campaigns_.count(name) > 0) {
    ESP_LOGW(TAG, "Campaign '%s' already registered, replacing", name.c_str());
  }
  
  this->campaigns_[name] = campaign;
  ESP_LOGD(TAG, "Registered campaign: %s", name.c_str());
}

MeasurementCampaign *PoolStationComponent::get_campaign(const std::string &name) {
  auto it = this->campaigns_.find(name);
  if (it != this->campaigns_.end()) {
    return it->second;
  }
  return nullptr;
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

  // Snapshot YAML seeds before NVS (Lot A gain + Lot B filters/interval).
  this->snapshot_yaml_filter_seeds_();

  // Lot A/B: YAML seed then NVS overlay (NVS wins). Apply before subscribe so
  // the first source poll usually sees the runtime gain. One YAML-gain sample
  // is still possible if the ADS already updated during its own setup.
  if (this->ads_overlay_enabled_ || this->runtime_prefs_key_ != 0) {
    ads_runtime::init_unmanaged(&this->runtime_prefs_cache_);
  }
  if (this->ads_overlay_enabled_) {
    this->gain_shadow_ = this->yaml_gain_;
    this->apply_ads_gain_to_source_();
  }
  if (this->runtime_prefs_key_ != 0) {
    this->load_runtime_preferences();
  }
  if (this->ads_overlay_enabled_) {
    this->publish_ads_ui_();
  }
  this->publish_filter_ui_();
  
  // Initialize filter components (Lot 3 / Lot B after possible NVS override).
  // sync_* skip a no-op resize/reset so a NVS apply does not clear twice.
  this->sync_median_window_();
  this->sync_jump_guard_(false);
  if (this->filter_config_.is_median_enabled()) {
    ESP_LOGD(TAG, "Channel %s median filter: %d samples",
             this->get_channel_type_name(), this->filter_config_.filter_samples);
  }
  if (this->filter_config_.is_jump_guard_enabled()) {
    ESP_LOGD(TAG, "Channel %s jump guard: max_jump=%.3f, streak=%d",
             this->get_channel_type_name(),
             this->filter_config_.max_jump,
             this->filter_config_.max_jump_streak);
  }
  
  // Subscribe to source sensor
  this->source_sensor_->add_on_state_callback([this](float value) {
    this->on_source_value_(value);
  });
  
  ESP_LOGD(TAG, "Channel %s subscribed to source sensor", this->get_channel_type_name());
  
  // Log published (live) calibration — draft algo must not hide a valid live set.
  if (this->calibration_.get_live_type() != CAL_TYPE_NONE) {
    ESP_LOGI(TAG, "Channel %s calibration: type=%s, valid=%s, points=%zu",
             this->get_channel_type_name(),
             this->calibration_.get_type_name(),
             this->calibration_.is_valid() ? "YES" : "NO",
             this->calibration_.get_live_point_count());
    
    // Publish calibration temperature if available (Lot 5)
    if (this->calibration_.has_calibration_temperature()) {
      ESP_LOGI(TAG, "Channel %s calibrated at Tw=%.1f°C",
               this->get_channel_type_name(),
               this->calibration_.get_calibration_temperature());
      this->publish_calibration_temperature();
    }
  }

  // Numbers/selects often setup() before prefs are loaded; push engine state now.
  this->notify_calibration_updated();
  
  // Initialize diagnostics (Lot 4)
  if (this->diagnostics_enabled_) {
    this->diagnostics_.set_config(this->diagnostics_config_);
    ESP_LOGD(TAG, "Channel %s diagnostics: window=%d, ptp_warn=%.3f, sigma_warn=%.3f, stuck=%ums",
             this->get_channel_type_name(),
             this->diagnostics_config_.noise_window,
             this->diagnostics_config_.noise_warn_ptp,
             this->diagnostics_config_.noise_warn_sigma,
             this->diagnostics_config_.stuck_timeout_ms);
  }

  // NVS deferred flush (v0.10.4): register shutdown hook for best-effort save
  this->register_shutdown_hook_();
}

void PoolStationChannelSensor::loop() {
  uint32_t now = millis();
  if (this->ads_overlay_enabled_) {
    this->expire_saturation_hold_(now);
  }
  // NVS deferred flush (v0.10.4): save after idle debounce
  this->flush_nvs_if_idle_(now);
}

void PoolStationChannelSensor::dump_config() {
  LOG_SENSOR("", "Pool Station Channel", this);
  ESP_LOGCONFIG(TAG, "  Type: %s", this->get_channel_type_name());
  ESP_LOGCONFIG(TAG, "  Update interval: %u ms", this->configured_interval_);
  if (this->ads_overlay_enabled_) {
    ESP_LOGCONFIG(TAG, "  ADS overlay: ON (compose, not ownership)");
    ESP_LOGCONFIG(TAG, "    gain shadow: %s (FSR=%.3f V, yaml seed=%s)",
                  ads_runtime::gain_code_to_label(this->gain_shadow_), this->get_fsr_volts(),
                  ads_runtime::gain_code_to_label(this->yaml_gain_));
    ESP_LOGCONFIG(TAG, "    saturation: ON>=%.0f%% FSR, OFF<=%.0f%%, hold=%u ms",
                  this->sat_on_ * 100.0f, this->sat_off_ * 100.0f, this->sat_hold_ms_);
    if (this->gain_at_last_cal_save_ == ads_runtime::GAIN_UNMANAGED) {
      ESP_LOGCONFIG(TAG, "    gain_at_last_cal_save: never");
    } else {
      ESP_LOGCONFIG(TAG, "    gain_at_last_cal_save: %s%s",
                    ads_runtime::gain_code_to_label(this->gain_at_last_cal_save_),
                    this->is_gain_mismatch() ? " (MISMATCH)" : "");
    }
    ESP_LOGCONFIG(TAG, "    changing gain does not rescale calibration X/Y (volts)");
  }
  if (this->raw_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Raw sensor: configured");
  }
  if (this->calibrated_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Calibrated sensor (pre-guard): configured");
  }
  
  // Dump filter config (Lot 3)
  ESP_LOGCONFIG(TAG, "  Filters:");
  ESP_LOGCONFIG(TAG, "    filter_samples: %d%s", 
                this->filter_config_.filter_samples,
                this->filter_config_.is_median_enabled() ? " (median enabled)" : " (off)");
  ESP_LOGCONFIG(TAG, "    max_jump: %.3f%s", 
                this->filter_config_.max_jump,
                this->filter_config_.is_jump_guard_enabled() ? "" : " (off)");
  if (this->filter_config_.is_jump_guard_enabled()) {
    ESP_LOGCONFIG(TAG, "    max_jump_streak: %d", this->filter_config_.max_jump_streak);
  }
  if (!std::isnan(this->filter_config_.value_min)) {
    ESP_LOGCONFIG(TAG, "    value_min: %.3f", this->filter_config_.value_min);
  }
  if (!std::isnan(this->filter_config_.value_max)) {
    ESP_LOGCONFIG(TAG, "    value_max: %.3f", this->filter_config_.value_max);
  }
  if (this->filters_persist_ || this->interval_persist_) {
    ESP_LOGCONFIG(TAG, "    runtime persist: %s%s (sidecar ps-md5-rt-v1, not cal draft)",
                  this->filters_persist_ ? "filters" : "",
                  this->interval_persist_ ? (this->filters_persist_ ? "+interval" : "interval") : "");
  }
  
  // Dump calibration config
  this->calibration_.dump_config();
  
  // Dump diagnostics config (Lot 4)
  if (this->diagnostics_enabled_) {
    ESP_LOGCONFIG(TAG, "  Diagnostics:");
    ESP_LOGCONFIG(TAG, "    noise_window: %d", this->diagnostics_config_.noise_window);
    ESP_LOGCONFIG(TAG, "    noise_warn_ptp: %.4f%s", 
                  this->diagnostics_config_.noise_warn_ptp,
                  this->diagnostics_config_.noise_warn_ptp > 0 ? "" : " (off)");
    ESP_LOGCONFIG(TAG, "    noise_warn_sigma: %.4f%s", 
                  this->diagnostics_config_.noise_warn_sigma,
                  this->diagnostics_config_.noise_warn_sigma > 0 ? "" : " (off)");
    ESP_LOGCONFIG(TAG, "    stuck_timeout: %u ms%s", 
                  this->diagnostics_config_.stuck_timeout_ms,
                  this->diagnostics_config_.stuck_timeout_ms > 0 ? "" : " (off)");
    if (!std::isnan(this->diagnostics_config_.range_min)) {
      ESP_LOGCONFIG(TAG, "    range_min: %.3f", this->diagnostics_config_.range_min);
    }
    if (!std::isnan(this->diagnostics_config_.range_max)) {
      ESP_LOGCONFIG(TAG, "    range_max: %.3f", this->diagnostics_config_.range_max);
    }
    if (this->diag_mean_sensor_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    mean_sensor: configured");
    }
    if (this->diag_sigma_sensor_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    sigma_sensor: configured");
    }
    if (this->diag_ptp_sensor_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    ptp_sensor: configured");
    }
    if (this->diag_noisy_flag_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    noisy_flag: configured");
    }
    if (this->diag_stuck_flag_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    stuck_flag: configured");
    }
    if (this->diag_out_of_range_flag_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    out_of_range_flag: configured");
    }
  }
  
  // Dump temperature compensation config (Lot 5)
  if (this->channel_type_ == CHANNEL_TYPE_PH || this->channel_type_ == CHANNEL_TYPE_ORP) {
    this->temp_compensator_.dump_config();
    if (this->temp_comp_switch_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    temp_comp_switch: configured");
    }
    if (this->cal_temp_sensor_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    calibration_temp_sensor: configured");
    }
  }
  
  // Dump gate config (Lot 6)
  if (this->gate_ != nullptr) {
    ESP_LOGCONFIG(TAG, "  Gate:");
    this->gate_->dump_config();
    if (this->gate_blocked_sensor_ != nullptr) {
      ESP_LOGCONFIG(TAG, "    gate_blocked_sensor: configured");
    }
  }
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

void PoolStationChannelSensor::set_draft_mode_enabled(bool enabled) {
  if (enabled) {
    this->calibration_.enable_draft_mode();
  } else {
    this->calibration_.disable_draft_mode();
  }
}

void PoolStationChannelSensor::set_ads1115_sensor(sensor::Sensor *sensor) {
#ifdef USE_ADS1115
  this->ads_ = static_cast<ads1115::ADS1115Sensor *>(sensor);
  this->ads_overlay_enabled_ = (sensor != nullptr);
#else
  this->ads_overlay_enabled_ = false;
  (void) sensor;
#endif
}

void PoolStationChannelSensor::set_ads_overlay_enabled(bool enabled) {
#ifdef USE_ADS1115
  // Codegen may call set_ads_overlay_enabled(true) after bind. Never enable
  // the overlay without a typed ADS pointer — otherwise HA would "change
  // gain" without set_gain() (USE_ADS1115 off or failed bind).
  this->ads_overlay_enabled_ = enabled && (this->ads_ != nullptr);
#else
  this->ads_overlay_enabled_ = false;
  (void) enabled;
#endif
}

void PoolStationChannelSensor::set_runtime_preferences_key(uint32_t key) {
  this->runtime_prefs_key_ = key;
}

void PoolStationChannelSensor::ensure_runtime_prefs_obj_() {
  if (this->runtime_prefs_obj_ready_ || this->runtime_prefs_key_ == 0) {
    return;
  }
  this->runtime_prefs_ =
      global_preferences->make_preference<ads_runtime::ChannelRuntimePrefsData>(
          this->runtime_prefs_key_);
  this->runtime_prefs_obj_ready_ = true;
}

void PoolStationChannelSensor::apply_ads_gain_to_source_() {
#ifdef USE_ADS1115
  if (this->ads_ != nullptr && ads_runtime::is_managed_gain(this->gain_shadow_)) {
    this->ads_->set_gain(static_cast<ads1115::ADS1115Gain>(this->gain_shadow_));
  }
#endif
}

void PoolStationChannelSensor::apply_gain_runtime(uint8_t gain_code, bool persist) {
  if (!ads_runtime::is_valid_gain_code(gain_code)) {
    ESP_LOGW(TAG, "Channel %s: ignore invalid ADS gain code 0x%02X",
             this->get_channel_type_name(), gain_code);
    return;
  }
  // Defense in depth: persist=true is a user-facing apply. Soft-lock while
  // Calibration Mode is ON (select / reset YAML already refuse; NVS load
  // uses persist=false and still applies at setup).
  if (persist && this->parent_ != nullptr && this->parent_->is_calibration_mode()) {
    ESP_LOGW(TAG, "Channel %s: ADS gain apply refused: Calibration Mode ON — select unchanged",
             this->get_channel_type_name());
    this->publish_ads_ui_();
    return;
  }
  const bool changed = (gain_code != this->gain_shadow_);
  this->gain_shadow_ = gain_code;
  this->apply_ads_gain_to_source_();
  // Design §5.3: auto-save NVS on every persist apply (including first HA
  // apply of the YAML-same gain so NVS wins after reboot).
  // v0.10.4: deferred flush — mark dirty instead of immediate save.
  if (persist) {
    this->mark_nvs_dirty_(false, false);
  }
  this->publish_ads_ui_();
  if (changed) {
    ESP_LOGI(TAG, "Channel %s: ADS gain %s (FSR=%.3f V) — calibration volts unchanged",
             this->get_channel_type_name(), ads_runtime::gain_code_to_label(gain_code),
             this->get_fsr_volts());
  }
}

void PoolStationChannelSensor::reset_ads_to_yaml() {
  ESP_LOGI(TAG, "Channel %s: reset ADS gain to YAML seed %s",
           this->get_channel_type_name(), ads_runtime::gain_code_to_label(this->yaml_gain_));
  this->apply_gain_runtime(this->yaml_gain_, true);
}

void PoolStationChannelSensor::load_runtime_preferences() {
  if (this->runtime_prefs_key_ == 0) {
    return;
  }
  this->ensure_runtime_prefs_obj_();
  ads_runtime::ChannelRuntimePrefsData data;
  ads_runtime::init_unmanaged(&data);
  if (!this->runtime_prefs_.load(&data)) {
    ESP_LOGD(TAG, "Channel %s: no runtime ADS prefs, using YAML seed",
             this->get_channel_type_name());
    ads_runtime::init_unmanaged(&this->runtime_prefs_cache_);
    this->runtime_prefs_loaded_ = false;
    return;
  }
  if (data.magic != ads_runtime::CHANNEL_RUNTIME_PREFS_MAGIC ||
      data.version != ads_runtime::CHANNEL_RUNTIME_PREFS_VERSION) {
    ESP_LOGD(TAG, "Channel %s: ignore runtime prefs magic=0x%08X ver=%u",
             this->get_channel_type_name(), data.magic, data.version);
    ads_runtime::init_unmanaged(&this->runtime_prefs_cache_);
    this->runtime_prefs_loaded_ = false;
    return;
  }
  this->runtime_prefs_cache_ = data;
  this->runtime_prefs_loaded_ = true;
  this->gain_at_last_cal_save_ = data.gain_at_last_cal_save;
  if (this->ads_overlay_enabled_ && (data.flags & ads_runtime::RT_FLAG_HAS_GAIN) &&
      ads_runtime::is_managed_gain(data.ads_gain)) {
    this->apply_gain_runtime(data.ads_gain, false);
    ESP_LOGI(TAG, "Channel %s: loaded runtime ADS gain %s from NVS",
             this->get_channel_type_name(), ads_runtime::gain_code_to_label(data.ads_gain));
  }
  this->apply_nvs_filter_fields_(data);
}

void PoolStationChannelSensor::save_runtime_preferences(bool stamp_filters, bool stamp_interval) {
  if (this->runtime_prefs_key_ == 0) {
    return;
  }
  this->ensure_runtime_prefs_obj_();
  ads_runtime::ChannelRuntimePrefsData data = this->runtime_prefs_cache_;
  if (data.magic != ads_runtime::CHANNEL_RUNTIME_PREFS_MAGIC) {
    ads_runtime::init_unmanaged(&data);
  }
  data.magic = ads_runtime::CHANNEL_RUNTIME_PREFS_MAGIC;
  data.version = ads_runtime::CHANNEL_RUNTIME_PREFS_VERSION;
  if (this->ads_overlay_enabled_) {
    data.ads_gain = this->gain_shadow_;
    data.flags = static_cast<uint8_t>(data.flags | ads_runtime::RT_FLAG_HAS_GAIN);
    data.gain_at_last_cal_save = this->gain_at_last_cal_save_;
  }
  // Design §4.2: Save cal / gain apply must not write filters. Only a
  // filter HA apply (or reset_yaml) stamps these fields so a Lot A blob
  // plus an unused filters.runtime: persist does not freeze YAML seeds.
  if (this->filters_persist_ && stamp_filters) {
    data.filter_samples = this->filter_config_.filter_samples;
    data.max_jump_streak = this->filter_config_.max_jump_streak;
    data.max_jump = this->filter_config_.max_jump;
    if (!std::isnan(this->filter_config_.value_min)) {
      data.flags = static_cast<uint8_t>(data.flags | ads_runtime::RT_FLAG_HAS_VMIN);
      data.value_min = this->filter_config_.value_min;
    } else {
      data.flags = static_cast<uint8_t>(data.flags & ~ads_runtime::RT_FLAG_HAS_VMIN);
      data.value_min = NAN;
    }
    if (!std::isnan(this->filter_config_.value_max)) {
      data.flags = static_cast<uint8_t>(data.flags | ads_runtime::RT_FLAG_HAS_VMAX);
      data.value_max = this->filter_config_.value_max;
    } else {
      data.flags = static_cast<uint8_t>(data.flags & ~ads_runtime::RT_FLAG_HAS_VMAX);
      data.value_max = NAN;
    }
  }
  if (this->interval_persist_ && stamp_interval) {
    data.update_interval_ms = this->configured_interval_;
  }
  this->runtime_prefs_.save(&data);
  this->runtime_prefs_cache_ = data;
  this->runtime_prefs_loaded_ = true;
}

void PoolStationChannelSensor::remember_gain_at_cal_save() {
  if (!this->ads_overlay_enabled_) {
    return;
  }
  this->gain_at_last_cal_save_ = this->gain_shadow_;
  // Calibration save is a critical user action — flush immediately.
  // Do not defer: losing gain_at_last_cal_save would break mismatch detection.
  // Preserve any pending stamps so filter/interval changes aren't lost.
  this->save_runtime_preferences(this->nvs_pending_stamp_filters_,
                                  this->nvs_pending_stamp_interval_);
  // Clear pending dirty state since we just saved everything.
  this->nvs_dirty_ = false;
  this->nvs_pending_stamp_filters_ = false;
  this->nvs_pending_stamp_interval_ = false;
  this->publish_gain_mismatch_();
  ESP_LOGI(TAG, "Channel %s: remembered ADS gain %s at calibration Save",
           this->get_channel_type_name(), ads_runtime::gain_code_to_label(this->gain_shadow_));
}

float PoolStationChannelSensor::get_fsr_volts() const {
  return ads_runtime::fsr_volts_for_gain(this->gain_shadow_);
}

float PoolStationChannelSensor::get_fsr_percent(float raw_v) const {
  return ads_runtime::fsr_percent(raw_v, this->get_fsr_volts());
}

bool PoolStationChannelSensor::is_adc_saturated(float raw_v) const {
  if (!this->ads_overlay_enabled_) {
    return false;
  }
  return ads_runtime::above_sat_on(raw_v, this->get_fsr_volts(), this->sat_on_);
}

bool PoolStationChannelSensor::refuse_calibration_save_if_saturated() const {
  return this->is_adc_saturated();
}

bool PoolStationChannelSensor::is_gain_mismatch() const {
  if (!this->ads_overlay_enabled_) {
    return false;
  }
  if (this->gain_at_last_cal_save_ == ads_runtime::GAIN_UNMANAGED) {
    return false;
  }
  return this->gain_shadow_ != this->gain_at_last_cal_save_;
}

void PoolStationChannelSensor::update_saturation_(uint32_t now) {
  if (!this->ads_overlay_enabled_) {
    return;
  }
  const float fsr = this->get_fsr_volts();
  const float raw = this->last_raw_value_;
  this->sat_raw_latched_ = ads_runtime::saturation_hysteresis(
      std::fabs(raw), fsr, this->sat_on_, this->sat_off_, this->sat_raw_latched_);
  if (this->sat_raw_latched_) {
    this->sat_last_on_ms_ = now;
    this->sat_published_ = true;
  } else {
    this->expire_saturation_hold_(now);
  }
  if (this->saturated_sensor_ != nullptr) {
    this->saturated_sensor_->publish_state(this->sat_published_);
  }
  if (this->fsr_percent_sensor_ != nullptr) {
    this->fsr_percent_sensor_->publish_state(this->get_fsr_percent(raw));
  }
}

void PoolStationChannelSensor::expire_saturation_hold_(uint32_t now) {
  if (!this->sat_published_ || this->sat_raw_latched_) {
    return;
  }
  if (ads_runtime::saturation_hold_expired(now, this->sat_last_on_ms_, this->sat_hold_ms_)) {
    this->sat_published_ = false;
    if (this->saturated_sensor_ != nullptr) {
      this->saturated_sensor_->publish_state(false);
    }
  }
}

void PoolStationChannelSensor::publish_gain_mismatch_() {
  if (this->gain_mismatch_sensor_ != nullptr) {
    this->gain_mismatch_sensor_->publish_state(this->is_gain_mismatch());
  }
}

void PoolStationChannelSensor::publish_ads_ui_() {
  if (this->gain_select_ != nullptr) {
    this->gain_select_->update_from_channel();
  }
  this->publish_gain_mismatch_();
  if (this->ads_overlay_enabled_) {
    this->update_saturation_(millis());
  }
}

void PoolStationChannelSensor::set_filter_samples(uint8_t samples) {
  this->filter_config_.filter_samples = samples;
}

void PoolStationChannelSensor::set_max_jump(float max_jump) {
  this->filter_config_.max_jump = max_jump;
}

void PoolStationChannelSensor::set_max_jump_streak(uint8_t streak) {
  this->filter_config_.max_jump_streak = streak;
}

void PoolStationChannelSensor::set_value_min(float min) {
  this->filter_config_.value_min = min;
}

void PoolStationChannelSensor::set_value_max(float max) {
  this->filter_config_.value_max = max;
}

void PoolStationChannelSensor::snapshot_yaml_filter_seeds_() {
  this->yaml_filter_samples_ = this->filter_config_.filter_samples;
  this->yaml_max_jump_ = this->filter_config_.max_jump;
  this->yaml_max_jump_streak_ = this->filter_config_.max_jump_streak;
  this->yaml_value_min_ = this->filter_config_.value_min;
  this->yaml_value_max_ = this->filter_config_.value_max;
  this->yaml_update_interval_ms_ = this->configured_interval_;
}

void PoolStationChannelSensor::sync_median_window_() {
  const uint8_t want =
      this->filter_config_.is_median_enabled() ? this->filter_config_.filter_samples : 0;
  if (this->raw_window_.get_size() == want) {
    return;
  }
  this->raw_window_.set_size(want);
}

void PoolStationChannelSensor::sync_jump_guard_(bool reset) {
  const bool unchanged = (this->jump_guard_.get_max_jump() == this->filter_config_.max_jump) &&
                         (this->jump_guard_.get_streak_threshold() == this->filter_config_.max_jump_streak);
  this->jump_guard_.set_max_jump(this->filter_config_.max_jump);
  this->jump_guard_.set_streak_threshold(this->filter_config_.max_jump_streak);
  if (reset && !unchanged) {
    this->jump_guard_.reset();
  }
}

void PoolStationChannelSensor::publish_filter_ui_() {
  for (FilterRuntimeNumber *num : this->filter_runtime_numbers_) {
    if (num != nullptr) {
      num->update_from_channel();
    }
  }
}

void PoolStationChannelSensor::persist_filters_if_(bool persist) {
  if (persist && this->filters_persist_) {
    // v0.10.4: deferred flush — mark dirty instead of immediate save.
    this->mark_nvs_dirty_(true, false);
  }
}

void PoolStationChannelSensor::persist_interval_if_(bool persist) {
  if (persist && this->interval_persist_) {
    // v0.10.4: deferred flush — mark dirty instead of immediate save.
    this->mark_nvs_dirty_(false, true);
  }
}

void PoolStationChannelSensor::register_filter_runtime_number(FilterRuntimeNumber *num) {
  if (num == nullptr) {
    return;
  }
  this->filter_runtime_numbers_.push_back(num);
}

void PoolStationChannelSensor::apply_nvs_filter_fields_(const ads_runtime::ChannelRuntimePrefsData &data) {
  bool applied = false;
  if (this->filters_persist_) {
    if (ads_runtime::is_managed_u8(data.filter_samples)) {
      this->apply_filter_samples_runtime(data.filter_samples, false);
      applied = true;
    }
    if (ads_runtime::is_managed_float(data.max_jump)) {
      this->apply_max_jump_runtime(data.max_jump, false);
      applied = true;
    }
    if (ads_runtime::is_managed_u8(data.max_jump_streak)) {
      this->apply_max_jump_streak_runtime(data.max_jump_streak, false);
      applied = true;
    }
    if ((data.flags & ads_runtime::RT_FLAG_HAS_VMIN) &&
        ads_runtime::is_managed_float(data.value_min)) {
      this->apply_value_min_runtime(data.value_min, false);
      applied = true;
    }
    if ((data.flags & ads_runtime::RT_FLAG_HAS_VMAX) &&
        ads_runtime::is_managed_float(data.value_max)) {
      this->apply_value_max_runtime(data.value_max, false);
      applied = true;
    }
    if (applied) {
      ESP_LOGI(TAG, "Channel %s: loaded runtime filters from NVS (samples=%u jump=%.3f streak=%u)",
               this->get_channel_type_name(), this->filter_config_.filter_samples,
               this->filter_config_.max_jump, this->filter_config_.max_jump_streak);
    }
  }
  if (this->interval_persist_ && ads_runtime::is_managed_interval_ms(data.update_interval_ms)) {
    this->apply_update_interval_runtime(data.update_interval_ms, false);
    ESP_LOGI(TAG, "Channel %s: loaded runtime update_interval %u ms from NVS",
             this->get_channel_type_name(), this->configured_interval_);
  }
}

// ============================================================================
// NVS Deferred Flush (v0.10.4)
// Coalesces rapid HA parameter changes into a single NVS write after idle.
// ============================================================================

void PoolStationChannelSensor::mark_nvs_dirty_(bool stamp_filters, bool stamp_interval) {
  if (this->runtime_prefs_key_ == 0) {
    return;
  }
  if (!this->nvs_dirty_) {
    this->nvs_dirty_ = true;
    this->nvs_dirty_since_ms_ = millis();
  }
  this->nvs_pending_stamp_filters_ = this->nvs_pending_stamp_filters_ || stamp_filters;
  this->nvs_pending_stamp_interval_ = this->nvs_pending_stamp_interval_ || stamp_interval;
}

void PoolStationChannelSensor::flush_nvs_if_idle_(uint32_t now) {
  if (!this->nvs_dirty_) {
    return;
  }
  if ((now - this->nvs_dirty_since_ms_) < ads_runtime::NVS_FLUSH_DEBOUNCE_MS) {
    return;
  }
  this->flush_nvs_now_();
}

void PoolStationChannelSensor::flush_nvs_now_() {
  if (!this->nvs_dirty_) {
    return;
  }
  ESP_LOGI(TAG, "Channel %s: flushing NVS prefs (filters=%s, interval=%s)",
           this->get_channel_type_name(),
           this->nvs_pending_stamp_filters_ ? "yes" : "no",
           this->nvs_pending_stamp_interval_ ? "yes" : "no");
  this->save_runtime_preferences(this->nvs_pending_stamp_filters_,
                                  this->nvs_pending_stamp_interval_);
  this->nvs_dirty_ = false;
  this->nvs_pending_stamp_filters_ = false;
  this->nvs_pending_stamp_interval_ = false;
}

void PoolStationChannelSensor::register_shutdown_hook_() {
  if (this->nvs_shutdown_hook_registered_) {
    return;
  }
  if (this->runtime_prefs_key_ == 0) {
    return;
  }
  PoolStationChannelSensor *self = this;
  App.register_shutdown_hook([self]() {
    if (self->nvs_dirty_) {
      ESP_LOGI(TAG, "Channel %s: shutdown — flushing pending NVS prefs",
               self->get_channel_type_name());
      self->flush_nvs_now_();
    }
  });
  this->nvs_shutdown_hook_registered_ = true;
}

void PoolStationChannelSensor::apply_filter_samples_runtime(uint8_t n, bool persist) {
  this->filter_config_.filter_samples = ads_runtime::clamp_filter_samples(n);
  // set_size() clears — publish raw until is_full() (same as boot).
  this->sync_median_window_();
  this->persist_filters_if_(persist);
  this->publish_filter_ui_();
  ESP_LOGI(TAG, "Channel %s: filter_samples=%u (median window resized, not a cal draft)",
           this->get_channel_type_name(), this->filter_config_.filter_samples);
}

void PoolStationChannelSensor::apply_max_jump_runtime(float j, bool persist) {
  this->filter_config_.max_jump = ads_runtime::clamp_max_jump(j);
  this->sync_jump_guard_(true);
  this->persist_filters_if_(persist);
  this->publish_filter_ui_();
  ESP_LOGI(TAG, "Channel %s: max_jump=%.3f (JumpGuard reset, Lot 8 still pre-guard)",
           this->get_channel_type_name(), this->filter_config_.max_jump);
}

void PoolStationChannelSensor::apply_max_jump_streak_runtime(uint8_t s, bool persist) {
  this->filter_config_.max_jump_streak = ads_runtime::clamp_max_jump_streak(s);
  this->sync_jump_guard_(true);
  this->persist_filters_if_(persist);
  this->publish_filter_ui_();
  ESP_LOGI(TAG, "Channel %s: max_jump_streak=%u (JumpGuard reset)",
           this->get_channel_type_name(), this->filter_config_.max_jump_streak);
}

void PoolStationChannelSensor::apply_value_min_runtime(float v, bool persist) {
  if (!std::isnan(v) && !std::isfinite(v)) {
    ESP_LOGW(TAG, "Channel %s: ignore non-finite value_min", this->get_channel_type_name());
    this->publish_filter_ui_();
    return;
  }
  if (!ads_runtime::clamp_bounds_valid(v, this->filter_config_.value_max)) {
    ESP_LOGW(TAG, "Channel %s: value_min=%.3f > value_max=%.3f, clamp apply refused",
             this->get_channel_type_name(), v, this->filter_config_.value_max);
    this->publish_filter_ui_();
    return;
  }
  this->filter_config_.value_min = v;
  this->persist_filters_if_(persist);
  this->publish_filter_ui_();
  if (std::isnan(v)) {
    ESP_LOGI(TAG, "Channel %s: value_min clamp off", this->get_channel_type_name());
  } else {
    ESP_LOGI(TAG, "Channel %s: value_min=%.3f (clamp, not j5 reject)",
             this->get_channel_type_name(), v);
  }
}

void PoolStationChannelSensor::apply_value_max_runtime(float v, bool persist) {
  if (!std::isnan(v) && !std::isfinite(v)) {
    ESP_LOGW(TAG, "Channel %s: ignore non-finite value_max", this->get_channel_type_name());
    this->publish_filter_ui_();
    return;
  }
  if (!ads_runtime::clamp_bounds_valid(this->filter_config_.value_min, v)) {
    ESP_LOGW(TAG, "Channel %s: value_max=%.3f < value_min=%.3f, clamp apply refused",
             this->get_channel_type_name(), v, this->filter_config_.value_min);
    this->publish_filter_ui_();
    return;
  }
  this->filter_config_.value_max = v;
  this->persist_filters_if_(persist);
  this->publish_filter_ui_();
  if (std::isnan(v)) {
    ESP_LOGI(TAG, "Channel %s: value_max clamp off", this->get_channel_type_name());
  } else {
    ESP_LOGI(TAG, "Channel %s: value_max=%.3f (clamp, not j5 reject)",
             this->get_channel_type_name(), v);
  }
}

void PoolStationChannelSensor::apply_update_interval_runtime(uint32_t ms, bool persist) {
  // HA widget is 1–3600 s. YAML seeds / NVS restore keep sub-second intervals.
  this->configured_interval_ = ads_runtime::sanitize_update_interval_ms(ms, persist);
  this->persist_interval_if_(persist);
  this->publish_filter_ui_();
  ESP_LOGI(TAG, "Channel %s: update_interval=%u ms (cal-mode still %u ms; ADS source unchanged)",
           this->get_channel_type_name(), this->configured_interval_,
           this->parent_ != nullptr ? this->parent_->get_calibration_interval() : 1000);
}

void PoolStationChannelSensor::reset_filters_to_yaml() {
  ESP_LOGI(TAG, "Channel %s: reset filters/interval to YAML seeds", this->get_channel_type_name());
  this->apply_filter_samples_runtime(this->yaml_filter_samples_, false);
  this->apply_max_jump_runtime(this->yaml_max_jump_, false);
  this->apply_max_jump_streak_runtime(this->yaml_max_jump_streak_, false);
  this->apply_value_min_runtime(this->yaml_value_min_, false);
  this->apply_value_max_runtime(this->yaml_value_max_, false);
  this->apply_update_interval_runtime(this->yaml_update_interval_ms_, false);
  if (this->filters_persist_ || this->interval_persist_) {
    // v0.10.4: deferred flush — mark dirty instead of immediate save.
    this->mark_nvs_dirty_(this->filters_persist_, this->interval_persist_);
  }
  this->publish_filter_ui_();
}

// ============================================================================
// Diagnostics configuration setters (Lot 4)
// ============================================================================

void PoolStationChannelSensor::set_diagnostics_enabled(bool enabled) {
  this->diagnostics_enabled_ = enabled;
}

void PoolStationChannelSensor::set_noise_window(uint8_t size) {
  this->diagnostics_config_.noise_window = size;
}

void PoolStationChannelSensor::set_noise_warn_ptp(float threshold) {
  this->diagnostics_config_.noise_warn_ptp = threshold;
}

void PoolStationChannelSensor::set_noise_warn_sigma(float threshold) {
  this->diagnostics_config_.noise_warn_sigma = threshold;
}

void PoolStationChannelSensor::set_stuck_timeout_ms(uint32_t timeout) {
  this->diagnostics_config_.stuck_timeout_ms = timeout;
}

void PoolStationChannelSensor::set_stuck_threshold(float threshold) {
  this->diagnostics_config_.stuck_threshold = threshold;
}

void PoolStationChannelSensor::set_diagnostics_range_min(float min) {
  this->diagnostics_config_.range_min = min;
}

void PoolStationChannelSensor::set_diagnostics_range_max(float max) {
  this->diagnostics_config_.range_max = max;
}

void PoolStationChannelSensor::set_log_rate_limit_ms(uint32_t rate) {
  this->diagnostics_config_.log_rate_limit_ms = rate;
}

// ============================================================================
// Temperature compensation configuration setters (Lot 5)
// ============================================================================

void PoolStationChannelSensor::set_temp_compensation_enabled(bool enabled) {
  this->temp_compensator_.set_enabled(enabled);
}

void PoolStationChannelSensor::set_temp_comp_reference_temperature(float temp) {
  this->temp_compensator_.set_reference_temperature(temp);
}

void PoolStationChannelSensor::set_temp_comp_neutral_ph(float ph) {
  this->temp_compensator_.set_neutral_ph(ph);
}

void PoolStationChannelSensor::set_temp_comp_orp_coefficient(float coeff) {
  this->temp_compensator_.set_orp_coefficient(coeff);
}

void PoolStationChannelSensor::set_temp_compensation_enabled_runtime(bool enabled) {
  this->temp_compensator_.set_enabled(enabled);
  if (enabled) {
    ESP_LOGI(TAG, "Channel %s: Temperature compensation ENABLED", this->get_channel_type_name());
  } else {
    ESP_LOGI(TAG, "Channel %s: Temperature compensation DISABLED", this->get_channel_type_name());
  }
}

void PoolStationChannelSensor::publish_calibration_temperature() {
  if (this->cal_temp_sensor_ == nullptr) {
    return;
  }
  
  float cal_temp = this->calibration_.get_calibration_temperature();
  this->cal_temp_sensor_->publish_state(cal_temp);
  
  if (!std::isnan(cal_temp)) {
    ESP_LOGD(TAG, "Channel %s: Published calibration temperature %.1f°C", 
             this->get_channel_type_name(), cal_temp);
  }
}

const DiagnosticsStats &PoolStationChannelSensor::get_diagnostics_stats() const {
  return this->diagnostics_.get_stats();
}

const DiagnosticsFlags &PoolStationChannelSensor::get_diagnostics_flags() const {
  return this->diagnostics_.get_flags();
}

void PoolStationChannelSensor::notify_calibration_updated() {
  ESP_LOGD(TAG, "Calibration updated for channel %s — refreshing Capturer UI",
           this->get_channel_type_name());
  if (this->parent_ != nullptr) {
    this->parent_->refresh_calibration_ui(static_cast<uint8_t>(this->channel_type_));
  }
}

bool PoolStationChannelSensor::is_gated() const {
  if (this->gate_ == nullptr) {
    return false;  // No gate = not gated
  }
  return this->gate_->is_blocked();
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
  
  // =========================================================================
  // GATE CHECK (Lot 6): Block sampling if gate is blocked
  // =========================================================================
  if (this->gate_ != nullptr && this->gate_->is_enabled()) {
    bool was_blocked = this->gate_blocked_sensor_ != nullptr && this->gate_blocked_sensor_->state;
    bool is_blocked = this->gate_->is_blocked();
    
    // Update gate_blocked sensor if state changed
    if (this->gate_blocked_sensor_ != nullptr && was_blocked != is_blocked) {
      this->gate_blocked_sensor_->publish_state(is_blocked);
      if (is_blocked) {
        const GateCondition *blocker = this->gate_->get_blocking_condition();
        ESP_LOGI(TAG, "Channel %s: Gate BLOCKED%s",
                 this->get_channel_type_name(),
                 blocker ? (" by " + blocker->describe()).c_str() : "");
      } else {
        ESP_LOGI(TAG, "Channel %s: Gate OPEN (sampling resumed)", this->get_channel_type_name());
      }
    }
    
    // In calibration mode, bypass gate for testing
    bool bypass_gate = this->parent_ != nullptr && this->parent_->is_calibration_mode();
    
    if (is_blocked && !bypass_gate) {
      ESP_LOGV(TAG, "Channel %s: Sampling blocked by gate", this->get_channel_type_name());
      return;  // Don't process or publish
    }
  }
  
  // =========================================================================
  // PIPELINE: raw → median → raw_sensor → calibrate → temp_comp 
  //           → calibrated_sensor → clamp → jump guard → publish
  // (Lot 5: Temperature compensation inserted after calibration)
  // =========================================================================
  
  // Step 1: Store original raw value (Lot A sat uses this, pre-median)
  this->last_raw_value_ = value;
  if (this->ads_overlay_enabled_) {
    this->update_saturation_(now);
  }
  
  // Step 2: Apply median window filter (if enabled)
  // In calibration mode, bypass median for ~1s raw response
  float filtered_raw = value;
  if (this->filter_config_.is_median_enabled()) {
    bool bypass_median = this->parent_ != nullptr && this->parent_->is_calibration_mode();
    if (!bypass_median) {
      this->raw_window_.push(value);
      if (this->raw_window_.is_full()) {
        filtered_raw = this->raw_window_.median();
      }
    }
  }
  this->last_filtered_raw_ = filtered_raw;
  
  // Step 3: Publish raw value to diagnostic sensor (post-median if enabled)
  if (this->raw_sensor_ != nullptr) {
    this->raw_sensor_->publish_state(filtered_raw);
  }
  
  // Step 4: Apply last committed calibration (draft type/points must not gate this)
  float calibrated;
  if (this->calibration_.get_live_type() != CAL_TYPE_NONE && this->calibration_.is_valid()) {
    calibrated = this->calibration_.calibrate(filtered_raw);
  } else {
    // Fallback: use channel-specific default transforms
    switch (this->channel_type_) {
      case CHANNEL_TYPE_PRESSURE:
        // Pass-through voltage (user should configure calibration)
        calibrated = filtered_raw;
        break;
        
      case CHANNEL_TYPE_PH:
        // Pass-through voltage (user should configure calibration)
        calibrated = filtered_raw;
        break;
        
      case CHANNEL_TYPE_ORP:
        // Default: convert V to mV
        calibrated = filtered_raw * 1000.0f;
        break;
        
      default:
        calibrated = filtered_raw;
        break;
    }
  }
  this->last_calibrated_value_ = calibrated;
  
  // Step 5 (Lot 5): Apply temperature compensation
  // Get water temperature from parent component
  float t_water = NAN;
  if (this->parent_ != nullptr && this->parent_->has_water_temperature()) {
    t_water = this->parent_->get_water_temperature();
  }
  
  float compensated = calibrated;
  if (this->temp_compensator_.is_enabled() && !std::isnan(t_water)) {
    switch (this->channel_type_) {
      case CHANNEL_TYPE_PH:
        compensated = this->temp_compensator_.compensate_ph(calibrated, t_water);
        ESP_LOGV(TAG, "Channel %s: Tw=%.1f°C, pH %.3f → %.3f (compensated)",
                 this->get_channel_type_name(), t_water, calibrated, compensated);
        break;
        
      case CHANNEL_TYPE_ORP:
        compensated = this->temp_compensator_.compensate_orp(calibrated, t_water);
        if (std::abs(this->temp_compensator_.get_orp_coefficient()) > 0.001f) {
          ESP_LOGV(TAG, "Channel %s: Tw=%.1f°C, ORP %.1f → %.1f mV (compensated)",
                   this->get_channel_type_name(), t_water, calibrated, compensated);
        }
        break;
        
      case CHANNEL_TYPE_PRESSURE:
        // No temperature compensation for pressure by default
        break;
        
      default:
        break;
    }
  }
  
  // Step 6: Publish compensated value to diagnostic sensor (pre-guard)
  if (this->calibrated_sensor_ != nullptr) {
    this->calibrated_sensor_->publish_state(compensated);
  }
  
  // Step 7: Apply value clamp (min/max guards)
  float guarded = compensated;
  if (this->filter_config_.is_clamp_enabled()) {
    guarded = filter_functions::clamp_value(
        guarded, 
        this->filter_config_.value_min, 
        this->filter_config_.value_max);
  }
  
  // Step 8: Apply jump guard (reject absurd jumps, accept new plateau after streak)
  // In calibration mode, bypass jump guard for immediate response
  if (this->filter_config_.is_jump_guard_enabled()) {
    bool bypass_jump = this->parent_ != nullptr && this->parent_->is_calibration_mode();
    if (!bypass_jump) {
      bool rejected = false;
      guarded = this->jump_guard_.process(guarded, rejected);
      if (rejected) {
        ESP_LOGD(TAG, "Channel %s: jump rejected (%.3f → %.3f, keeping %.3f)",
                 this->get_channel_type_name(), 
                 this->last_guarded_value_, compensated, guarded);
      }
    } else {
      // Reset jump guard baseline in calibration mode
      this->jump_guard_.reset();
      bool dummy = false;
      this->jump_guard_.process(guarded, dummy);
    }
  }
  this->last_guarded_value_ = guarded;
  
  // Step 9: Publish final guarded value
  this->publish_state(guarded);
  
  // =========================================================================
  // DIAGNOSTICS (Lot 4): Process and publish diagnostic sensors/flags
  // =========================================================================
  if (this->diagnostics_enabled_) {
    // Process diagnostics on the guarded (final) value
    this->diagnostics_.process(guarded, now);
    
    const DiagnosticsStats &stats = this->diagnostics_.get_stats();
    const DiagnosticsFlags &flags = this->diagnostics_.get_flags();
    
    // Publish diagnostic sensors if configured
    if (this->diag_mean_sensor_ != nullptr && stats.is_valid()) {
      this->diag_mean_sensor_->publish_state(stats.mean);
    }
    if (this->diag_sigma_sensor_ != nullptr && stats.is_valid()) {
      this->diag_sigma_sensor_->publish_state(stats.sigma);
    }
    if (this->diag_ptp_sensor_ != nullptr && stats.is_valid()) {
      this->diag_ptp_sensor_->publish_state(stats.ptp);
    }
    
    // Publish diagnostic flags if configured
    if (this->diag_noisy_flag_ != nullptr) {
      this->diag_noisy_flag_->publish_state(flags.noisy);
    }
    if (this->diag_stuck_flag_ != nullptr) {
      this->diag_stuck_flag_->publish_state(flags.stuck);
    }
    if (this->diag_out_of_range_flag_ != nullptr) {
      this->diag_out_of_range_flag_->publish_state(flags.out_of_range);
    }
    
    // Structured logging on abnormal events (rate-limited)
    // Avoid spamming logs in calibration mode
    bool in_cal_mode = this->parent_ != nullptr && this->parent_->is_calibration_mode();
    if (!in_cal_mode && flags.has_any_problem() && this->diagnostics_.should_log_now(now)) {
      if (flags.noisy) {
        ESP_LOGW(TAG, "[pool_station] Channel %s: NOISY detected (ptp=%.4f, sigma=%.4f)",
                 this->get_channel_type_name(), stats.ptp, stats.sigma);
      }
      if (flags.stuck) {
        ESP_LOGW(TAG, "[pool_station] Channel %s: STUCK detected (value=%.3f, no change)",
                 this->get_channel_type_name(), guarded);
      }
      if (flags.out_of_range) {
        ESP_LOGW(TAG, "[pool_station] Channel %s: OUT_OF_RANGE (value=%.3f, range=[%.3f, %.3f])",
                 this->get_channel_type_name(), guarded,
                 this->diagnostics_config_.range_min, this->diagnostics_config_.range_max);
      }
      this->diagnostics_.mark_logged(now);
    }
  }

  if (this->parent_ != nullptr) {
    // Pre-jump-guard: Lot 3 max_jump would otherwise swallow the pump/EMI
    // spikes this detector is meant to flag.
    this->parent_->on_channel_sample(static_cast<uint8_t>(this->channel_type_), compensated, now);
  }
  
  ESP_LOGV(TAG, "Channel %s: raw=%.4f, filtered=%.4f, cal=%.3f, comp=%.3f, guarded=%.3f (type=%s)",
           this->get_channel_type_name(), value, filtered_raw, calibrated, compensated, guarded,
           this->calibration_.get_type_name());
}

float PoolStationChannelSensor::apply_filters_(float raw_value) {
  // Helper method for testing (not used in main pipeline currently)
  float result = raw_value;
  
  if (this->filter_config_.is_median_enabled() && this->raw_window_.is_full()) {
    result = this->raw_window_.median();
  }
  
  return result;
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

// ============================================================================
// Diagnostic Flag Binary Sensors (Lot 4)
// ============================================================================

void DiagnosticNoisyFlag::setup() {
  ESP_LOGD(TAG, "Setting up Diagnostic Noisy Flag for channel type %d", this->channel_type_);
  this->publish_state(false);
}

void DiagnosticNoisyFlag::dump_config() {
  LOG_BINARY_SENSOR("", "Diagnostic Noisy Flag", this);
  ESP_LOGCONFIG(TAG, "  Channel type: %d", this->channel_type_);
}

void DiagnosticStuckFlag::setup() {
  ESP_LOGD(TAG, "Setting up Diagnostic Stuck Flag for channel type %d", this->channel_type_);
  this->publish_state(false);
}

void DiagnosticStuckFlag::dump_config() {
  LOG_BINARY_SENSOR("", "Diagnostic Stuck Flag", this);
  ESP_LOGCONFIG(TAG, "  Channel type: %d", this->channel_type_);
}

void DiagnosticOutOfRangeFlag::setup() {
  ESP_LOGD(TAG, "Setting up Diagnostic Out Of Range Flag for channel type %d", this->channel_type_);
  this->publish_state(false);
}

void DiagnosticOutOfRangeFlag::dump_config() {
  LOG_BINARY_SENSOR("", "Diagnostic Out Of Range Flag", this);
  ESP_LOGCONFIG(TAG, "  Channel type: %d", this->channel_type_);
}

// ============================================================================
// Temperature Compensation Switch (Lot 5)
// ============================================================================

void TempCompensationSwitch::setup() {
  ESP_LOGD(TAG, "Setting up Temperature Compensation Switch for channel type %d", this->channel_type_);
  
  // Get initial state from channel configuration
  bool initial_state = false;
  if (this->parent_ != nullptr) {
    PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
    if (channel != nullptr) {
      initial_state = channel->is_temp_compensation_enabled();
    }
  }
  this->publish_state(initial_state);
}

void TempCompensationSwitch::dump_config() {
  LOG_SWITCH("", "Temperature Compensation Switch", this);
  ESP_LOGCONFIG(TAG, "  Channel type: %d", this->channel_type_);
}

void TempCompensationSwitch::write_state(bool state) {
  this->publish_state(state);
  
  if (this->parent_ != nullptr) {
    PoolStationChannelSensor *channel = this->parent_->get_channel(this->channel_type_);
    if (channel != nullptr) {
      channel->set_temp_compensation_enabled_runtime(state);
    }
  }
  
  ESP_LOGI(TAG, "Temperature compensation for channel type %d switched %s", 
           this->channel_type_, state ? "ON" : "OFF");
}

}  // namespace pool_station
}  // namespace esphome
