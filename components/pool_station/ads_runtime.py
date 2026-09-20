"""Lot A/B ADS overlay + filter runtime helpers — no ESPHome imports.

Mirrors ads_runtime.h (FSR table, hysteresis, PGA labels, filter clamps,
sidecar stamp) so unit tests can lock the contract without compiling
firmware. Also validates that ``ads:`` is only used with a native
ADS1115Sensor source.
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

# cv.use_id(sensor.Sensor) types compose-model IDs as these, never ADS1115Sensor.
GENERIC_SENSOR_TYPE_NAMES = (
    "Sensor",
    "sensor.Sensor",
    "sensor::Sensor",
    "esphome::sensor::Sensor",
)

ADS1115_PLATFORM = "ads1115"

# ESPHome 2026.x ads1115 hub does not emit this; Lot A C++ is #ifdef'd on it.
USE_ADS1115_DEFINE = "USE_ADS1115"

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

# NVS deferred flush debounce period (v0.10.4)
NVS_FLUSH_DEBOUNCE_MS = 3000

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


def is_generic_sensor_type_name(type_name):
    """True for the compose-model ID type produced by cv.use_id(sensor.Sensor).

    Also treats unknown/repr-like names as generic so channel-schema validation
    can defer to FINAL_VALIDATE (CORE.config is complete then).
    """
    if not type_name:
        return True
    name = str(type_name)
    if name in GENERIC_SENSOR_TYPE_NAMES:
        return True
    if name.startswith("<") or " object" in name or "MockType" in name:
        return True
    return name.endswith("::Sensor") and not source_type_is_ads1115_sensor(name)


def source_platform_is_ads1115(platform):
    """Exact YAML platform name — not a substring (rejects fake_ads1115)."""
    if platform is None:
        return False
    return str(platform).strip().lower() == ADS1115_PLATFORM


def normalize_config_id(obj):
    """String id from an ESPHome ID object, mock, or YAML scalar."""
    if obj is None:
        return ""
    if isinstance(obj, str):
        return obj.strip()
    for attr in ("id", "id_"):
        value = getattr(obj, attr, None)
        if isinstance(value, str) and value:
            return value
        if value is not None and not callable(value):
            nested = getattr(value, "id", None)
            if isinstance(nested, str) and nested:
                return nested
            text = str(value).strip()
            if text:
                return text
    return str(obj).strip()


def try_core_sensor_entries():
    """Sensor dicts from ESPHome CORE.raw_config / CORE.config (empty off-host)."""
    entries = []
    try:
        from esphome.core import CORE
    except Exception:
        return entries
    seen = set()
    for blob in (getattr(CORE, "raw_config", None), getattr(CORE, "config", None)):
        if not isinstance(blob, dict):
            continue
        sensors = blob.get("sensor")
        if sensors is None:
            continue
        if isinstance(sensors, dict):
            sensors = [sensors]
        try:
            iterable = list(sensors)
        except TypeError:
            continue
        for entry in iterable:
            if not isinstance(entry, dict):
                continue
            marker = id(entry)
            if marker in seen:
                continue
            seen.add(marker)
            entries.append(entry)
    return entries


def sensor_platform_for_id(source_id, sensor_entries=None):
    """Return the YAML ``platform:`` for ``source_id``, or None if unknown."""
    wanted = normalize_config_id(source_id)
    if not wanted:
        return None
    if sensor_entries is None:
        sensor_entries = try_core_sensor_entries()
    if not sensor_entries:
        return None
    if isinstance(sensor_entries, dict):
        sensor_entries = [sensor_entries]
    for entry in sensor_entries:
        if not isinstance(entry, dict):
            continue
        if normalize_config_id(entry.get("id")) == wanted:
            platform = entry.get("platform")
            if platform is not None:
                return platform
    return None


def _try_import_ads1115_sensor():
    try:
        from esphome.components.ads1115.sensor import ADS1115Sensor

        return ADS1115Sensor
    except Exception:  # pragma: no cover - host tests have no ESPHome ads1115
        return None


def source_id_is_ads1115_sensor(source_id, platform=None, sensor_entries=None):
    """True if source_id is a native ADS1115 sensor.

    ``inherits_from(ADS1115Sensor)`` is used only when it returns True.
    False or an exception must fall through: ``cv.use_id(sensor.Sensor)``
    types compose-model IDs as ``Sensor``, so inherits is False even for
    a real ``platform: ads1115`` source. Next checks are type-name, then
    the YAML platform from ``sensor_entries`` / CORE.config.
    """
    if source_id is None:
        return False
    type_obj = getattr(source_id, "type", None)
    inherits = getattr(type_obj, "inherits_from", None)
    ads_cls = _try_import_ads1115_sensor()
    if callable(inherits) and ads_cls is not None:
        try:
            if bool(inherits(ads_cls)):
                return True
        except Exception:
            pass
    if source_type_is_ads1115_sensor(id_type_name(source_id)):
        return True
    if platform is None:
        platform = sensor_platform_for_id(source_id, sensor_entries)
    return source_platform_is_ads1115(platform)


def ads_overlay_channel_error(channel, sensor_entries=None):
    """Return ADS_OVERLAY_SOURCE_ERROR if ads: is set on a non-ADS source."""
    if not channel or not channel.get("ads"):
        return None
    source = channel.get("source_id")
    if source_id_is_ads1115_sensor(source, sensor_entries=sensor_entries):
        return None
    return ADS_OVERLAY_SOURCE_ERROR


def iter_channel_configs(config):
    """Yield channel dicts from a pool_station config (pressure / ph / orp)."""
    channels = (config or {}).get("channels") or {}
    if isinstance(channels, dict):
        return [ch for ch in channels.values() if isinstance(ch, dict)]
    if isinstance(channels, list):
        return [ch for ch in channels if isinstance(ch, dict)]
    return []


def config_needs_use_ads1115(config, sensor_entries=None):
    """True when Lot A (ads:) or a composed platform: ads1115 source is present.

    Used to emit ``USE_ADS1115`` without a hard ``DEPENDENCIES = ["ads1115"]``.
    """
    for ch in iter_channel_configs(config):
        if ch.get("ads") is not None:
            return True
        if source_id_is_ads1115_sensor(ch.get("source_id"), sensor_entries=sensor_entries):
            return True
    return False


def ensure_use_ads1115_define(config, add_define, sensor_entries=None):
    """Call ``add_define("USE_ADS1115")`` when Lot A / ads1115 sources need it.

    ESPHome 2026.x ``ads1115`` ``to_code`` does not ``cg.add_define(USE_ADS1115)``,
    so Lot A C++ (bind / FSR / saturation / set_gain) compiled out on Eric
    v0.10.2. Builds that omit ads1115 entirely stay clean (no define, no
    hard dependency). Returns True if the define was added.
    """
    if add_define is None:
        return False
    if sensor_entries is None:
        sensor_entries = try_core_sensor_entries()
    if not config_needs_use_ads1115(config, sensor_entries=sensor_entries):
        return False
    add_define(USE_ADS1115_DEFINE)
    return True


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


def clamp_bounds_valid(vmin, vmax):
    """NAN / missing bounds are unbounded; inverted finite min>max is invalid."""
    if vmin is None or vmax is None:
        return True
    try:
        if math.isnan(vmin) or math.isnan(vmax):
            return True
    except TypeError:
        return True
    return float(vmin) <= float(vmax)


def sanitize_update_interval_ms(ms, ha_range=True):
    """HA apply uses 1–3600 s. YAML / NVS restore may keep a shorter seed."""
    ms = int(ms)
    if ha_range:
        return clamp_update_interval_ms(ms)
    if ms > UPDATE_INTERVAL_MS_MAX:
        return UPDATE_INTERVAL_MS_MAX
    return ms


def validate_filter_runtime_seeds(filters_conf):
    """Return an error string or None. runtime value_min/max need YAML seeds."""
    if not filters_conf:
        return None
    if "value_min" in filters_conf and "value_max" in filters_conf:
        if not clamp_bounds_valid(filters_conf.get("value_min"), filters_conf.get("value_max")):
            return "filters.value_min must be <= filters.value_max (clamp bounds)"
    rt = filters_conf.get("runtime")
    if not rt:
        return None
    if "value_min" in rt and "value_min" not in filters_conf:
        return (
            "filters.runtime.value_min requires filters.value_min YAML seed "
            "(omit both to leave clamp off; NAN cannot be a HA number)"
        )
    if "value_max" in rt and "value_max" not in filters_conf:
        return (
            "filters.runtime.value_max requires filters.value_max YAML seed "
            "(omit both to leave clamp off; NAN cannot be a HA number)"
        )
    return None


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


def merge_nvs_filters(yaml_filters, nvs, persist=True, persist_filters=None, persist_interval=None):
    """YAML seeds; NVS wins per managed field when persist is on.

    Unmanaged sentinels (0xFF / NAN / interval 0) keep the YAML seed so a
    Lot A-only sidecar does not wipe Lot 3 filter defaults. HAS_VMIN/HAS_VMAX
    without a finite value are treated as unmanaged (do not disable YAML clamp).
    """
    if persist_filters is None:
        persist_filters = persist
    if persist_interval is None:
        persist_interval = persist
    out = dict(yaml_filters)
    if not nvs:
        return out
    if persist_filters:
        if is_managed_u8(nvs.get("filter_samples")):
            out["filter_samples"] = clamp_filter_samples(nvs["filter_samples"])
        if is_managed_float(nvs.get("max_jump")):
            out["max_jump"] = clamp_max_jump(nvs["max_jump"])
        if is_managed_u8(nvs.get("max_jump_streak")):
            out["max_jump_streak"] = clamp_max_jump_streak(nvs["max_jump_streak"])
        flags = int(nvs.get("flags", 0))
        if (flags & RT_FLAG_HAS_VMIN) and is_managed_float(nvs.get("value_min")):
            out["value_min"] = nvs.get("value_min")
        if (flags & RT_FLAG_HAS_VMAX) and is_managed_float(nvs.get("value_max")):
            out["value_max"] = nvs.get("value_max")
    if persist_interval and is_managed_interval_ms(nvs.get("update_interval_ms")):
        out["update_interval_ms"] = sanitize_update_interval_ms(
            nvs["update_interval_ms"], ha_range=False
        )
    return out


def stamp_runtime_prefs(cache, live, stamp_filters=False, stamp_interval=False, ads_overlay=False):
    """Mirror save_runtime_preferences: start from cache, stamp only requested fields.

    Gain Save / remember_gain pass stamp_filters=False so unmanaged Lot B
    sentinels stay unmanaged (design §4.2: Save cal does not write filters).
    """
    out = dict(cache)
    out["magic"] = RUNTIME_PREFS_MAGIC
    out["version"] = 1
    flags = int(out.get("flags", 0))
    if ads_overlay:
        out["ads_gain"] = live.get("ads_gain", out.get("ads_gain", GAIN_UNMANAGED))
        flags |= RT_FLAG_HAS_GAIN
        out["gain_at_last_cal_save"] = live.get(
            "gain_at_last_cal_save", out.get("gain_at_last_cal_save", GAIN_UNMANAGED)
        )
    if stamp_filters:
        out["filter_samples"] = clamp_filter_samples(live["filter_samples"])
        out["max_jump_streak"] = clamp_max_jump_streak(live["max_jump_streak"])
        out["max_jump"] = clamp_max_jump(live["max_jump"])
        if is_managed_float(live.get("value_min")):
            flags |= RT_FLAG_HAS_VMIN
            out["value_min"] = live["value_min"]
        else:
            flags &= ~RT_FLAG_HAS_VMIN
            out["value_min"] = math.nan
        if is_managed_float(live.get("value_max")):
            flags |= RT_FLAG_HAS_VMAX
            out["value_max"] = live["value_max"]
        else:
            flags &= ~RT_FLAG_HAS_VMAX
            out["value_max"] = math.nan
    if stamp_interval:
        out["update_interval_ms"] = live["update_interval_ms"]
    out["flags"] = flags
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
