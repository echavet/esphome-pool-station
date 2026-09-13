"""
ESPHome external component: pool_station
Rich calibration and diagnostics platform for pool monitoring.

Lot 2: N-point calibration with persistence, algorithms, and HA Capturer UI.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, switch, number, button, binary_sensor
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
    DEVICE_CLASS_PROBLEM,
    CONF_UNIT_OF_MEASUREMENT,
    CONF_ACCURACY_DECIMALS,
)
from esphome.core import coroutine

CODEOWNERS = ["@echavet"]
MULTI_CONF = False

AUTO_LOAD = ["sensor", "switch", "number", "button", "binary_sensor"]
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

# Calibration UI classes
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
CalibrationCaptureButton = pool_station_ns.class_(
    "CalibrationCaptureButton", button.Button, cg.Component
)
CalibrationSaveButton = pool_station_ns.class_(
    "CalibrationSaveButton", button.Button, cg.Component
)
CalibrationInvalidSensor = pool_station_ns.class_(
    "CalibrationInvalidSensor", binary_sensor.BinarySensor, cg.Component
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

# Calibration configuration keys
CONF_CALIBRATION = "calibration"
CONF_CALIBRATION_TYPE = "type"
CONF_CALIBRATION_ORDER = "order"
CONF_CALIBRATION_PRECISION = "precision"
CONF_CALIBRATION_POINTS = "points"
CONF_CALIBRATION_X = "x"
CONF_CALIBRATION_Y = "y"
CONF_DFROBOT_MID = "mid_mv"
CONF_DFROBOT_OFFSET = "offset_mv"
CONF_CAL_INVALID = "cal_invalid"

# Capturer UI configuration
CONF_CAPTURER = "capturer"
CONF_POINT_COUNT = "point_count"
CONF_CAPTURE_BUTTONS = "capture_buttons"
CONF_POINT_NUMBERS = "point_numbers"
CONF_SAVE_BUTTON = "save_button"
CONF_MID_NUMBER = "mid_number"
CONF_OFFSET_NUMBER = "offset_number"

# Channel types enum (must match C++)
CHANNEL_TYPE_PRESSURE = 0
CHANNEL_TYPE_PH = 1
CHANNEL_TYPE_ORP = 2

# Calibration types enum (must match C++)
CALIBRATION_TYPES = {
    "none": 0,
    "linear": 1,
    "polynomial": 2,
    "piecewise": 3,
    "dfrobot_orp": 4,
    "exponential": 5,  # TODO
    "logarithmic": 6,  # TODO
    "power": 7,        # TODO
}

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
        "y_unit": "bar",
        "y_min": -1.0,
        "y_max": 20.0,
    },
    "ph": {
        "unit": "pH",
        "accuracy": 2,
        "icon": "mdi:ph",
        "device_class": None,
        "raw_unit": "V",
        "raw_accuracy": 3,
        "y_unit": "pH",
        "y_min": 0.0,
        "y_max": 14.0,
    },
    "orp": {
        "unit": "mV",
        "accuracy": 0,
        "icon": "mdi:flash",
        "device_class": None,
        "raw_unit": "V",
        "raw_accuracy": 3,
        "y_unit": "mV",
        "y_min": -2000.0,
        "y_max": 2000.0,
    },
}


def validate_calibration_type(value):
    """Validate calibration type string."""
    value = cv.string_strict(value).lower()
    if value not in CALIBRATION_TYPES:
        raise cv.Invalid(
            f"Unknown calibration type '{value}'. Valid types: {list(CALIBRATION_TYPES.keys())}"
        )
    return value


def calibration_point_schema():
    """Schema for a single calibration point (x, y)."""
    return cv.Schema({
        cv.Required(CONF_CALIBRATION_X): cv.float_,
        cv.Required(CONF_CALIBRATION_Y): cv.float_,
    })


def calibration_schema():
    """Schema for calibration configuration."""
    return cv.Schema({
        cv.Required(CONF_CALIBRATION_TYPE): validate_calibration_type,
        cv.Optional(CONF_CALIBRATION_ORDER, default=2): cv.int_range(min=1, max=5),
        cv.Optional(CONF_CALIBRATION_PRECISION, default=2): cv.int_range(min=0, max=5),
        cv.Optional(CONF_CALIBRATION_POINTS, default=[]): cv.ensure_list(calibration_point_schema()),
        # DFRobot ORP specific
        cv.Optional(CONF_DFROBOT_MID, default=2500.0): cv.float_,
        cv.Optional(CONF_DFROBOT_OFFSET, default=0.0): cv.float_,
    })


def capturer_schema(channel_type):
    """Schema for capturer UI configuration."""
    return cv.Schema({
        cv.Optional(CONF_POINT_COUNT, default=3): cv.int_range(min=1, max=10),
        cv.Optional(CONF_CAPTURE_BUTTONS, default=True): cv.boolean,
        cv.Optional(CONF_POINT_NUMBERS, default=True): cv.boolean,
        cv.Optional(CONF_SAVE_BUTTON, default=True): cv.boolean,
        # DFRobot specific UI (only for ORP)
        cv.Optional(CONF_MID_NUMBER, default=False): cv.boolean,
        cv.Optional(CONF_OFFSET_NUMBER, default=False): cv.boolean,
    })


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
        # Calibration configuration
        cv.Optional(CONF_CALIBRATION): calibration_schema(),
        # Capturer UI for calibration
        cv.Optional(CONF_CAPTURER): capturer_schema(channel_type),
        # Calibration invalid binary sensor
        cv.Optional(CONF_CAL_INVALID): binary_sensor.binary_sensor_schema(
            CalibrationInvalidSensor,
            icon="mdi:alert-circle",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_PROBLEM,
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


def generate_preferences_key(base_key, channel_type):
    """Generate a unique preferences key for calibration storage."""
    # Use a hash based on base_key and channel type
    return hash((base_key, channel_type)) & 0xFFFFFFFF


async def setup_capturer_ui(config, parent_var, channel_var, channel_type, channel_key):
    """Setup Capturer UI entities for a channel."""
    capturer_conf = config.get(CONF_CAPTURER)
    if capturer_conf is None:
        return
    
    defaults = CHANNEL_DEFAULTS.get(channel_key, {})
    point_count = capturer_conf.get(CONF_POINT_COUNT, 3)
    
    # Create capture buttons and point numbers for each point
    for point_idx in range(point_count):
        # Capture button (copies current raw to point X)
        if capturer_conf.get(CONF_CAPTURE_BUTTONS, True):
            btn_id = f"{channel_key}_capture_{point_idx}"
            btn_conf = {
                CONF_ID: cv.declare_id(CalibrationCaptureButton)(btn_id),
                CONF_NAME: f"{channel_key.title()} Capture Point {point_idx + 1}",
                CONF_ICON: "mdi:crosshairs-gps",
                CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
            }
            btn_var = cg.new_Pvariable(btn_conf[CONF_ID])
            await cg.register_component(btn_var, btn_conf)
            await button.register_button(btn_var, btn_conf)
            cg.add(btn_var.set_parent(parent_var))
            cg.add(btn_var.set_channel_type(channel_type))
            cg.add(btn_var.set_point_index(point_idx))
        
        # Point X number (raw voltage)
        if capturer_conf.get(CONF_POINT_NUMBERS, True):
            num_x_id = f"{channel_key}_point{point_idx}_x"
            num_x_conf = {
                CONF_ID: cv.declare_id(CalibrationPointXNumber)(num_x_id),
                CONF_NAME: f"{channel_key.title()} Cal Point {point_idx + 1} X",
                CONF_ICON: "mdi:alpha-x-circle",
                CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
                CONF_UNIT_OF_MEASUREMENT: "V",
            }
            num_x_var = cg.new_Pvariable(num_x_conf[CONF_ID])
            await cg.register_component(num_x_var, num_x_conf)
            await number.register_number(
                num_x_var, num_x_conf,
                min_value=-10.0,
                max_value=10.0,
                step=0.0001,
            )
            cg.add(num_x_var.set_parent(parent_var))
            cg.add(num_x_var.set_channel_type(channel_type))
            cg.add(num_x_var.set_point_index(point_idx))
            cg.add(parent_var.register_point_x_number(num_x_var, channel_type, point_idx))
        
        # Point Y number (calibrated value)
        if capturer_conf.get(CONF_POINT_NUMBERS, True):
            num_y_id = f"{channel_key}_point{point_idx}_y"
            num_y_conf = {
                CONF_ID: cv.declare_id(CalibrationPointYNumber)(num_y_id),
                CONF_NAME: f"{channel_key.title()} Cal Point {point_idx + 1} Y",
                CONF_ICON: "mdi:alpha-y-circle",
                CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
                CONF_UNIT_OF_MEASUREMENT: defaults.get("y_unit", ""),
            }
            num_y_var = cg.new_Pvariable(num_y_conf[CONF_ID])
            await cg.register_component(num_y_var, num_y_conf)
            await number.register_number(
                num_y_var, num_y_conf,
                min_value=defaults.get("y_min", -1000.0),
                max_value=defaults.get("y_max", 1000.0),
                step=0.01,
            )
            cg.add(num_y_var.set_parent(parent_var))
            cg.add(num_y_var.set_channel_type(channel_type))
            cg.add(num_y_var.set_point_index(point_idx))
            cg.add(parent_var.register_point_y_number(num_y_var, channel_type, point_idx))
    
    # Save button
    if capturer_conf.get(CONF_SAVE_BUTTON, True):
        save_id = f"{channel_key}_save_cal"
        save_conf = {
            CONF_ID: cv.declare_id(CalibrationSaveButton)(save_id),
            CONF_NAME: f"{channel_key.title()} Save Calibration",
            CONF_ICON: "mdi:content-save",
            CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
        }
        save_var = cg.new_Pvariable(save_conf[CONF_ID])
        await cg.register_component(save_var, save_conf)
        await button.register_button(save_var, save_conf)
        cg.add(save_var.set_parent(parent_var))
        cg.add(save_var.set_channel_type(channel_type))
    
    # DFRobot mid_mv number (typically for ORP with dfrobot_orp calibration)
    if capturer_conf.get(CONF_MID_NUMBER, False):
        mid_id = f"{channel_key}_dfrobot_mid"
        mid_conf = {
            CONF_ID: cv.declare_id(DFRobotMidNumber)(mid_id),
            CONF_NAME: f"{channel_key.title()} DFRobot Mid mV",
            CONF_ICON: "mdi:arrow-collapse-vertical",
            CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
            CONF_UNIT_OF_MEASUREMENT: "mV",
        }
        mid_var = cg.new_Pvariable(mid_conf[CONF_ID])
        await cg.register_component(mid_var, mid_conf)
        await number.register_number(
            mid_var, mid_conf,
            min_value=0.0,
            max_value=5000.0,
            step=1.0,
        )
        cg.add(mid_var.set_parent(parent_var))
        cg.add(mid_var.set_channel_type(channel_type))
        cg.add(parent_var.register_dfrobot_mid_number(mid_var, channel_type))
    
    # DFRobot offset_mv number
    if capturer_conf.get(CONF_OFFSET_NUMBER, False):
        offset_id = f"{channel_key}_dfrobot_offset"
        offset_conf = {
            CONF_ID: cv.declare_id(DFRobotOffsetNumber)(offset_id),
            CONF_NAME: f"{channel_key.title()} DFRobot Offset mV",
            CONF_ICON: "mdi:delta",
            CONF_ENTITY_CATEGORY: ENTITY_CATEGORY_CONFIG,
            CONF_UNIT_OF_MEASUREMENT: "mV",
        }
        offset_var = cg.new_Pvariable(offset_conf[CONF_ID])
        await cg.register_component(offset_var, offset_conf)
        await number.register_number(
            offset_var, offset_conf,
            min_value=-1000.0,
            max_value=1000.0,
            step=1.0,
        )
        cg.add(offset_var.set_parent(parent_var))
        cg.add(offset_var.set_channel_type(channel_type))
        cg.add(parent_var.register_dfrobot_offset_number(offset_var, channel_type))


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
            
            # Set preferences key for calibration persistence
            prefs_key = generate_preferences_key(str(config[CONF_ID]), channel_type)
            cg.add(ch_var.set_preferences_key(prefs_key))
            
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
            
            # Configure calibration
            if CONF_CALIBRATION in ch_conf:
                cal_conf = ch_conf[CONF_CALIBRATION]
                cal_type = CALIBRATION_TYPES[cal_conf[CONF_CALIBRATION_TYPE]]
                
                cg.add(ch_var.set_calibration_type(cal_type))
                cg.add(ch_var.set_polynomial_order(cal_conf[CONF_CALIBRATION_ORDER]))
                cg.add(ch_var.set_calibration_precision(cal_conf[CONF_CALIBRATION_PRECISION]))
                
                # DFRobot ORP specific params
                cg.add(ch_var.set_dfrobot_mid_mv(cal_conf[CONF_DFROBOT_MID]))
                cg.add(ch_var.set_dfrobot_offset_mv(cal_conf[CONF_DFROBOT_OFFSET]))
                
                # Add seed calibration points from YAML
                for pt in cal_conf.get(CONF_CALIBRATION_POINTS, []):
                    cg.add(ch_var.add_calibration_point(pt[CONF_CALIBRATION_X], pt[CONF_CALIBRATION_Y]))
            
            # Setup calibration invalid binary sensor
            if CONF_CAL_INVALID in ch_conf:
                cal_inv_conf = ch_conf[CONF_CAL_INVALID]
                cal_inv_var = cg.new_Pvariable(cal_inv_conf[CONF_ID])
                await cg.register_component(cal_inv_var, cal_inv_conf)
                await binary_sensor.register_binary_sensor(cal_inv_var, cal_inv_conf)
                cg.add(cal_inv_var.set_parent(var))
                cg.add(cal_inv_var.set_channel_type(channel_type))
                cg.add(var.register_cal_invalid_sensor(cal_inv_var, channel_type))
            
            # Setup Capturer UI
            await setup_capturer_ui(ch_conf, var, ch_var, channel_type, channel_key)
            
            # Register channel with parent
            cg.add(var.register_channel(ch_var, channel_type))
    
    # Water temperature sensor binding (stub for future)
    if CONF_WATER_TEMPERATURE in config:
        wt_config = config[CONF_WATER_TEMPERATURE]
        if CONF_WATER_TEMPERATURE_SENSOR_ID in wt_config:
            wt_sensor = await cg.get_variable(wt_config[CONF_WATER_TEMPERATURE_SENSOR_ID])
            cg.add(var.set_water_temperature_sensor(wt_sensor))
