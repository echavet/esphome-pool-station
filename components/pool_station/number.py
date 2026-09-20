"""
ESPHome number platform for pool_station calibration.
Provides number entities for calibration point x/y values and DFRobot ORP parameters.

Lot 2: N-point calibration with runtime editing.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import number
from esphome.const import (
    CONF_ID,
    CONF_ICON,
    CONF_ENTITY_CATEGORY,
    CONF_MODE,
    CONF_UNIT_OF_MEASUREMENT,
    ENTITY_CATEGORY_CONFIG,
)
from . import (
    pool_station_ns,
    PoolStationComponent,
    CONF_POOL_STATION_ID,
)

DEPENDENCIES = ["pool_station"]

# Number classes for calibration
CalibrationPointXNumber = pool_station_ns.class_(
    "CalibrationPointXNumber", number.Number, cg.Component
)
CalibrationPointYNumber = pool_station_ns.class_(
    "CalibrationPointYNumber", number.Number, cg.Component
)
DFRobotMidNumber = pool_station_ns.class_(
    "DFRobotMidNumber", number.Number, cg.Component
)
DFRobotOffsetNumber = pool_station_ns.class_(
    "DFRobotOffsetNumber", number.Number, cg.Component
)
FilterRuntimeNumber = pool_station_ns.class_(
    "FilterRuntimeNumber", number.Number, cg.Component
)

# Configuration keys
CONF_CHANNEL = "channel"
CONF_POINT_INDEX = "point_index"
CONF_MIN_VALUE = "min_value"
CONF_MAX_VALUE = "max_value"
CONF_STEP = "step"

# Channel name to type mapping (must match C++)
CHANNEL_NAME_TO_TYPE = {
    "pressure": 0,
    "ph": 1,
    "orp": 2,
}

# Schema for calibration point X number (raw voltage)
CALIBRATION_POINT_X_SCHEMA = number.number_schema(
    CalibrationPointXNumber,
    icon="mdi:alpha-x-circle",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationPointXNumber),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
    cv.Required(CONF_POINT_INDEX): cv.int_range(min=0, max=9),
    cv.Optional(CONF_MIN_VALUE, default=-10.0): cv.float_,
    cv.Optional(CONF_MAX_VALUE, default=10.0): cv.float_,
    cv.Optional(CONF_STEP, default=0.001): cv.positive_float,
    cv.Optional(CONF_UNIT_OF_MEASUREMENT, default="V"): cv.string,
    cv.Optional(CONF_MODE, default="BOX"): cv.one_of("BOX", "SLIDER", upper=True),
})

# Schema for calibration point Y number (calibrated value)
CALIBRATION_POINT_Y_SCHEMA = number.number_schema(
    CalibrationPointYNumber,
    icon="mdi:alpha-y-circle",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationPointYNumber),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
    cv.Required(CONF_POINT_INDEX): cv.int_range(min=0, max=9),
    cv.Optional(CONF_MIN_VALUE, default=-1000.0): cv.float_,
    cv.Optional(CONF_MAX_VALUE, default=1000.0): cv.float_,
    cv.Optional(CONF_STEP, default=0.01): cv.positive_float,
    cv.Optional(CONF_UNIT_OF_MEASUREMENT): cv.string,
    cv.Optional(CONF_MODE, default="BOX"): cv.one_of("BOX", "SLIDER", upper=True),
})

# Schema for DFRobot mid_mv number
DFROBOT_MID_SCHEMA = number.number_schema(
    DFRobotMidNumber,
    icon="mdi:arrow-collapse-vertical",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(DFRobotMidNumber),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
    cv.Optional(CONF_MIN_VALUE, default=0.0): cv.float_,
    cv.Optional(CONF_MAX_VALUE, default=5000.0): cv.float_,
    cv.Optional(CONF_STEP, default=1.0): cv.positive_float,
    cv.Optional(CONF_UNIT_OF_MEASUREMENT, default="mV"): cv.string,
    cv.Optional(CONF_MODE, default="BOX"): cv.one_of("BOX", "SLIDER", upper=True),
})

# Schema for DFRobot offset_mv number
DFROBOT_OFFSET_SCHEMA = number.number_schema(
    DFRobotOffsetNumber,
    icon="mdi:delta",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(DFRobotOffsetNumber),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
    cv.Optional(CONF_MIN_VALUE, default=-1000.0): cv.float_,
    cv.Optional(CONF_MAX_VALUE, default=1000.0): cv.float_,
    cv.Optional(CONF_STEP, default=1.0): cv.positive_float,
    cv.Optional(CONF_UNIT_OF_MEASUREMENT, default="mV"): cv.string,
    cv.Optional(CONF_MODE, default="BOX"): cv.one_of("BOX", "SLIDER", upper=True),
})

# Combined config schema (typically not used directly, numbers are defined inline)
CONFIG_SCHEMA = cv.Any(
    CALIBRATION_POINT_X_SCHEMA,
    CALIBRATION_POINT_Y_SCHEMA,
    DFROBOT_MID_SCHEMA,
    DFROBOT_OFFSET_SCHEMA,
)


async def to_code(config):
    """Generate C++ code for calibration numbers - used by internal schema."""
    pass


async def new_calibration_point_x_number(config, parent, channel_type, point_index):
    """Create a calibration point X number entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    # register_number already registers the Component (ESPHome 2026.4).
    await number.register_number(
        var, config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    cg.add(var.set_point_index(point_index))
    
    return var


async def new_calibration_point_y_number(config, parent, channel_type, point_index):
    """Create a calibration point Y number entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    await number.register_number(
        var, config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    cg.add(var.set_point_index(point_index))
    
    return var


async def new_dfrobot_mid_number(config, parent, channel_type):
    """Create a DFRobot mid_mv number entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    await number.register_number(
        var, config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    
    return var


async def new_dfrobot_offset_number(config, parent, channel_type):
    """Create a DFRobot offset_mv number entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    await number.register_number(
        var, config,
        min_value=config[CONF_MIN_VALUE],
        max_value=config[CONF_MAX_VALUE],
        step=config[CONF_STEP],
    )
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    
    return var
