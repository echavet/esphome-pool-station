"""Per-channel calibration-algorithm select options.

Must stay in sync with CalibrationAlgorithmSelect::setup() in
calibration_ui.cpp. ESPHome's select.register_select() publishes this
list to Home Assistant at discovery; an empty list leaves the entity
as state `unknown` with `options: []`.

dfrobot_orp is ORP-only (SEN0165-like mid/offset). pressure / pH expose
the generic implemented types. exponential / logarithmic / power remain
TODO in CalibrationEngine and are omitted so the initial publish always
uses an option that exists.
"""

# Channel types must match C++ ChannelType and __init__.py CHANNEL_TYPE_*
CHANNEL_TYPE_PRESSURE = 0
CHANNEL_TYPE_PH = 1
CHANNEL_TYPE_ORP = 2

# Implemented generic algorithms (CalibrationEngine supports these on any channel)
ALGORITHM_SELECT_OPTIONS_GENERIC = (
    "none",
    "linear",
    "polynomial",
    "piecewise",
)

# ORP additionally exposes dfrobot_orp; linear/polynomial/piecewise are
# also implemented in CalibrationEngine and remain available.
ALGORITHM_SELECT_OPTIONS_ORP = ALGORITHM_SELECT_OPTIONS_GENERIC + ("dfrobot_orp",)


def algorithm_select_options(channel_type):
    """Return non-empty HA select options for a ChannelType int.

    Args:
        channel_type: 0=pressure, 1=ph, 2=orp (must match C++ ChannelType).
    """
    if channel_type == CHANNEL_TYPE_ORP:
        return ALGORITHM_SELECT_OPTIONS_ORP
    return ALGORITHM_SELECT_OPTIONS_GENERIC
