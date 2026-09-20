#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace esphome {
namespace pool_station {
namespace ads_runtime {

/** Sidecar NVS magic: "ADS" + version nibble. Do not reuse calibration magic. */
static constexpr uint32_t CHANNEL_RUNTIME_PREFS_MAGIC = 0xA0511115;
static constexpr uint8_t CHANNEL_RUNTIME_PREFS_VERSION = 1;

static constexpr uint8_t GAIN_UNMANAGED = 0xFF;

/** ESPHome ads1115::ADS1115Gain values (no getter upstream — we shadow these). */
static constexpr uint8_t GAIN_6P144 = 0b000;
static constexpr uint8_t GAIN_4P096 = 0b001;
static constexpr uint8_t GAIN_2P048 = 0b010;
static constexpr uint8_t GAIN_1P024 = 0b011;
static constexpr uint8_t GAIN_0P512 = 0b100;
static constexpr uint8_t GAIN_0P256 = 0b101;
static constexpr uint8_t GAIN_DEFAULT = GAIN_4P096;

static constexpr float SAT_ON_DEFAULT = 0.98f;
static constexpr float SAT_OFF_DEFAULT = 0.95f;
static constexpr uint32_t SAT_HOLD_MS_DEFAULT = 15000;

/** flags: bit0 has_vmin, bit1 has_vmax, bit2 has_gain (Lot A). */
static constexpr uint8_t RT_FLAG_HAS_VMIN = 0x01;
static constexpr uint8_t RT_FLAG_HAS_VMAX = 0x02;
static constexpr uint8_t RT_FLAG_HAS_GAIN = 0x04;

/**
 * Runtime sidecar (Lot A writes gain + flags + gain_at_last_cal_save).
 * Lot B may fill filter/interval fields. 0xFF / NAN = unmanaged.
 *
 * Packed but 4-byte aligned after the uint8 header: unaligned floats
 * fault on ESP32-C3/C6. pad_align_[2] makes sizeof == 32 (no production
 * NVS yet — layout change is safe on this PR).
 */
struct ChannelRuntimePrefsData {
  uint32_t magic;
  uint8_t version;
  uint8_t ads_gain;
  uint8_t ads_sps;
  uint8_t filter_samples;
  uint8_t max_jump_streak;
  uint8_t flags;
  uint8_t pad_align_[2];
  float max_jump;
  float value_min;
  float value_max;
  uint32_t update_interval_ms;
  uint8_t gain_at_last_cal_save;
  uint8_t reserved[3];
} __attribute__((packed));

static_assert(sizeof(ChannelRuntimePrefsData) == 32,
              "Lot A sidecar must be 32 bytes (4-byte aligned floats)");
static_assert(sizeof(ChannelRuntimePrefsData) % 4 == 0,
              "Lot A sidecar size must be a multiple of 4");
static_assert(offsetof(ChannelRuntimePrefsData, max_jump) == 12,
              "Lot A sidecar floats must start at offset 12");
static_assert(offsetof(ChannelRuntimePrefsData, max_jump) % 4 == 0,
              "Lot A sidecar floats must be 4-byte aligned");

inline bool is_valid_gain_code(uint8_t code) {
  return code <= GAIN_0P256;
}

inline bool is_managed_gain(uint8_t code) { return is_valid_gain_code(code); }

inline float fsr_volts_for_gain(uint8_t code) {
  switch (code) {
    case GAIN_6P144:
      return 6.144f;
    case GAIN_4P096:
      return 4.096f;
    case GAIN_2P048:
      return 2.048f;
    case GAIN_1P024:
      return 1.024f;
    case GAIN_0P512:
      return 0.512f;
    case GAIN_0P256:
      return 0.256f;
    default:
      return 4.096f;
  }
}

inline const char *gain_code_to_label(uint8_t code) {
  switch (code) {
    case GAIN_6P144:
      return "6.144";
    case GAIN_4P096:
      return "4.096";
    case GAIN_2P048:
      return "2.048";
    case GAIN_1P024:
      return "1.024";
    case GAIN_0P512:
      return "0.512";
    case GAIN_0P256:
      return "0.256";
    default:
      return "4.096";
  }
}

inline uint8_t gain_label_to_code(const char *label) {
  if (label == nullptr) {
    return GAIN_UNMANAGED;
  }
  if (std::strcmp(label, "6.144") == 0)
    return GAIN_6P144;
  if (std::strcmp(label, "4.096") == 0)
    return GAIN_4P096;
  if (std::strcmp(label, "2.048") == 0)
    return GAIN_2P048;
  if (std::strcmp(label, "1.024") == 0)
    return GAIN_1P024;
  if (std::strcmp(label, "0.512") == 0)
    return GAIN_0P512;
  if (std::strcmp(label, "0.256") == 0)
    return GAIN_0P256;
  return GAIN_UNMANAGED;
}

inline float fsr_percent(float raw_v, float fsr_v) {
  if (fsr_v <= 0.0f || std::isnan(raw_v)) {
    return NAN;
  }
  return 100.0f * std::fabs(raw_v) / fsr_v;
}

inline bool above_sat_on(float raw_v, float fsr_v, float sat_on) {
  if (fsr_v <= 0.0f || std::isnan(raw_v)) {
    return false;
  }
  return std::fabs(raw_v) >= sat_on * fsr_v;
}

/** Hysteresis: ON at sat_on*FSR, OFF at sat_off*FSR, else keep previous.
 *  NaN raw or invalid FSR keep the previous latch (do not clear a rail alarm
 *  because of a missing sample).
 */
inline bool saturation_hysteresis(float abs_raw, float fsr_v, float sat_on, float sat_off, bool previous) {
  if (fsr_v <= 0.0f || std::isnan(abs_raw)) {
    return previous;
  }
  if (abs_raw >= sat_on * fsr_v) {
    return true;
  }
  if (abs_raw <= sat_off * fsr_v) {
    return false;
  }
  return previous;
}

/** uint32 millis wrap-safe: true when hold_ms has elapsed since last_on_ms. */
inline bool saturation_hold_expired(uint32_t now_ms, uint32_t last_on_ms, uint32_t hold_ms) {
  return (now_ms - last_on_ms) >= hold_ms;
}

inline void init_unmanaged(ChannelRuntimePrefsData *data) {
  if (data == nullptr) {
    return;
  }
  std::memset(data, 0, sizeof(*data));
  data->magic = CHANNEL_RUNTIME_PREFS_MAGIC;
  data->version = CHANNEL_RUNTIME_PREFS_VERSION;
  data->ads_gain = GAIN_UNMANAGED;
  data->ads_sps = GAIN_UNMANAGED;
  data->filter_samples = GAIN_UNMANAGED;
  data->max_jump_streak = GAIN_UNMANAGED;
  data->flags = 0;
  data->max_jump = NAN;
  data->value_min = NAN;
  data->value_max = NAN;
  data->update_interval_ms = 0;
  data->gain_at_last_cal_save = GAIN_UNMANAGED;
}

}  // namespace ads_runtime
}  // namespace pool_station
}  // namespace esphome

#ifdef USE_ADS1115
#include "esphome/components/ads1115/ads1115.h"
static_assert(static_cast<uint8_t>(esphome::ads1115::ADS1115_GAIN_6P144) ==
                  esphome::pool_station::ads_runtime::GAIN_6P144,
              "ADS1115_GAIN_6P144 != Lot A GAIN_6P144");
static_assert(static_cast<uint8_t>(esphome::ads1115::ADS1115_GAIN_4P096) ==
                  esphome::pool_station::ads_runtime::GAIN_4P096,
              "ADS1115_GAIN_4P096 != Lot A GAIN_4P096");
static_assert(static_cast<uint8_t>(esphome::ads1115::ADS1115_GAIN_2P048) ==
                  esphome::pool_station::ads_runtime::GAIN_2P048,
              "ADS1115_GAIN_2P048 != Lot A GAIN_2P048");
static_assert(static_cast<uint8_t>(esphome::ads1115::ADS1115_GAIN_1P024) ==
                  esphome::pool_station::ads_runtime::GAIN_1P024,
              "ADS1115_GAIN_1P024 != Lot A GAIN_1P024");
static_assert(static_cast<uint8_t>(esphome::ads1115::ADS1115_GAIN_0P512) ==
                  esphome::pool_station::ads_runtime::GAIN_0P512,
              "ADS1115_GAIN_0P512 != Lot A GAIN_0P512");
static_assert(static_cast<uint8_t>(esphome::ads1115::ADS1115_GAIN_0P256) ==
                  esphome::pool_station::ads_runtime::GAIN_0P256,
              "ADS1115_GAIN_0P256 != Lot A GAIN_0P256");
#endif
