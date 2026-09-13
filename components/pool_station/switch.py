"""
ESPHome switch platform for pool_station component.
Provides calibration mode switch.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import (
    CONF_ID,
    CONF_NAME,
    CONF_ICON,
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_CONFIG,
)
from . import (
    pool_station_ns,
    PoolStationComponent,
    CONF_POOL_STATION_ID,
    CalibrationModeSwitch,
)

DEPENDENCIES = ["pool_station"]

# Configuration keys
CONF_CALIBRATION_MODE_SWITCH = "calibration_mode"

# Switch schema for standalone use
CONFIG_SCHEMA = switch.switch_schema(
    CalibrationModeSwitch,
    icon="mdi:calibrate",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationModeSwitch),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
})


async def to_code(config):
    """Generate C++ code for calibration mode switch."""
    parent = await cg.get_variable(config[CONF_POOL_STATION_ID])
    
    var = await switch.new_switch(config)
    await cg.register_component(var, config)
    
    cg.add(var.set_parent(parent))
    cg.add(parent.set_calibration_mode_switch(var))
