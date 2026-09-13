"""Shared calibration-algorithm select options.

Must stay in sync with CalibrationAlgorithmSelect::setup() in
calibration_ui.cpp. ESPHome's select.register_select() publishes this
list to Home Assistant at discovery; an empty list leaves the entity
as state `unknown` with `options: []`.

Only implemented calibration types are exposed. exponential /
logarithmic / power remain TODO in CalibrationEngine and are omitted
so the initial publish always uses an option that exists.
"""

ALGORITHM_SELECT_OPTIONS = (
    "none",
    "linear",
    "polynomial",
    "piecewise",
    "dfrobot_orp",
)
