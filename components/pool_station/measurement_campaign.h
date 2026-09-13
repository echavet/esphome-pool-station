#pragma once

#include "esphome/core/component.h"
#include "esphome/core/log.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "measurement_gate.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace esphome {
namespace pool_station {

// Forward declarations
class PoolStationComponent;
class PoolStationChannelSensor;
class MeasurementCampaign;

/**
 * Campaign state machine states.
 * 
 * Transitions: idle → preparing → waiting → sampling → restoring → idle
 *              (any state) → idle (on abort/timeout)
 */
enum CampaignState : uint8_t {
  CAMPAIGN_IDLE = 0,       // Not running, ready to start
  CAMPAIGN_PREPARING = 1,  // Executing prepare actions (turning switches on/off)
  CAMPAIGN_WAITING = 2,    // Waiting for delay before sampling
  CAMPAIGN_SAMPLING = 3,   // Taking burst samples on configured channels
  CAMPAIGN_RESTORING = 4,  // Restoring actuators to prior state
};

/**
 * Action type for prepare/restore phases.
 */
enum CampaignActionType : uint8_t {
  ACTION_SWITCH_ON = 0,
  ACTION_SWITCH_OFF = 1,
};

/**
 * A single action to perform during prepare or restore phase.
 */
struct CampaignAction {
  CampaignActionType type{ACTION_SWITCH_OFF};
  switch_::Switch *switch_ref{nullptr};
  std::string switch_id;  // For logging
  
  // For restore: the original state to restore to
  bool original_state{false};
  bool captured_original{false};
  
  /**
   * Execute this action.
   */
  void execute();
  
  /**
   * Capture current state for later restore.
   */
  void capture_state();
  
  /**
   * Restore to captured state.
   */
  void restore_state();
  
  /**
   * Get human-readable description.
   */
  std::string describe() const;
};

/**
 * Result of a single channel sample during a campaign.
 */
struct CampaignSample {
  uint8_t channel_type{0};
  float value{NAN};
  uint32_t timestamp_ms{0};
  std::string tag;  // Optional A/B tag (e.g., "filtration_off", "chlorine_on")
};

/**
 * Results of a completed campaign.
 */
struct CampaignResult {
  std::string campaign_name;
  std::string tag;  // Conditions tag (A/B testing)
  uint32_t start_time_ms{0};
  uint32_t end_time_ms{0};
  bool success{false};
  bool timed_out{false};
  bool aborted{false};
  std::string abort_reason;
  std::vector<CampaignSample> samples;
  
  /**
   * Get sample for a channel type.
   * @return pointer to sample or nullptr if not found
   */
  const CampaignSample *get_sample(uint8_t channel_type) const;
  
  /**
   * Get duration in milliseconds.
   */
  uint32_t get_duration_ms() const { 
    return this->end_time_ms > this->start_time_ms 
           ? this->end_time_ms - this->start_time_ms : 0; 
  }
};

/**
 * Configuration for a measurement campaign.
 * 
 * Eric's canonical scenario:
 *   - prepare: turn relay_filtration OFF
 *   - delay: 60s (wait for water to settle)
 *   - sample: pH, ORP channels
 *   - restore: turn relay_filtration back to prior state
 */
struct CampaignConfig {
  std::string name;
  
  // Prepare actions (executed before delay)
  std::vector<CampaignAction> prepare_actions;
  
  // Delay before sampling (milliseconds)
  uint32_t delay_ms{0};
  
  // Channels to sample (by channel type)
  std::vector<uint8_t> sample_channels;
  
  // Number of samples to take per channel (burst mode)
  uint8_t burst_samples{1};
  
  // Delay between burst samples (milliseconds)
  uint32_t burst_delay_ms{100};
  
  // Restore actuators to prior state after sampling
  bool restore{true};
  
  // Maximum campaign duration (timeout safety)
  uint32_t timeout_ms{120000};  // Default 2 minutes
  
  // Optional safety gate - refuse to start if this gate is blocked
  MeasurementGate *safety_gate{nullptr};
  
  // Optional conditions tag for A/B results
  std::string conditions_tag;
  
  /**
   * Validate configuration.
   * @return error message or empty string if valid
   */
  std::string validate() const;
};

/**
 * MeasurementCampaign - One-shot measurement sequence with actuator control.
 * 
 * State machine: idle → preparing → waiting → sampling → restoring → idle
 * 
 * Triggered by button press from Home Assistant. Exposes:
 *   - campaign_running binary_sensor
 *   - Optional tagged result sensors
 * 
 * Safety features:
 *   - Configurable timeout with automatic abort + restore
 *   - Optional safety gate that must be clear to start
 *   - State transitions logged at INFO level
 */
class MeasurementCampaign : public Component {
 public:
  MeasurementCampaign() = default;
  
  void setup() override;
  void loop() override;
  void dump_config() override;
  
  /**
   * Set configuration.
   */
  void set_config(const CampaignConfig &config) { this->config_ = config; }
  const CampaignConfig &get_config() const { return this->config_; }
  
  /**
   * Set parent component reference.
   */
  void set_parent(PoolStationComponent *parent) { this->parent_ = parent; }
  
  /**
   * Set campaign name.
   */
  void set_name(const std::string &name) { this->config_.name = name; }
  const std::string &get_name() const { return this->config_.name; }
  
  /**
   * Add a prepare action.
   */
  void add_prepare_action(const CampaignAction &action);
  
  /**
   * Add a channel to sample.
   */
  void add_sample_channel(uint8_t channel_type);
  
  /**
   * Set the delay before sampling.
   */
  void set_delay_ms(uint32_t delay_ms) { this->config_.delay_ms = delay_ms; }
  
  /**
   * Set burst sampling parameters.
   */
  void set_burst_samples(uint8_t count) { this->config_.burst_samples = count; }
  void set_burst_delay_ms(uint32_t delay_ms) { this->config_.burst_delay_ms = delay_ms; }
  
  /**
   * Set restore behavior.
   */
  void set_restore(bool restore) { this->config_.restore = restore; }
  
  /**
   * Set timeout.
   */
  void set_timeout_ms(uint32_t timeout_ms) { this->config_.timeout_ms = timeout_ms; }
  
  /**
   * Set safety gate.
   */
  void set_safety_gate(MeasurementGate *gate) { this->config_.safety_gate = gate; }
  
  /**
   * Set conditions tag for A/B results.
   */
  void set_conditions_tag(const std::string &tag) { this->config_.conditions_tag = tag; }
  
  /**
   * Start the campaign.
   * @return true if started, false if refused (already running, safety gate blocked, etc.)
   */
  bool start();
  
  /**
   * Abort the campaign and restore actuators.
   * @param reason Abort reason for logging
   */
  void abort(const std::string &reason = "manual abort");
  
  /**
   * Check if campaign is currently running.
   */
  bool is_running() const { return this->state_ != CAMPAIGN_IDLE; }
  
  /**
   * Get current state.
   */
  CampaignState get_state() const { return this->state_; }
  
  /**
   * Get state name as string.
   */
  const char *get_state_name() const;
  
  /**
   * Get last result (valid after campaign completes).
   */
  const CampaignResult &get_last_result() const { return this->last_result_; }
  
  /**
   * Register callback for campaign completion.
   */
  void set_on_complete_callback(std::function<void(const CampaignResult &)> callback) {
    this->on_complete_callback_ = std::move(callback);
  }
  
  /**
   * Set the campaign running binary sensor.
   */
  void set_running_sensor(binary_sensor::BinarySensor *sensor) { 
    this->running_sensor_ = sensor; 
  }

 protected:
  /**
   * Transition to a new state.
   */
  void transition_to(CampaignState new_state);
  
  /**
   * Process current state (called from loop).
   */
  void process_state();
  
  /**
   * Execute all prepare actions.
   */
  void execute_prepare_actions();
  
  /**
   * Restore all actuators to prior state.
   */
  void execute_restore_actions();
  
  /**
   * Take samples from configured channels.
   */
  void take_samples();
  
  /**
   * Complete the campaign (success or failure).
   */
  void complete(bool success, const std::string &error = "");
  
  /**
   * Check for timeout.
   */
  bool check_timeout();
  
  CampaignConfig config_;
  PoolStationComponent *parent_{nullptr};
  
  // State machine
  CampaignState state_{CAMPAIGN_IDLE};
  uint32_t state_enter_time_ms_{0};
  uint32_t campaign_start_time_ms_{0};
  
  // Burst sampling state
  uint8_t current_burst_index_{0};
  uint8_t current_channel_index_{0};
  uint32_t last_burst_time_ms_{0};
  
  // Results
  CampaignResult current_result_;
  CampaignResult last_result_;
  
  // Completion callback
  std::function<void(const CampaignResult &)> on_complete_callback_;
  
  // Running sensor
  binary_sensor::BinarySensor *running_sensor_{nullptr};
};

/**
 * CampaignStartButton - Triggers a measurement campaign.
 */
class CampaignStartButton : public button::Button, public Component {
 public:
  CampaignStartButton() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_campaign(MeasurementCampaign *campaign) { this->campaign_ = campaign; }

 protected:
  void press_action() override;
  
  MeasurementCampaign *campaign_{nullptr};
};

/**
 * CampaignAbortButton - Aborts a running campaign.
 */
class CampaignAbortButton : public button::Button, public Component {
 public:
  CampaignAbortButton() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_campaign(MeasurementCampaign *campaign) { this->campaign_ = campaign; }

 protected:
  void press_action() override;
  
  MeasurementCampaign *campaign_{nullptr};
};

/**
 * CampaignRunningSensor - Exposes campaign_running state.
 */
class CampaignRunningSensor : public binary_sensor::BinarySensor, public Component {
 public:
  CampaignRunningSensor() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_campaign(MeasurementCampaign *campaign) { this->campaign_ = campaign; }
  
  /**
   * Update state based on campaign.
   */
  void update_state();

 protected:
  MeasurementCampaign *campaign_{nullptr};
};

/**
 * CampaignResultSensor - Exposes last campaign sample for a channel.
 * 
 * Optional A/B tagged results: shows last sample under specific conditions.
 */
class CampaignResultSensor : public sensor::Sensor, public Component {
 public:
  CampaignResultSensor() = default;
  
  void setup() override;
  void dump_config() override;
  
  void set_campaign(MeasurementCampaign *campaign) { this->campaign_ = campaign; }
  void set_channel_type(uint8_t type) { this->channel_type_ = type; }
  void set_tag(const std::string &tag) { this->tag_ = tag; }
  
  /**
   * Update from campaign result.
   */
  void update_from_result(const CampaignResult &result);

 protected:
  MeasurementCampaign *campaign_{nullptr};
  uint8_t channel_type_{0};
  std::string tag_;  // Optional filter for A/B results
};

/**
 * Namespace for campaign helper functions.
 */
namespace campaign_functions {

/**
 * Get human-readable state name.
 */
const char *state_to_string(CampaignState state);

}  // namespace campaign_functions

}  // namespace pool_station
}  // namespace esphome
