"""
ESPHome sensor platform for pool_station component.
Exposes calibrated and raw sensor values for pH, ORP, pressure.

Lot 1: This file maintains backward compatibility with Lot 0 role-based sensors,
but the preferred approach is now using channels in the main pool_station block.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_ICON,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_ACCURACY_DECIMALS,
    CONF_DEVICE_CLASS,
    CONF_STATE_CLASS,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_PRESSURE,
    STATE_CLASS_MEASUREMENT,
    UNIT_CELSIUS,
)
from . import (
    pool_station_ns,
    PoolStationComponent,
    CONF_POOL_STATION_ID,
)

DEPENDENCIES = ["pool_station"]

# Legacy sensor class for backward compatibility with Lot 0
PoolStationSensor = pool_station_ns.class_(
    "PoolStationSensor", sensor.Sensor, cg.Component
)

# Sensor role identifiers (must match C++ enum)
SENSOR_ROLE = {
    "water_temperature": 0,
    "air_pump": 1,
    "air_local": 2,
    "ph_raw": 10,
    "ph_calibrated": 11,
    "orp_raw": 20,
    "orp_calibrated": 21,
    "pressure_raw": 30,
    "pressure_calibrated": 31,
}

# Configuration keys
CONF_SENSOR_ROLE = "role"
CONF_PARENT_ID = "pool_station_id"

# Default sensor configurations by role
SENSOR_DEFAULTS = {
    "water_temperature": {
        "unit": UNIT_CELSIUS,
        "accuracy": 2,
        "device_class": DEVICE_CLASS_TEMPERATURE,
        "icon": "mdi:thermometer-water",
    },
    "air_pump": {
        "unit": UNIT_CELSIUS,
        "accuracy": 2,
        "device_class": DEVICE_CLASS_TEMPERATURE,
        "icon": "mdi:thermometer",
    },
    "air_local": {
        "unit": UNIT_CELSIUS,
        "accuracy": 2,
        "device_class": DEVICE_CLASS_TEMPERATURE,
        "icon": "mdi:thermometer",
    },
    "ph_raw": {
        "unit": "V",
        "accuracy": 3,
        "device_class": None,
        "icon": "mdi:flask",
    },
    "ph_calibrated": {
        "unit": "pH",
        "accuracy": 2,
        "device_class": None,
        "icon": "mdi:ph",
    },
    "orp_raw": {
        "unit": "V",
        "accuracy": 3,
        "device_class": None,
        "icon": "mdi:flash",
    },
    "orp_calibrated": {
        "unit": "mV",
        "accuracy": 0,
        "device_class": None,
        "icon": "mdi:flash",
    },
    "pressure_raw": {
        "unit": "V",
        "accuracy": 3,
        "device_class": None,
        "icon": "mdi:gauge",
    },
    "pressure_calibrated": {
        "unit": "bar",
        "accuracy": 2,
        "device_class": DEVICE_CLASS_PRESSURE,
        "icon": "mdi:gauge",
    },
}


def validate_sensor_role(value):
    """Validate sensor role is known."""
    value = cv.string_strict(value).lower()
    if value not in SENSOR_ROLE:
        raise cv.Invalid(
            f"Unknown sensor role '{value}'. Valid roles: {list(SENSOR_ROLE.keys())}"
        )
    return value


# Sensor schema (legacy Lot 0 style)
SENSOR_SCHEMA = sensor.sensor_schema(
    PoolStationSensor,
    state_class=STATE_CLASS_MEASUREMENT,
).extend({
    cv.GenerateID(): cv.declare_id(PoolStationSensor),
    cv.Required(CONF_PARENT_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_SENSOR_ROLE): validate_sensor_role,
})

CONFIG_SCHEMA = SENSOR_SCHEMA


async def to_code(config):
    """Generate C++ code for pool_station sensor (legacy)."""
    parent = await cg.get_variable(config[CONF_PARENT_ID])
    
    role_str = config[CONF_SENSOR_ROLE]
    role_value = SENSOR_ROLE[role_str]
    defaults = SENSOR_DEFAULTS.get(role_str, {})
    
    # Create sensor with role
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await sensor.register_sensor(var, config)
    
    # Set role and parent
    cg.add(var.set_role(role_value))
    cg.add(var.set_parent(parent))
    
    # Register sensor with parent component
    cg.add(parent.register_sensor(var, role_value))
