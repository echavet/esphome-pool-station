#include "measurement_campaign.h"
#include "pool_station.h"
#include <cmath>

namespace esphome {
namespace pool_station {

static const char *const TAG = "pool_station.campaign";  // Required by LOG_* macros

// ============================================================================
// CampaignAction
// ============================================================================

void CampaignAction::execute() {
  if (this->switch_ref == nullptr) {
    ESP_LOGW(CAMPAIGN_TAG, "Action has null switch reference: %s", 
             this->switch_id.c_str());
    return;
  }
  
  bool target_state = (this->type == ACTION_SWITCH_ON);
  
  ESP_LOGI(CAMPAIGN_TAG, "Executing action: %s → %s",
           this->switch_id.c_str(), target_state ? "ON" : "OFF");
  
  if (target_state) {
    this->switch_ref->turn_on();
  } else {
    this->switch_ref->turn_off();
  }
}

void CampaignAction::capture_state() {
  if (this->switch_ref == nullptr) {
    return;
  }
  
  this->original_state = this->switch_ref->state;
  this->captured_original = true;
  
  ESP_LOGD(CAMPAIGN_TAG, "Captured original state: %s = %s",
           this->switch_id.c_str(), this->original_state ? "ON" : "OFF");
}

void CampaignAction::restore_state() {
  if (this->switch_ref == nullptr || !this->captured_original) {
    return;
  }
  
  ESP_LOGI(CAMPAIGN_TAG, "Restoring: %s → %s (original state)",
           this->switch_id.c_str(), this->original_state ? "ON" : "OFF");
  
  if (this->original_state) {
    this->switch_ref->turn_on();
  } else {
    this->switch_ref->turn_off();
  }
}

std::string CampaignAction::describe() const {
  std::string desc = this->switch_id;
  desc += " → ";
  desc += (this->type == ACTION_SWITCH_ON) ? "ON" : "OFF";
  return desc;
}

// ============================================================================
// CampaignResult
// ============================================================================

const CampaignSample *CampaignResult::get_sample(uint8_t channel_type) const {
  for (const auto &sample : this->samples) {
    if (sample.channel_type == channel_type) {
      return &sample;
    }
  }
  return nullptr;
}

// ============================================================================
// CampaignConfig
// ============================================================================

std::string CampaignConfig::validate() const {
  if (this->name.empty()) {
    return "Campaign name is required";
  }
  
  if (this->sample_channels.empty()) {
    return "At least one sample channel is required";
  }
  
  if (this->timeout_ms == 0) {
    return "Timeout must be > 0";
  }
  
  // Check that prepare actions have valid switch references
  for (const auto &action : this->prepare_actions) {
    if (action.switch_ref == nullptr) {
      return "Prepare action '" + action.switch_id + "' has no switch reference";
    }
  }
  
  return "";  // Valid
}

// ============================================================================
// MeasurementCampaign
// ============================================================================

void MeasurementCampaign::setup() {
  ESP_LOGI(CAMPAIGN_TAG, "Setting up campaign: %s", this->config_.name.c_str());
  
  // Validate configuration
  std::string error = this->config_.validate();
  if (!error.empty()) {
    ESP_LOGE(CAMPAIGN_TAG, "Campaign '%s' configuration error: %s",
             this->config_.name.c_str(), error.c_str());
    this->mark_failed();
    return;
  }
  
  // HIGH-01 fix: Validate that all sample channels exist
  if (this->parent_ != nullptr) {
    size_t valid_channels = 0;
    for (uint8_t channel_type : this->config_.sample_channels) {
      PoolStationChannelSensor *channel = this->parent_->get_channel(channel_type);
      if (channel == nullptr) {
        ESP_LOGW(CAMPAIGN_TAG, "Campaign '%s': sample channel type %d not found! "
                 "Campaign may fail to collect samples.",
                 this->config_.name.c_str(), channel_type);
      } else {
        valid_channels++;
      }
    }
    
    if (valid_channels == 0 && !this->config_.sample_channels.empty()) {
      ESP_LOGE(CAMPAIGN_TAG, "Campaign '%s': NO valid sample channels found! "
               "Campaign will complete with zero samples.",
               this->config_.name.c_str());
    }
  }
  
  ESP_LOGI(CAMPAIGN_TAG, "Campaign '%s' ready:", this->config_.name.c_str());
  ESP_LOGI(CAMPAIGN_TAG, "  Prepare actions: %zu", this->config_.prepare_actions.size());
  ESP_LOGI(CAMPAIGN_TAG, "  Delay: %u ms", this->config_.delay_ms);
  ESP_LOGI(CAMPAIGN_TAG, "  Sample channels: %zu", this->config_.sample_channels.size());
  ESP_LOGI(CAMPAIGN_TAG, "  Burst samples: %d (delay %u ms)", 
           this->config_.burst_samples, this->config_.burst_delay_ms);
  ESP_LOGI(CAMPAIGN_TAG, "  Restore: %s", this->config_.restore ? "YES" : "NO");
  ESP_LOGI(CAMPAIGN_TAG, "  Timeout: %u ms", this->config_.timeout_ms);
}

void MeasurementCampaign::loop() {
  if (this->state_ == CAMPAIGN_IDLE) {
    return;
  }
  
  // Check timeout
  if (this->check_timeout()) {
    return;
  }
  
  // Process current state
  this->process_state();
}

void MeasurementCampaign::dump_config() {
  ESP_LOGCONFIG(CAMPAIGN_TAG, "Measurement Campaign:");
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Name: %s", this->config_.name.c_str());
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Prepare actions: %zu", this->config_.prepare_actions.size());
  for (const auto &action : this->config_.prepare_actions) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "    - %s", action.describe().c_str());
  }
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Delay: %u ms", this->config_.delay_ms);
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Sample channels: %zu", this->config_.sample_channels.size());
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Burst: %d samples, %u ms delay", 
                this->config_.burst_samples, this->config_.burst_delay_ms);
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Restore: %s", this->config_.restore ? "YES" : "NO");
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Timeout: %u ms", this->config_.timeout_ms);
  if (this->config_.safety_gate != nullptr) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "  Safety gate: configured");
  }
  if (!this->config_.conditions_tag.empty()) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "  Conditions tag: %s", this->config_.conditions_tag.c_str());
  }
}

void MeasurementCampaign::add_prepare_action(const CampaignAction &action) {
  this->config_.prepare_actions.push_back(action);
}

void MeasurementCampaign::add_sample_channel(uint8_t channel_type) {
  this->config_.sample_channels.push_back(channel_type);
}

bool MeasurementCampaign::start() {
  if (this->state_ != CAMPAIGN_IDLE) {
    ESP_LOGW(CAMPAIGN_TAG, "Campaign '%s' already running (state: %s)",
             this->config_.name.c_str(), this->get_state_name());
    return false;
  }
  
  // Check safety gate
  if (this->config_.safety_gate != nullptr) {
    if (this->config_.safety_gate->is_blocked()) {
      const GateCondition *blocker = this->config_.safety_gate->get_blocking_condition();
      std::string reason = "Safety gate blocked";
      if (blocker != nullptr) {
        reason += " by " + blocker->describe();
      }
      ESP_LOGW(CAMPAIGN_TAG, "Campaign '%s' refused to start: %s",
               this->config_.name.c_str(), reason.c_str());
      return false;
    }
  }
  
  ESP_LOGI(CAMPAIGN_TAG, "========================================");
  ESP_LOGI(CAMPAIGN_TAG, "Starting campaign: %s", this->config_.name.c_str());
  ESP_LOGI(CAMPAIGN_TAG, "========================================");
  
  // Initialize result
  this->current_result_ = CampaignResult();
  this->current_result_.campaign_name = this->config_.name;
  this->current_result_.tag = this->config_.conditions_tag;
  this->current_result_.start_time_ms = millis();
  
  // Store campaign start time
  this->campaign_start_time_ms_ = millis();
  
  // Capture original states for restore
  for (auto &action : this->config_.prepare_actions) {
    action.capture_state();
  }
  
  // Transition to preparing state
  this->transition_to(CAMPAIGN_PREPARING);
  
  return true;
}

void MeasurementCampaign::abort(const std::string &reason) {
  if (this->state_ == CAMPAIGN_IDLE) {
    return;
  }
  
  ESP_LOGW(CAMPAIGN_TAG, "Campaign '%s' ABORTED: %s",
           this->config_.name.c_str(), reason.c_str());
  
  this->current_result_.aborted = true;
  this->current_result_.abort_reason = reason;
  
  // Restore actuators if configured
  if (this->config_.restore) {
    this->execute_restore_actions();
  }
  
  this->complete(false, reason);
}

const char *MeasurementCampaign::get_state_name() const {
  return campaign_functions::state_to_string(this->state_);
}

void MeasurementCampaign::transition_to(CampaignState new_state) {
  CampaignState old_state = this->state_;
  this->state_ = new_state;
  this->state_enter_time_ms_ = millis();
  
  ESP_LOGI(CAMPAIGN_TAG, "[%s] State: %s → %s",
           this->config_.name.c_str(),
           campaign_functions::state_to_string(old_state),
           campaign_functions::state_to_string(new_state));
  
  // Update running sensor
  if (this->running_sensor_ != nullptr) {
    this->running_sensor_->publish_state(new_state != CAMPAIGN_IDLE);
  }
  
  // Actions on entering new state
  switch (new_state) {
    case CAMPAIGN_PREPARING:
      this->execute_prepare_actions();
      // Immediately transition to waiting (actions are fire-and-forget)
      this->transition_to(CAMPAIGN_WAITING);
      break;
    
    case CAMPAIGN_WAITING:
      if (this->config_.delay_ms == 0) {
        // No delay, go straight to sampling
        this->transition_to(CAMPAIGN_SAMPLING);
      }
      break;
    
    case CAMPAIGN_SAMPLING:
      // Initialize burst sampling state
      this->current_burst_index_ = 0;
      this->current_channel_index_ = 0;
      this->last_burst_time_ms_ = 0;
      break;
    
    case CAMPAIGN_RESTORING:
      if (this->config_.restore) {
        this->execute_restore_actions();
      }
      // Complete after restore
      this->complete(true);
      break;
    
    case CAMPAIGN_IDLE:
      // Nothing to do
      break;
  }
}

void MeasurementCampaign::process_state() {
  uint32_t now = millis();
  uint32_t time_in_state = now - this->state_enter_time_ms_;
  
  switch (this->state_) {
    case CAMPAIGN_WAITING:
      // Check if delay has elapsed
      if (time_in_state >= this->config_.delay_ms) {
        ESP_LOGI(CAMPAIGN_TAG, "[%s] Delay complete (%u ms), starting sampling",
                 this->config_.name.c_str(), this->config_.delay_ms);
        this->transition_to(CAMPAIGN_SAMPLING);
      }
      break;
    
    case CAMPAIGN_SAMPLING:
      this->take_samples();
      break;
    
    default:
      // Other states handled in transition_to
      break;
  }
}

void MeasurementCampaign::execute_prepare_actions() {
  ESP_LOGI(CAMPAIGN_TAG, "[%s] Executing %zu prepare actions",
           this->config_.name.c_str(), this->config_.prepare_actions.size());
  
  for (auto &action : this->config_.prepare_actions) {
    action.execute();
  }
}

void MeasurementCampaign::execute_restore_actions() {
  ESP_LOGI(CAMPAIGN_TAG, "[%s] Restoring %zu actuators to original state",
           this->config_.name.c_str(), this->config_.prepare_actions.size());
  
  for (auto &action : this->config_.prepare_actions) {
    action.restore_state();
  }
}

void MeasurementCampaign::take_samples() {
  if (this->parent_ == nullptr) {
    ESP_LOGE(CAMPAIGN_TAG, "Cannot sample: no parent component");
    this->abort("no parent component");
    return;
  }
  
  uint32_t now = millis();
  
  // Check burst delay
  if (this->current_burst_index_ > 0 && 
      now - this->last_burst_time_ms_ < this->config_.burst_delay_ms) {
    return;  // Wait for burst delay
  }
  
  // Sample current channel (CRIT-02 fix: validate value before use)
  if (this->current_channel_index_ < this->config_.sample_channels.size()) {
    uint8_t channel_type = this->config_.sample_channels[this->current_channel_index_];
    
    PoolStationChannelSensor *channel = this->parent_->get_channel(channel_type);
    if (channel != nullptr) {
      // Use get_last_valid_value() instead of raw state to avoid stale/NaN values
      float value = channel->get_last_valid_value();
      
      // Validate: never record NaN values as campaign results
      if (std::isnan(value)) {
        ESP_LOGW(CAMPAIGN_TAG, "[%s] Channel %s has no valid value (NaN), skipping sample",
                 this->config_.name.c_str(),
                 channel->get_channel_type_name());
      } else {
        CampaignSample sample;
        sample.channel_type = channel_type;
        sample.value = value;
        sample.timestamp_ms = now;
        sample.tag = this->config_.conditions_tag;
        
        this->current_result_.samples.push_back(sample);
        
        ESP_LOGI(CAMPAIGN_TAG, "[%s] Sample: %s = %.3f (burst %d/%d)",
                 this->config_.name.c_str(),
                 channel->get_channel_type_name(),
                 value,
                 this->current_burst_index_ + 1,
                 this->config_.burst_samples);
      }
    } else {
      ESP_LOGW(CAMPAIGN_TAG, "[%s] Channel type %d not found, skipping",
               this->config_.name.c_str(), channel_type);
    }
    
    this->current_channel_index_++;
  }
  
  // Check if all channels sampled for this burst
  if (this->current_channel_index_ >= this->config_.sample_channels.size()) {
    this->current_channel_index_ = 0;
    this->current_burst_index_++;
    this->last_burst_time_ms_ = now;
    
    // Check if all bursts complete
    if (this->current_burst_index_ >= this->config_.burst_samples) {
      ESP_LOGI(CAMPAIGN_TAG, "[%s] Sampling complete (%d samples total)",
               this->config_.name.c_str(), 
               static_cast<int>(this->current_result_.samples.size()));
      this->transition_to(CAMPAIGN_RESTORING);
    }
  }
}

void MeasurementCampaign::complete(bool success, const std::string &error) {
  this->current_result_.success = success;
  this->current_result_.end_time_ms = millis();
  
  // Copy to last result
  this->last_result_ = this->current_result_;
  
  // Log completion
  if (success) {
    ESP_LOGI(CAMPAIGN_TAG, "========================================");
    ESP_LOGI(CAMPAIGN_TAG, "Campaign '%s' COMPLETE (duration: %u ms)",
             this->config_.name.c_str(), 
             this->last_result_.get_duration_ms());
    ESP_LOGI(CAMPAIGN_TAG, "  Samples collected: %zu",
             this->last_result_.samples.size());
    for (const auto &sample : this->last_result_.samples) {
      ESP_LOGI(CAMPAIGN_TAG, "    - Channel %d: %.3f", 
               sample.channel_type, sample.value);
    }
    ESP_LOGI(CAMPAIGN_TAG, "========================================");
  } else {
    ESP_LOGW(CAMPAIGN_TAG, "========================================");
    ESP_LOGW(CAMPAIGN_TAG, "Campaign '%s' FAILED: %s",
             this->config_.name.c_str(), error.c_str());
    ESP_LOGW(CAMPAIGN_TAG, "========================================");
  }
  
  // Transition to idle
  this->state_ = CAMPAIGN_IDLE;
  this->state_enter_time_ms_ = millis();
  
  // Update running sensor
  if (this->running_sensor_ != nullptr) {
    this->running_sensor_->publish_state(false);
  }
  
  // Call completion callback
  if (this->on_complete_callback_) {
    this->on_complete_callback_(this->last_result_);
  }
}

bool MeasurementCampaign::check_timeout() {
  if (this->state_ == CAMPAIGN_IDLE) {
    return false;
  }
  
  uint32_t elapsed = millis() - this->campaign_start_time_ms_;
  if (elapsed >= this->config_.timeout_ms) {
    ESP_LOGW(CAMPAIGN_TAG, "Campaign '%s' TIMEOUT after %u ms (limit: %u ms)",
             this->config_.name.c_str(), elapsed, this->config_.timeout_ms);
    
    this->current_result_.timed_out = true;
    this->abort("timeout");
    return true;
  }
  
  return false;
}

// ============================================================================
// CampaignStartButton
// ============================================================================

void CampaignStartButton::setup() {
  ESP_LOGD(CAMPAIGN_TAG, "Setting up Campaign Start Button");
}

void CampaignStartButton::dump_config() {
  LOG_BUTTON("", "Campaign Start Button", this);
  if (this->campaign_ != nullptr) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "  Campaign: %s", this->campaign_->get_name().c_str());
  }
}

void CampaignStartButton::press_action() {
  if (this->campaign_ == nullptr) {
    ESP_LOGW(CAMPAIGN_TAG, "Start button has no campaign assigned");
    return;
  }
  
  ESP_LOGI(CAMPAIGN_TAG, "Start button pressed for campaign: %s",
           this->campaign_->get_name().c_str());
  
  if (!this->campaign_->start()) {
    ESP_LOGW(CAMPAIGN_TAG, "Campaign refused to start");
  }
}

// ============================================================================
// CampaignAbortButton
// ============================================================================

void CampaignAbortButton::setup() {
  ESP_LOGD(CAMPAIGN_TAG, "Setting up Campaign Abort Button");
}

void CampaignAbortButton::dump_config() {
  LOG_BUTTON("", "Campaign Abort Button", this);
  if (this->campaign_ != nullptr) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "  Campaign: %s", this->campaign_->get_name().c_str());
  }
}

void CampaignAbortButton::press_action() {
  if (this->campaign_ == nullptr) {
    ESP_LOGW(CAMPAIGN_TAG, "Abort button has no campaign assigned");
    return;
  }
  
  ESP_LOGI(CAMPAIGN_TAG, "Abort button pressed for campaign: %s",
           this->campaign_->get_name().c_str());
  
  this->campaign_->abort("user abort via button");
}

// ============================================================================
// CampaignRunningSensor
// ============================================================================

void CampaignRunningSensor::setup() {
  ESP_LOGD(CAMPAIGN_TAG, "Setting up Campaign Running Sensor");
  this->publish_state(false);  // Initially not running
}

void CampaignRunningSensor::dump_config() {
  LOG_BINARY_SENSOR("", "Campaign Running", this);
  if (this->campaign_ != nullptr) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "  Campaign: %s", this->campaign_->get_name().c_str());
  }
}

void CampaignRunningSensor::update_state() {
  if (this->campaign_ == nullptr) {
    return;
  }
  
  bool running = this->campaign_->is_running();
  if (this->state != running) {
    this->publish_state(running);
  }
}

// ============================================================================
// CampaignResultSensor
// ============================================================================

void CampaignResultSensor::setup() {
  ESP_LOGD(CAMPAIGN_TAG, "Setting up Campaign Result Sensor (channel %d)", 
           this->channel_type_);
}

void CampaignResultSensor::dump_config() {
  LOG_SENSOR("", "Campaign Result", this);
  ESP_LOGCONFIG(CAMPAIGN_TAG, "  Channel type: %d", this->channel_type_);
  if (!this->tag_.empty()) {
    ESP_LOGCONFIG(CAMPAIGN_TAG, "  Tag filter: %s", this->tag_.c_str());
  }
}

void CampaignResultSensor::update_from_result(const CampaignResult &result) {
  // Check tag filter
  if (!this->tag_.empty() && result.tag != this->tag_) {
    return;  // Tag doesn't match filter
  }
  
  const CampaignSample *sample = result.get_sample(this->channel_type_);
  if (sample != nullptr && !std::isnan(sample->value)) {
    this->publish_state(sample->value);
    ESP_LOGD(CAMPAIGN_TAG, "Published campaign result: channel %d = %.3f",
             this->channel_type_, sample->value);
  }
}

// ============================================================================
// Campaign Helper Functions
// ============================================================================

namespace campaign_functions {

const char *state_to_string(CampaignState state) {
  switch (state) {
    case CAMPAIGN_IDLE: return "IDLE";
    case CAMPAIGN_PREPARING: return "PREPARING";
    case CAMPAIGN_WAITING: return "WAITING";
    case CAMPAIGN_SAMPLING: return "SAMPLING";
    case CAMPAIGN_RESTORING: return "RESTORING";
    default: return "UNKNOWN";
  }
}

}  // namespace campaign_functions

}  // namespace pool_station
}  // namespace esphome
