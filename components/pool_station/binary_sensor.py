"""
ESPHome binary_sensor platform for pool_station calibration status.
Provides cal_invalid indicator when calibration is insufficient for selected algorithm.

Lot 2: Calibration validation feedback.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import (
    CONF_ID,
    CONF_ICON,
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_DIAGNOSTIC,
    DEVICE_CLASS_PROBLEM,
)
from . import (
    pool_station_ns,
    PoolStationComponent,
    CONF_POOL_STATION_ID,
)

DEPENDENCIES = ["pool_station"]

# Binary sensor class for calibration invalid indicator
CalibrationInvalidSensor = pool_station_ns.class_(
    "CalibrationInvalidSensor", binary_sensor.BinarySensor, cg.Component
)

AdsSaturatedBinarySensor = pool_station_ns.class_(
    "AdsSaturatedBinarySensor", binary_sensor.BinarySensor, cg.Component
)
AdsGainMismatchSensor = pool_station_ns.class_(
    "AdsGainMismatchSensor", binary_sensor.BinarySensor, cg.Component
)

# Configuration keys
CONF_CHANNEL = "channel"

# Channel name to type mapping (must match C++)
CHANNEL_NAME_TO_TYPE = {
    "pressure": 0,
    "ph": 1,
    "orp": 2,
}

# Schema for calibration invalid sensor
CALIBRATION_INVALID_SCHEMA = binary_sensor.binary_sensor_schema(
    CalibrationInvalidSensor,
    icon="mdi:alert-circle",
    entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    device_class=DEVICE_CLASS_PROBLEM,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationInvalidSensor),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
})

CONFIG_SCHEMA = CALIBRATION_INVALID_SCHEMA


async def to_code(config):
    """Generate C++ code for calibration invalid sensor - used by internal schema."""
    pass


async def new_calibration_invalid_sensor(config, parent, channel_type):
    """Create a calibration invalid binary sensor entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await binary_sensor.register_binary_sensor(var, config)
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    
    return var
