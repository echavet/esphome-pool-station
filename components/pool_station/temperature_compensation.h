#pragma once

#include <cmath>
#include <cstdint>
#include "esphome/core/log.h"

namespace esphome {
namespace pool_station {

static const char *const TEMPCOMP_TAG = "pool_station.tempcomp";

/**
 * Temperature Compensation — Lot 5
 * 
 * Provides temperature-compensated pH and ORP readings.
 * 
 * ## pH Temperature Compensation (Nernstian Model)
 * 
 * The Nernst equation describes the relationship between electrode potential
 * and pH. The theoretical slope is temperature-dependent:
 * 
 *   slope(T) = (2.303 × R × T) / F ≈ 59.16 × (T + 273.15) / 298.15  [mV/pH]
 * 
 * where:
 *   - R = 8.314 J/(mol·K) — Gas constant
 *   - F = 96485 C/mol — Faraday constant
 *   - T = Temperature in Kelvin
 * 
 * **Formula used:**
 * 
 *   pH_compensated = pH_neutral + (pH_measured - pH_neutral) × slope_ratio
 * 
 * where:
 *   - pH_neutral = Isopotential point (typically 7.0, where T has no effect)
 *   - slope_ratio = (T_ref + 273.15) / (T_measured + 273.15)
 *   - T_ref = Reference temperature (default 25°C, standard calibration temp)
 *   - T_measured = Current water temperature
 * 
 * **Example:**
 *   - pH measured: 7.50 at 30°C water
 *   - T_ref: 25°C
 *   - slope_ratio = 298.15 / 303.15 = 0.9835
 *   - pH_compensated = 7.0 + (7.50 - 7.0) × 0.9835 = 7.492
 * 
 * At pH = 7.0 (neutral), temperature compensation has no effect.
 * Away from pH 7.0, warmer water gives a slightly lower compensated pH.
 * 
 * ## ORP Temperature Compensation (Optional)
 * 
 * ORP compensation is less standardized. A simple linear model is provided
 * but disabled by default. The typical coefficient is small (~0.5-1.0 mV/°C).
 * 
 *   ORP_compensated = ORP_measured - α × (T_measured - T_ref)
 * 
 * where α is a user-configurable coefficient (default: 0, meaning disabled).
 * 
 * ## Configuration Parameters
 * 
 * - enabled: Runtime enable/disable (switch entity)
 * - reference_temperature: Calibration/standard temp (default 25°C)
 * - neutral_ph: Isopotential point (default 7.0)
 * - orp_coefficient: Linear ORP correction (default 0.0 = disabled)
 */

// Default values
static constexpr float DEFAULT_REFERENCE_TEMPERATURE = 25.0f;  // °C (standard calibration)
static constexpr float DEFAULT_NEUTRAL_PH = 7.0f;              // Isopotential point
static constexpr float DEFAULT_ORP_COEFFICIENT = 0.0f;         // mV/°C (0 = disabled)
static constexpr float ABSOLUTE_ZERO_OFFSET = 273.15f;         // Celsius to Kelvin

/**
 * Temperature compensation configuration for a channel.
 */
struct TemperatureCompensationConfig {
  bool enabled{false};                              // Runtime enable flag
  float reference_temperature{DEFAULT_REFERENCE_TEMPERATURE};  // T_ref in °C
  float neutral_ph{DEFAULT_NEUTRAL_PH};             // Isopotential point (pH only)
  float orp_coefficient{DEFAULT_ORP_COEFFICIENT};   // α in mV/°C (ORP only)
  
  // Validation
  bool is_valid() const {
    return reference_temperature > -40.0f && reference_temperature < 100.0f &&
           neutral_ph >= 0.0f && neutral_ph <= 14.0f;
  }
};

/**
 * Temperature compensation functions — pure, unit-testable.
 */
namespace temp_comp_functions {

/**
 * Compute Nernst slope ratio.
 * 
 * @param t_measured Current temperature in °C
 * @param t_ref Reference temperature in °C (default 25°C)
 * @return slope_ratio = (T_ref + 273.15) / (T_measured + 273.15)
 */
inline float compute_slope_ratio(float t_measured, float t_ref = DEFAULT_REFERENCE_TEMPERATURE) {
  float t_meas_k = t_measured + ABSOLUTE_ZERO_OFFSET;
  float t_ref_k = t_ref + ABSOLUTE_ZERO_OFFSET;
  
  if (t_meas_k <= 0.0f) {
    return 1.0f;  // Invalid temperature, no compensation
  }
  
  return t_ref_k / t_meas_k;
}

/**
 * Compensate pH for temperature using Nernstian model.
 * 
 * pH_compensated = pH_neutral + (pH_measured - pH_neutral) × slope_ratio
 * 
 * @param ph_measured Measured pH value
 * @param t_measured Current water temperature in °C
 * @param t_ref Reference temperature in °C (default 25°C)
 * @param ph_neutral Isopotential point (default 7.0)
 * @return Temperature-compensated pH
 */
inline float compensate_ph(float ph_measured, float t_measured, 
                           float t_ref = DEFAULT_REFERENCE_TEMPERATURE,
                           float ph_neutral = DEFAULT_NEUTRAL_PH) {
  if (std::isnan(ph_measured) || std::isnan(t_measured)) {
    return ph_measured;  // Return uncompensated if invalid input
  }
  
  float slope_ratio = compute_slope_ratio(t_measured, t_ref);
  float ph_deviation = ph_measured - ph_neutral;
  float ph_compensated = ph_neutral + (ph_deviation * slope_ratio);
  
  return ph_compensated;
}

/**
 * Compensate ORP for temperature using linear model.
 * 
 * ORP_compensated = ORP_measured - α × (T_measured - T_ref)
 * 
 * @param orp_measured Measured ORP in mV
 * @param t_measured Current water temperature in °C
 * @param t_ref Reference temperature in °C (default 25°C)
 * @param coefficient Temperature coefficient in mV/°C (default 0 = disabled)
 * @return Temperature-compensated ORP in mV
 */
inline float compensate_orp(float orp_measured, float t_measured,
                            float t_ref = DEFAULT_REFERENCE_TEMPERATURE,
                            float coefficient = DEFAULT_ORP_COEFFICIENT) {
  if (std::isnan(orp_measured) || std::isnan(t_measured)) {
    return orp_measured;  // Return uncompensated if invalid input
  }
  
  if (std::abs(coefficient) < 0.001f) {
    return orp_measured;  // Coefficient effectively zero, skip compensation
  }
  
  float delta_t = t_measured - t_ref;
  float orp_compensated = orp_measured - (coefficient * delta_t);
  
  return orp_compensated;
}

}  // namespace temp_comp_functions

/**
 * Temperature Compensator class.
 * 
 * Wraps configuration and provides compensate() method.
 */
class TemperatureCompensator {
 public:
  TemperatureCompensator() = default;
  
  // Configuration
  void set_enabled(bool enabled) { this->config_.enabled = enabled; }
  bool is_enabled() const { return this->config_.enabled; }
  
  void set_reference_temperature(float temp) { this->config_.reference_temperature = temp; }
  float get_reference_temperature() const { return this->config_.reference_temperature; }
  
  void set_neutral_ph(float ph) { this->config_.neutral_ph = ph; }
  float get_neutral_ph() const { return this->config_.neutral_ph; }
  
  void set_orp_coefficient(float coeff) { this->config_.orp_coefficient = coeff; }
  float get_orp_coefficient() const { return this->config_.orp_coefficient; }
  
  const TemperatureCompensationConfig &get_config() const { return this->config_; }
  
  /**
   * Compensate pH value for temperature.
   * 
   * @param ph_measured Measured pH value
   * @param t_water Current water temperature in °C
   * @return Compensated pH (or original if compensation disabled/invalid)
   */
  float compensate_ph(float ph_measured, float t_water) const {
    if (!this->config_.enabled || std::isnan(t_water)) {
      return ph_measured;
    }
    
    return temp_comp_functions::compensate_ph(
        ph_measured, 
        t_water,
        this->config_.reference_temperature,
        this->config_.neutral_ph);
  }
  
  /**
   * Compensate ORP value for temperature.
   * 
   * @param orp_measured Measured ORP in mV
   * @param t_water Current water temperature in °C
   * @return Compensated ORP (or original if compensation disabled/invalid)
   */
  float compensate_orp(float orp_measured, float t_water) const {
    if (!this->config_.enabled || std::isnan(t_water)) {
      return orp_measured;
    }
    
    return temp_comp_functions::compensate_orp(
        orp_measured,
        t_water,
        this->config_.reference_temperature,
        this->config_.orp_coefficient);
  }
  
  /**
   * Dump configuration for debugging.
   */
  void dump_config() const {
    ESP_LOGCONFIG(TEMPCOMP_TAG, "  Temperature Compensation:");
    ESP_LOGCONFIG(TEMPCOMP_TAG, "    Enabled: %s", this->config_.enabled ? "YES" : "NO");
    ESP_LOGCONFIG(TEMPCOMP_TAG, "    Reference temperature: %.1f °C", this->config_.reference_temperature);
    ESP_LOGCONFIG(TEMPCOMP_TAG, "    Neutral pH: %.1f", this->config_.neutral_ph);
    if (std::abs(this->config_.orp_coefficient) > 0.001f) {
      ESP_LOGCONFIG(TEMPCOMP_TAG, "    ORP coefficient: %.2f mV/°C", this->config_.orp_coefficient);
    } else {
      ESP_LOGCONFIG(TEMPCOMP_TAG, "    ORP coefficient: disabled");
    }
  }
  
 protected:
  TemperatureCompensationConfig config_;
};

}  // namespace pool_station
}  // namespace esphome
