"""Lot A ADS overlay helpers — no ESPHome imports.

Mirrors ads_runtime.h (FSR table, hysteresis, PGA labels) so unit tests
can lock the contract without compiling firmware. Also validates that
``ads:`` is only used with a native ADS1115Sensor source.
"""

from __future__ import annotations

import math

# Must match ads1115::ADS1115Gain and ads_runtime.h
GAIN_UNMANAGED = 0xFF
GAIN_6P144 = 0b000
GAIN_4P096 = 0b001
GAIN_2P048 = 0b010
GAIN_1P024 = 0b011
GAIN_0P512 = 0b100
GAIN_0P256 = 0b101
GAIN_DEFAULT = GAIN_4P096

SAT_ON_DEFAULT = 0.98
SAT_OFF_DEFAULT = 0.95

# Non-empty HA select options (v0.7.12: empty list → unknown forever).
GAIN_SELECT_OPTIONS = (
    "6.144",
    "4.096",
    "2.048",
    "1.024",
    "0.512",
    "0.256",
)

GAIN_FLOAT_TO_CODE = {
    6.144: GAIN_6P144,
    4.096: GAIN_4P096,
    2.048: GAIN_2P048,
    1.024: GAIN_1P024,
    0.512: GAIN_0P512,
    0.256: GAIN_0P256,
}

GAIN_CODE_TO_FSR = {
    GAIN_6P144: 6.144,
    GAIN_4P096: 4.096,
    GAIN_2P048: 2.048,
    GAIN_1P024: 1.024,
    GAIN_0P512: 0.512,
    GAIN_0P256: 0.256,
}

GAIN_CODE_TO_LABEL = {
    GAIN_6P144: "6.144",
    GAIN_4P096: "4.096",
    GAIN_2P048: "2.048",
    GAIN_1P024: "1.024",
    GAIN_0P512: "0.512",
    GAIN_0P256: "0.256",
}

ADS1115_TYPE_MARKERS = (
    "ADS1115Sensor",
    "ads1115::ADS1115Sensor",
)

ADS_OVERLAY_SOURCE_ERROR = (
    "ads: overlay requires source_id to be a native platform: ads1115 "
    "sensor (ADS1115Sensor). Non-ADS sources cannot use the overlay."
)

RUNTIME_PREFS_MAGIC = 0xA0511115
CALIBRATION_PREFS_MAGIC = 0xCA110005

RT_FLAG_HAS_VMIN = 0x01
RT_FLAG_HAS_VMAX = 0x02
RT_FLAG_HAS_GAIN = 0x04

FILTER_SAMPLES_MAX = 20
MAX_JUMP_STREAK_MIN = 1
MAX_JUMP_STREAK_MAX = 10
UPDATE_INTERVAL_S_MIN = 1
UPDATE_INTERVAL_S_MAX = 3600
UPDATE_INTERVAL_MS_MIN = 1000
UPDATE_INTERVAL_MS_MAX = 3600 * 1000

# Must match ads_runtime.h FilterRuntimeNumberKind
FILTER_RT_SAMPLES = 0
FILTER_RT_MAX_JUMP = 1
FILTER_RT_MAX_JUMP_STREAK = 2
FILTER_RT_VALUE_MIN = 3
FILTER_RT_VALUE_MAX = 4
FILTER_RT_UPDATE_INTERVAL_S = 5

# Design §4.1: max_jump number step by channel (pH 0.05, ORP 1, bar 0.01)
FILTER_NUMBER_SPECS = {
    "pressure": {
        "jump_step": 0.01,
        "jump_max": 20.0,
        "value_min": -1.0,
        "value_max": 20.0,
        "clamp_step": 0.01,
    },
    "ph": {
        "jump_step": 0.05,
        "jump_max": 14.0,
        "value_min": 0.0,
        "value_max": 14.0,
        "clamp_step": 0.05,
    },
    "orp": {
        "jump_step": 1.0,
        "jump_max": 2000.0,
        "value_min": -2000.0,
        "value_max": 2000.0,
        "clamp_step": 1.0,
    },
}

# Packed sidecar: 4 + 6 + 2 pad + 3*4 + 4 + 4 = 32 (floats at offset 12).
CHANNEL_RUNTIME_PREFS_SIZE = 32
CHANNEL_RUNTIME_PREFS_FLOAT_OFFSET = 12
CHANNEL_RUNTIME_PREFS_FORMAT = "<I6B2x3fI4B"


def is_valid_gain_code(code):
    return 0 <= int(code) <= GAIN_0P256


def gain_float_to_code(value):
    """Map YAML gain (float or str) to the ADS1115Gain uint8 code."""
    numeric = float(value)
    for gain, code in GAIN_FLOAT_TO_CODE.items():
        if abs(numeric - gain) < 1e-6:
            return code
    raise ValueError(f"Unsupported ADS gain {value!r}; expected one of {GAIN_SELECT_OPTIONS}")


def fsr_volts_for_gain(code):
    if not is_valid_gain_code(code):
        return GAIN_CODE_TO_FSR[GAIN_DEFAULT]
    return GAIN_CODE_TO_FSR[int(code)]


def gain_code_to_label(code):
    return GAIN_CODE_TO_LABEL.get(int(code), "4.096")


def gain_label_to_code(label):
    for code, name in GAIN_CODE_TO_LABEL.items():
        if name == label:
            return code
    return GAIN_UNMANAGED


def fsr_percent(raw_v, fsr_v):
    if fsr_v <= 0.0 or raw_v is None or math.isnan(raw_v):
        return math.nan
    return 100.0 * abs(raw_v) / fsr_v


def above_sat_on(raw_v, fsr_v, sat_on=SAT_ON_DEFAULT):
    if fsr_v <= 0.0 or raw_v is None or math.isnan(raw_v):
        return False
    return abs(raw_v) >= sat_on * fsr_v


def saturation_hysteresis(abs_raw, fsr_v, sat_on, sat_off, previous):
    """ON at sat_on*FSR, OFF at sat_off*FSR, else keep previous.

    NaN raw or invalid FSR keep the previous latch so a missing sample
    does not clear a rail alarm.
    """
    if abs_raw is None or (isinstance(abs_raw, float) and math.isnan(abs_raw)):
        return previous
    if fsr_v <= 0.0:
        return previous
    if abs_raw >= sat_on * fsr_v:
        return True
    if abs_raw <= sat_off * fsr_v:
        return False
    return previous


def saturation_hold_expired(now_ms, last_on_ms, hold_ms):
    """uint32 millis wrap-safe hold expiry (mirrors ads_runtime.h)."""
    elapsed = (int(now_ms) - int(last_on_ms)) & 0xFFFFFFFF
    return elapsed >= int(hold_ms)


def source_type_is_ads1115_sensor(type_name):
    """Exact type name / suffix — not a substring (rejects FakeADS1115Sensor)."""
    if not type_name:
        return False
    name = str(type_name)
    if name in ADS1115_TYPE_MARKERS or name == "ADS1115Sensor":
        return True
    return name.endswith("::ADS1115Sensor") or name.endswith(".ADS1115Sensor")


def _try_import_ads1115_sensor():
    try:
        from esphome.components.ads1115.sensor import ADS1115Sensor

        return ADS1115Sensor
    except Exception:  # pragma: no cover - host tests have no ESPHome ads1115
        return None


def source_id_is_ads1115_sensor(source_id):
    """Prefer inherits_from(ADS1115Sensor) when the ESPHome type is importable."""
    if source_id is None:
        return False
    type_obj = getattr(source_id, "type", None)
    inherits = getattr(type_obj, "inherits_from", None)
    ads_cls = _try_import_ads1115_sensor()
    if callable(inherits) and ads_cls is not None:
        try:
            return bool(inherits(ads_cls))
        except Exception:
            return False
    return source_type_is_ads1115_sensor(id_type_name(source_id))


def id_type_name(source_id):
    """Best-effort type name from an ESPHome ID / mock."""
    if source_id is None:
        return ""
    type_obj = getattr(source_id, "type", None)
    if type_obj is None:
        return str(source_id)
    for attr in ("_name", "name", "__name__"):
        value = getattr(type_obj, attr, None)
        if isinstance(value, str) and value:
            return value
    return str(type_obj)


def is_managed_u8(value):
    return value is not None and int(value) != GAIN_UNMANAGED


def is_managed_float(value):
    if value is None:
        return False
    try:
        return not math.isnan(value)
    except TypeError:
        return True


def is_managed_interval_ms(ms):
    return ms is not None and int(ms) != 0


def clamp_filter_samples(n):
    n = int(n)
    if n < 0:
        return 0
    if n > FILTER_SAMPLES_MAX:
        return FILTER_SAMPLES_MAX
    return n


def clamp_max_jump_streak(s):
    s = int(s)
    if s < MAX_JUMP_STREAK_MIN:
        return MAX_JUMP_STREAK_MIN
    if s > MAX_JUMP_STREAK_MAX:
        return MAX_JUMP_STREAK_MAX
    return s


def clamp_max_jump(j):
    j = float(j)
    if math.isnan(j) or j < 0.0:
        return 0.0
    return j


def clamp_update_interval_ms(ms):
    ms = int(ms)
    if ms < UPDATE_INTERVAL_MS_MIN:
        return UPDATE_INTERVAL_MS_MIN
    if ms > UPDATE_INTERVAL_MS_MAX:
        return UPDATE_INTERVAL_MS_MAX
    return ms


def merge_nvs_filters(yaml_filters, nvs, persist=True):
    """YAML seeds; NVS wins per managed field when persist is on.

    Unmanaged sentinels (0xFF / NAN / interval 0) keep the YAML seed so a
    Lot A-only sidecar does not wipe Lot 3 filter defaults.
    """
    out = dict(yaml_filters)
    if not persist or not nvs:
        return out
    if is_managed_u8(nvs.get("filter_samples")):
        out["filter_samples"] = clamp_filter_samples(nvs["filter_samples"])
    if is_managed_float(nvs.get("max_jump")):
        out["max_jump"] = clamp_max_jump(nvs["max_jump"])
    if is_managed_u8(nvs.get("max_jump_streak")):
        out["max_jump_streak"] = clamp_max_jump_streak(nvs["max_jump_streak"])
    flags = int(nvs.get("flags", 0))
    if flags & RT_FLAG_HAS_VMIN:
        out["value_min"] = nvs.get("value_min")
    if flags & RT_FLAG_HAS_VMAX:
        out["value_max"] = nvs.get("value_max")
    if is_managed_interval_ms(nvs.get("update_interval_ms")):
        out["update_interval_ms"] = clamp_update_interval_ms(nvs["update_interval_ms"])
    return out


def pack_runtime_prefs(fields):
    """Pack ChannelRuntimePrefsData with the Lot A/B 32-byte layout."""
    import struct

    return struct.pack(
        CHANNEL_RUNTIME_PREFS_FORMAT,
        int(fields.get("magic", RUNTIME_PREFS_MAGIC)),
        int(fields.get("version", 1)),
        int(fields.get("ads_gain", GAIN_UNMANAGED)),
        int(fields.get("ads_sps", GAIN_UNMANAGED)),
        int(fields.get("filter_samples", GAIN_UNMANAGED)),
        int(fields.get("max_jump_streak", GAIN_UNMANAGED)),
        int(fields.get("flags", 0)),
        float(fields.get("max_jump", math.nan)),
        float(fields.get("value_min", math.nan)),
        float(fields.get("value_max", math.nan)),
        int(fields.get("update_interval_ms", 0)),
        int(fields.get("gain_at_last_cal_save", GAIN_UNMANAGED)),
        0,
        0,
        0,
    )


def unpack_runtime_prefs(blob):
    """Unpack a 32-byte sidecar. Keys match ChannelRuntimePrefsData."""
    import struct

    unpacked = struct.unpack(CHANNEL_RUNTIME_PREFS_FORMAT, blob)
    return {
        "magic": unpacked[0],
        "version": unpacked[1],
        "ads_gain": unpacked[2],
        "ads_sps": unpacked[3],
        "filter_samples": unpacked[4],
        "max_jump_streak": unpacked[5],
        "flags": unpacked[6],
        "max_jump": unpacked[7],
        "value_min": unpacked[8],
        "value_max": unpacked[9],
        "update_interval_ms": unpacked[10],
        "gain_at_last_cal_save": unpacked[11],
    }


class SlidingWindow:
    """Python mirror of channel_filter SlidingWindow (set_size clears)."""

    def __init__(self):
        self.size = 0
        self.head = 0
        self.count = 0
        self.buffer = []

    def set_size(self, size):
        self.size = int(size)
        self.buffer = [math.nan] * self.size
        self.head = 0
        self.count = 0

    def push(self, value):
        if self.size == 0:
            return
        self.buffer[self.head] = value
        self.head = (self.head + 1) % self.size
        if self.count < self.size:
            self.count += 1

    def is_full(self):
        return self.count >= self.size and self.size > 0


def ads_overlay_source_error(has_ads_block, source_type_name):
    """Return an error string if ads: is present on a non-ADS source."""
    if not has_ads_block:
        return None
    if source_type_is_ads1115_sensor(source_type_name):
        return None
    return ADS_OVERLAY_SOURCE_ERROR
