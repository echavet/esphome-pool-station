#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace pool_station {
namespace interference_functions {

/** True when |current-previous| >= threshold. NaN or threshold<=0 → false. */
inline bool is_jump(float previous, float current, float threshold) {
  if (std::isnan(previous) || std::isnan(current) || !(threshold > 0.0f))
    return false;
  return std::fabs(current - previous) >= threshold;
}

/** Modular |a-b| for millis timestamps. Correct across wrap for dt < 2^31 ms. */
inline uint32_t time_delta_ms(uint32_t a, uint32_t b) {
  uint32_t d = a - b;
  if (d > 0x7FFFFFFFu)
    d = b - a;
  return d;
}

/** Later of two millis timestamps (wrap-safe for dt < 2^31 ms). */
inline uint32_t later_ms(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) >= 0 ? a : b; }

/** True when a is earlier than or equal to b (wrap-safe for dt < 2^31 ms). */
inline bool is_not_later(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) <= 0; }

/** True when two timestamps fall within window_ms (wrap-safe for dt < 2^31). */
inline bool coincident_in_window(uint32_t t_a, uint32_t t_b, uint32_t window_ms) {
  return time_delta_ms(t_a, t_b) <= window_ms;
}

/**
 * Pump/EMI pattern: both channels have a recorded jump, the two jump times
 * fall within window_ms of each other, and the later jump is still fresh
 * relative to now_ms (otherwise a one-shot pair would latch forever).
 */
inline bool coincident_jump(bool jump_a, uint32_t t_a, bool jump_b, uint32_t t_b, uint32_t now_ms,
                            uint32_t window_ms) {
  if (!jump_a || !jump_b)
    return false;
  if (!coincident_in_window(t_a, t_b, window_ms))
    return false;
  return coincident_in_window(now_ms, later_ms(t_a, t_b), window_ms);
}

/** Shared EMI: both rolling σ at or above their thresholds. */
inline bool shared_noise(float sigma_a, float sigma_b, float thresh_a, float thresh_b) {
  if (std::isnan(sigma_a) || std::isnan(sigma_b))
    return false;
  if (!(thresh_a > 0.0f) || !(thresh_b > 0.0f))
    return false;
  return sigma_a >= thresh_a && sigma_b >= thresh_b;
}

/**
 * Residual pH/Tw coupling: both moved at least their thresholds, same sign.
 * Compensated pH should rarely trip this if Lot 5 is well set.
 */
inline bool tw_coupling(float delta_ph, float delta_tw, float ph_threshold, float tw_threshold) {
  if (std::isnan(delta_ph) || std::isnan(delta_tw))
    return false;
  if (!(ph_threshold > 0.0f) || !(tw_threshold > 0.0f))
    return false;
  if (std::fabs(delta_ph) < ph_threshold || std::fabs(delta_tw) < tw_threshold)
    return false;
  return (delta_ph > 0.0f) == (delta_tw > 0.0f);
}

/** Population standard deviation; NaN if fewer than 2 valid samples. */
inline float stddev(const float *values, size_t count) {
  if (values == nullptr || count < 2)
    return NAN;
  double sum = 0.0;
  size_t n = 0;
  for (size_t i = 0; i < count; i++) {
    if (std::isnan(values[i]))
      continue;
    sum += values[i];
    n++;
  }
  if (n < 2)
    return NAN;
  const double mean = sum / static_cast<double>(n);
  double acc = 0.0;
  for (size_t i = 0; i < count; i++) {
    if (std::isnan(values[i]))
      continue;
    const double d = values[i] - mean;
    acc += d * d;
  }
  return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

}  // namespace interference_functions
}  // namespace pool_station
}  // namespace esphome
