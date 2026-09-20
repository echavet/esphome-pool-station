"""
ESPHome button platform for pool_station calibration.
Provides Capturer buttons that copy current raw voltage into calibration point X.

Lot 2: N-point calibration with runtime capture.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button
from esphome.const import (
    CONF_ID,
    CONF_ICON,
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_CONFIG,
)
from . import (
    pool_station_ns,
    PoolStationComponent,
    CONF_POOL_STATION_ID,
)

DEPENDENCIES = ["pool_station"]

# Button class for capturing raw voltage
CalibrationCaptureButton = pool_station_ns.class_(
    "CalibrationCaptureButton", button.Button, cg.Component
)

# Save calibration to flash button
CalibrationSaveButton = pool_station_ns.class_(
    "CalibrationSaveButton", button.Button, cg.Component
)

AdsResetYamlButton = pool_station_ns.class_(
    "AdsResetYamlButton", button.Button, cg.Component
)

# Configuration keys
CONF_CHANNEL = "channel"
CONF_POINT_INDEX = "point_index"

# Channel name to type mapping (must match C++)
CHANNEL_NAME_TO_TYPE = {
    "pressure": 0,
    "ph": 1,
    "orp": 2,
}

# Schema for capture button
CALIBRATION_CAPTURE_SCHEMA = button.button_schema(
    CalibrationCaptureButton,
    icon="mdi:crosshairs-gps",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationCaptureButton),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
    cv.Required(CONF_POINT_INDEX): cv.int_range(min=0, max=9),
})

# Schema for save button
CALIBRATION_SAVE_SCHEMA = button.button_schema(
    CalibrationSaveButton,
    icon="mdi:content-save",
    entity_category=ENTITY_CATEGORY_CONFIG,
).extend({
    cv.GenerateID(): cv.declare_id(CalibrationSaveButton),
    cv.Required(CONF_POOL_STATION_ID): cv.use_id(PoolStationComponent),
    cv.Required(CONF_CHANNEL): cv.one_of(*CHANNEL_NAME_TO_TYPE.keys(), lower=True),
})

# Combined config schema
CONFIG_SCHEMA = cv.Any(
    CALIBRATION_CAPTURE_SCHEMA,
    CALIBRATION_SAVE_SCHEMA,
)


async def to_code(config):
    """Generate C++ code for calibration buttons - used by internal schema."""
    pass


async def new_capture_button(config, parent, channel_type, point_index):
    """Create a calibration capture button entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    cg.add(var.set_point_index(point_index))
    
    return var


async def new_save_button(config, parent, channel_type):
    """Create a calibration save button entity."""
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await button.register_button(var, config)
    
    cg.add(var.set_parent(parent))
    cg.add(var.set_channel_type(channel_type))
    
    return var
