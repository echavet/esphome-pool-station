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
    """ON at sat_on*FSR, OFF at sat_off*FSR, else keep previous."""
    if fsr_v <= 0.0 or abs_raw is None or math.isnan(abs_raw):
        return False
    if abs_raw >= sat_on * fsr_v:
        return True
    if abs_raw <= sat_off * fsr_v:
        return False
    return previous


def source_type_is_ads1115_sensor(type_name):
    if not type_name:
        return False
    name = str(type_name)
    return any(marker in name for marker in ADS1115_TYPE_MARKERS)


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


def ads_overlay_source_error(has_ads_block, source_type_name):
    """Return an error string if ads: is present on a non-ADS source."""
    if not has_ads_block:
        return None
    if source_type_is_ads1115_sensor(source_type_name):
        return None
    return ADS_OVERLAY_SOURCE_ERROR
