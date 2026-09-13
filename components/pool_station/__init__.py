"""
ESPHome external component: pool_station
Rich calibration and diagnostics platform for pool monitoring.

Lot 1: ADS1115 channel binding, raw values, calibration mode switch.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, switch
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_UPDATE_INTERVAL,
    CONF_ICON,
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_CONFIG,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_VOLT,
    STATE_CLASS_MEASUREMENT,
    DEVICE_CLASS_VOLTAGE,
    DEVICE_CLASS_PRESSURE,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_ACCURACY_DECIMALS,
)
from esphome.core import coroutine

CODEOWNERS = ["@echavet"]
MULTI_CONF = False

AUTO_LOAD = ["sensor", "switch"]
DEPENDENCIES = []

# Namespace for pool_station C++ code
pool_station_ns = cg.esphome_ns.namespace("pool_station")

# Main component class
PoolStationComponent = pool_station_ns.class_(
    "PoolStationComponent", cg.PollingComponent
)

# Channel sensor class (wraps raw values, applies calibration)
PoolStationChannelSensor = pool_station_ns.class_(
    "PoolStationChannelSensor", sensor.Sensor, cg.Component
)

# Calibration mode switch class
CalibrationModeSwitch = pool_station_ns.class_(
    "CalibrationModeSwitch", switch.Switch, cg.Component
)

# Configuration keys
CONF_POOL_STATION_ID = "pool_station_id"
CONF_CALIBRATION_MODE = "calibration_mode"
CONF_CALIBRATION_INTERVAL = "calibration_interval"
CONF_CHANNELS = "channels"
CONF_PRESSURE = "pressure"
CONF_PH = "ph"
CONF_ORP = "orp"
CONF_SOURCE_ID = "source_id"
CONF_RAW_SENSOR = "raw_sensor"

# Channel types enum (must match C++)
CHANNEL_TYPE_PRESSURE = 0
CHANNEL_TYPE_PH = 1
CHANNEL_TYPE_ORP = 2

# Water temperature (for future lots)
CONF_WATER_TEMPERATURE = "water_temperature"
CONF_WATER_TEMPERATURE_SENSOR_ID = "sensor_id"

# Default sensor configurations
CHANNEL_DEFAULTS = {
    "pressure": {
        "unit": "bar",
        "accuracy": 2,
        "icon": "mdi:gauge",
        "device_class": DEVICE_CLASS_PRESSURE,
        "raw_unit": "V",
        "raw_accuracy": 3,
    },
    "ph": {
        "unit": "pH",
        "accuracy": 2,
        "icon": "mdi:ph",
        "device_class": None,
        "raw_unit": "V",
        "raw_accuracy": 3,
    },
    "orp": {
        "unit": "mV",
        "accuracy": 0,
        "icon": "mdi:flash",
        "device_class": None,
        "raw_unit": "V",
        "raw_accuracy": 3,
    },
}


def channel_schema(channel_type):
    """Generate schema for a channel (pressure, ph, orp)."""
    defaults = CHANNEL_DEFAULTS.get(channel_type, {})
    
    return cv.Schema({
        cv.GenerateID(): cv.declare_id(PoolStationChannelSensor),
        cv.Required(CONF_SOURCE_ID): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_ICON, default=defaults.get("icon", "mdi:gauge")): cv.icon,
        cv.Optional(CONF_UNIT_OF_MEASUREMENT, default=defaults.get("unit", "")): cv.string,
        cv.Optional(CONF_ACCURACY_DECIMALS, default=defaults.get("accuracy", 2)): cv.int_range(0, 5),
        cv.Optional(CONF_UPDATE_INTERVAL, default="1s"): cv.update_interval,
        cv.Optional(CONF_RAW_SENSOR): sensor.sensor_schema(
            unit_of_measurement=defaults.get("raw_unit", "V"),
            accuracy_decimals=defaults.get("raw_accuracy", 3),
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:sine-wave",
        ),
    })


# Calibration mode switch schema
CALIBRATION_MODE_SCHEMA = switch.switch_schema(
    CalibrationModeSwitch,
    icon="mdi:calibrate",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationModeSwitch),
})


# Main CONFIG_SCHEMA for pool_station platform
CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(PoolStationComponent),
    cv.Optional(CONF_UPDATE_INTERVAL, default="1s"): cv.update_interval,
    cv.Optional(CONF_CALIBRATION_INTERVAL, default="1s"): cv.update_interval,
    
    # Calibration mode switch
    cv.Optional(CONF_CALIBRATION_MODE): CALIBRATION_MODE_SCHEMA,
    
    # Channels
    cv.Optional(CONF_CHANNELS): cv.Schema({
        cv.Optional(CONF_PRESSURE): channel_schema("pressure"),
        cv.Optional(CONF_PH): channel_schema("ph"),
        cv.Optional(CONF_ORP): channel_schema("orp"),
    }),
    
    # Water temperature sensor binding (for future Lot 5)
    cv.Optional(CONF_WATER_TEMPERATURE): cv.Schema({
        cv.Optional(CONF_WATER_TEMPERATURE_SENSOR_ID): cv.use_id(sensor.Sensor),
    }),
}).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    """Generate C++ code for pool_station component."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    
    # Set update interval
    cg.add(var.set_update_interval(config[CONF_UPDATE_INTERVAL]))
    cg.add(var.set_calibration_interval(config[CONF_CALIBRATION_INTERVAL]))
    
    # Setup calibration mode switch
    if CONF_CALIBRATION_MODE in config:
        cal_conf = config[CONF_CALIBRATION_MODE]
        cal_switch = await switch.new_switch(cal_conf)
        await cg.register_component(cal_switch, cal_conf)
        cg.add(var.set_calibration_mode_switch(cal_switch))
    
    # Setup channels
    if CONF_CHANNELS in config:
        channels_conf = config[CONF_CHANNELS]
        
        for channel_key, channel_type in [
            (CONF_PRESSURE, CHANNEL_TYPE_PRESSURE),
            (CONF_PH, CHANNEL_TYPE_PH),
            (CONF_ORP, CHANNEL_TYPE_ORP),
        ]:
            if channel_key not in channels_conf:
                continue
                
            ch_conf = channels_conf[channel_key]
            defaults = CHANNEL_DEFAULTS.get(channel_key, {})
            
            # Get source sensor (ADS1115)
            source_sensor = await cg.get_variable(ch_conf[CONF_SOURCE_ID])
            
            # Create channel sensor
            ch_var = cg.new_Pvariable(ch_conf[CONF_ID])
            await cg.register_component(ch_var, ch_conf)
            
            # Configure channel
            cg.add(ch_var.set_channel_type(channel_type))
            cg.add(ch_var.set_source_sensor(source_sensor))
            cg.add(ch_var.set_parent(var))
            cg.add(ch_var.set_update_interval(ch_conf[CONF_UPDATE_INTERVAL]))
            
            # Set sensor properties
            if CONF_NAME in ch_conf:
                cg.add(ch_var.set_name(ch_conf[CONF_NAME]))
            cg.add(ch_var.set_unit_of_measurement(ch_conf[CONF_UNIT_OF_MEASUREMENT]))
            cg.add(ch_var.set_accuracy_decimals(ch_conf[CONF_ACCURACY_DECIMALS]))
            cg.add(ch_var.set_icon(ch_conf[CONF_ICON]))
            cg.add(ch_var.set_state_class(STATE_CLASS_MEASUREMENT))
            if defaults.get("device_class"):
                cg.add(ch_var.set_device_class(defaults["device_class"]))
            
            # Create raw sensor if configured
            if CONF_RAW_SENSOR in ch_conf:
                raw_conf = ch_conf[CONF_RAW_SENSOR]
                raw_var = await sensor.new_sensor(raw_conf)
                cg.add(ch_var.set_raw_sensor(raw_var))
            
            # Register channel with parent
            cg.add(var.register_channel(ch_var, channel_type))
    
    # Water temperature sensor binding (stub for future)
    if CONF_WATER_TEMPERATURE in config:
        wt_config = config[CONF_WATER_TEMPERATURE]
        if CONF_WATER_TEMPERATURE_SENSOR_ID in wt_config:
            wt_sensor = await cg.get_variable(wt_config[CONF_WATER_TEMPERATURE_SENSOR_ID])
            cg.add(var.set_water_temperature_sensor(wt_sensor))
