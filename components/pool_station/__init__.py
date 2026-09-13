"""
ESPHome external component: pool_station
Rich calibration and diagnostics platform for pool monitoring.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_UPDATE_INTERVAL,
)
from esphome.core import coroutine

CODEOWNERS = ["@echavet"]
MULTI_CONF = False

AUTO_LOAD = ["sensor"]
DEPENDENCIES = []

# Namespace for pool_station C++ code
pool_station_ns = cg.esphome_ns.namespace("pool_station")

# Main component class
PoolStationComponent = pool_station_ns.class_(
    "PoolStationComponent", cg.Component
)

# Configuration keys
CONF_POOL_STATION_ID = "pool_station_id"

# Calibration algorithm modes (for future lots)
CONF_CALIBRATION = "calibration"
CONF_CALIBRATION_MODE = "mode"
CONF_CALIBRATION_POINTS = "points"

CALIBRATION_MODES = {
    "none": 0,
    "linear": 1,
    "polynomial": 2,
    "exponential": 3,
    "logarithmic": 4,
    "power": 5,
    "piecewise": 6,
    "dfrobot_orp": 7,  # SEN0165-like mid/offset
}

# Channel configuration (for future lots)
CONF_CHANNELS = "channels"
CONF_CHANNEL_RAW = "raw"
CONF_CHANNEL_PH = "ph"
CONF_CHANNEL_ORP = "orp"
CONF_CHANNEL_PRESSURE = "pressure"

# Temperature compensation (for future lots)
CONF_WATER_TEMPERATURE = "water_temperature"
CONF_WATER_TEMPERATURE_SENSOR_ID = "sensor_id"

# Main CONFIG_SCHEMA for pool_station platform
CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(PoolStationComponent),
        cv.Optional(CONF_UPDATE_INTERVAL, default="1s"): cv.update_interval,
        # Future: calibration configuration
        cv.Optional(CONF_CALIBRATION): cv.Schema(
            {
                cv.Optional(CONF_CALIBRATION_MODE, default="none"): cv.enum(
                    CALIBRATION_MODES, lower=True
                ),
                # Calibration points placeholder for Lot 2
                # cv.Optional(CONF_CALIBRATION_POINTS): cv.ensure_list(...),
            }
        ),
        # Future: water temperature sensor for compensation
        cv.Optional(CONF_WATER_TEMPERATURE): cv.Schema(
            {
                cv.Optional(CONF_WATER_TEMPERATURE_SENSOR_ID): cv.use_id(
                    cg.esphome_ns.class_("sensor::Sensor")
                ),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


def to_code(config):
    """Generate C++ code for pool_station component."""
    var = cg.new_Pvariable(config[CONF_ID])
    yield cg.register_component(var, config)

    # Set update interval
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))

    # Process calibration config (stub for now)
    if CONF_CALIBRATION in config:
        cal_config = config[CONF_CALIBRATION]
        mode = cal_config.get(CONF_CALIBRATION_MODE, "none")
        mode_value = CALIBRATION_MODES.get(mode, 0)
        cg.add(var.set_calibration_mode(mode_value))

    # Water temperature sensor binding (stub for future)
    if CONF_WATER_TEMPERATURE in config:
        wt_config = config[CONF_WATER_TEMPERATURE]
        if CONF_WATER_TEMPERATURE_SENSOR_ID in wt_config:
            wt_sensor = yield cg.get_variable(wt_config[CONF_WATER_TEMPERATURE_SENSOR_ID])
            cg.add(var.set_water_temperature_sensor(wt_sensor))
