"""
ESPHome external component: pool_station
Rich calibration and diagnostics platform for pool monitoring.

Lot 2: N-point calibration with persistence, algorithms, and HA Capturer UI.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, switch, number, button, binary_sensor, select
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

AUTO_LOAD = ["sensor", "switch", "number", "button", "binary_sensor", "select"]
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

# Diagnostic flag binary sensors (Lot 4)
DiagnosticNoisyFlag = pool_station_ns.class_(
    "DiagnosticNoisyFlag", binary_sensor.BinarySensor, cg.Component
)
DiagnosticStuckFlag = pool_station_ns.class_(
    "DiagnosticStuckFlag", binary_sensor.BinarySensor, cg.Component
)
DiagnosticOutOfRangeFlag = pool_station_ns.class_(
    "DiagnosticOutOfRangeFlag", binary_sensor.BinarySensor, cg.Component
)

# Temperature compensation switch (Lot 5)
TempCompensationSwitch = pool_station_ns.class_(
    "TempCompensationSwitch", switch.Switch, cg.Component
)

# Calibration temperature text sensor (Lot 5)
CalibrationTempSensor = pool_station_ns.class_(
    "CalibrationTempSensor", cg.Component
)

# Gate classes (Lot 6)
MeasurementGate = pool_station_ns.class_("MeasurementGate")
GateBlockedBinarySensor = pool_station_ns.class_(
    "GateBlockedBinarySensor", binary_sensor.BinarySensor, cg.Component
)

# Campaign classes (Lot 6)
MeasurementCampaign = pool_station_ns.class_(
    "MeasurementCampaign", cg.Component
)
CampaignStartButton = pool_station_ns.class_(
    "CampaignStartButton", button.Button, cg.Component
)
CampaignAbortButton = pool_station_ns.class_(
    "CampaignAbortButton", button.Button, cg.Component
)
CampaignRunningSensor = pool_station_ns.class_(
    "CampaignRunningSensor", binary_sensor.BinarySensor, cg.Component
)
CampaignResultSensor = pool_station_ns.class_(
    "CampaignResultSensor", sensor.Sensor, cg.Component
)

# Gate condition type enum
GateConditionType = pool_station_ns.enum("GateConditionType")
GATE_COND_BINARY_SENSOR = GateConditionType.GATE_COND_BINARY_SENSOR
GATE_COND_SWITCH = GateConditionType.GATE_COND_SWITCH
GATE_COND_SENSOR_THRESHOLD = GateConditionType.GATE_COND_SENSOR_THRESHOLD

# Threshold operator enum
ThresholdOperator = pool_station_ns.enum("ThresholdOperator")
THRESHOLD_OPERATORS = {
    ">": 0,
    ">=": 1,
    "<": 2,
    "<=": 3,
    "==": 4,
}

# Campaign action type enum
CampaignActionType = pool_station_ns.enum("CampaignActionType")
ACTION_SWITCH_ON = CampaignActionType.ACTION_SWITCH_ON
ACTION_SWITCH_OFF = CampaignActionType.ACTION_SWITCH_OFF

# Lot 7: Runtime algorithm select
CalibrationAlgorithmSelect = pool_station_ns.class_(
    "CalibrationAlgorithmSelect", select.Select, cg.Component
)

# Lot 7: Add/Remove point buttons
CalibrationAddPointButton = pool_station_ns.class_(
    "CalibrationAddPointButton", button.Button, cg.Component
)
CalibrationRemovePointButton = pool_station_ns.class_(
    "CalibrationRemovePointButton", button.Button, cg.Component
)

# Lot 7: Draft/Commit buttons
CalibrationCommitButton = pool_station_ns.class_(
    "CalibrationCommitButton", button.Button, cg.Component
)
CalibrationDiscardButton = pool_station_ns.class_(
    "CalibrationDiscardButton", button.Button, cg.Component
)

# Lot 7: Point count number
CalibrationPointCountNumber = pool_station_ns.class_(
    "CalibrationPointCountNumber", number.Number, cg.Component
)

# Lot 7: Draft pending binary sensor
DraftPendingSensor = pool_station_ns.class_(
    "DraftPendingSensor", binary_sensor.BinarySensor, cg.Component
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
CONF_CALIBRATED_SENSOR = "calibrated_sensor"

# Filter configuration keys (Lot 3)
CONF_FILTERS = "filters"
CONF_FILTER_SAMPLES = "filter_samples"
CONF_MAX_JUMP = "max_jump"
CONF_MAX_JUMP_STREAK = "max_jump_streak"
CONF_VALUE_MIN = "value_min"
CONF_VALUE_MAX = "value_max"

# Diagnostics configuration keys (Lot 4)
CONF_DIAGNOSTICS = "diagnostics"
CONF_NOISE_WINDOW = "noise_window"
CONF_NOISE_WARN_PTP = "noise_warn_ptp"
CONF_NOISE_WARN_SIGMA = "noise_warn_sigma"
CONF_STUCK_TIMEOUT = "stuck_timeout"
CONF_STUCK_THRESHOLD = "stuck_threshold"
CONF_RANGE_MIN = "range_min"
CONF_RANGE_MAX = "range_max"
CONF_LOG_RATE_LIMIT = "log_rate_limit"
# Diagnostic sensors
CONF_MEAN_SENSOR = "mean_sensor"
CONF_SIGMA_SENSOR = "sigma_sensor"
CONF_PTP_SENSOR = "ptp_sensor"
# Diagnostic binary sensors (flags)
CONF_NOISY_FLAG = "noisy"
CONF_STUCK_FLAG = "stuck"
CONF_OUT_OF_RANGE_FLAG = "out_of_range"

# Temperature compensation configuration keys (Lot 5)
CONF_TEMPERATURE_COMPENSATION = "temperature_compensation"
CONF_TEMP_COMP_ENABLED = "enabled"
CONF_TEMP_COMP_REFERENCE_TEMP = "reference_temperature"
CONF_TEMP_COMP_NEUTRAL_PH = "neutral_ph"
CONF_TEMP_COMP_ORP_COEFFICIENT = "orp_coefficient"
CONF_TEMP_COMP_SWITCH = "enable_switch"
CONF_CALIBRATION_TEMP_SENSOR = "calibration_temp_sensor"

# Gate configuration keys (Lot 6)
CONF_GATE = "gate"
CONF_SAMPLE_WHEN = "sample_when"
CONF_GATE_CONDITIONS = "conditions"
CONF_GATE_BINARY_SENSOR = "binary_sensor_id"
CONF_GATE_SWITCH = "switch_id"
CONF_GATE_SENSOR = "sensor_id"
CONF_GATE_DESIRED_STATE = "state"
CONF_GATE_THRESHOLD = "threshold"
CONF_GATE_THRESHOLD_OP = "operator"
CONF_GATE_THRESHOLD_TOLERANCE = "tolerance"
CONF_GATE_INVERT = "invert"
CONF_GATE_BLOCKED = "gate_blocked"

# Campaign configuration keys (Lot 6)
CONF_CAMPAIGNS = "campaigns"
CONF_CAMPAIGN_PREPARE = "prepare"
CONF_CAMPAIGN_DELAY = "delay"
CONF_CAMPAIGN_SAMPLE_CHANNELS = "sample_channels"
CONF_CAMPAIGN_BURST_SAMPLES = "burst_samples"
CONF_CAMPAIGN_BURST_DELAY = "burst_delay"
CONF_CAMPAIGN_RESTORE = "restore"
CONF_CAMPAIGN_TIMEOUT = "timeout"
CONF_CAMPAIGN_SAFETY_GATE = "safety_gate"
CONF_CAMPAIGN_CONDITIONS_TAG = "conditions_tag"
CONF_CAMPAIGN_START_BUTTON = "start_button"
CONF_CAMPAIGN_ABORT_BUTTON = "abort_button"
CONF_CAMPAIGN_RUNNING = "running"
CONF_CAMPAIGN_RESULT_SENSORS = "result_sensors"
CONF_CAMPAIGN_ACTION_SWITCH = "switch_id"
CONF_CAMPAIGN_ACTION_STATE = "state"

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

# Lot 7: Advanced capturer UI configuration
CONF_ALGORITHM_SELECT = "algorithm_select"
CONF_ADD_POINT_BUTTON = "add_point_button"
CONF_REMOVE_POINT_BUTTON = "remove_point_button"
CONF_COMMIT_BUTTON = "commit_button"
CONF_DISCARD_BUTTON = "discard_button"
CONF_POINT_COUNT_NUMBER = "point_count_number"
CONF_DRAFT_PENDING = "draft_pending"
CONF_DRAFT_MODE = "draft_mode"

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


def filters_schema():
    """Schema for channel filter configuration (Lot 3).
    
    Pipeline order: raw → median window → calibrate → clamp → jump guard → publish
    
    - filter_samples: Median sliding window size (0 or 1 = off)
    - max_jump: Maximum allowed change between readings (0 = disabled)
    - max_jump_streak: Accept new plateau after N consecutive similar values
    - value_min/value_max: Clamp calibrated values to range
    """
    return cv.Schema({
        cv.Optional(CONF_FILTER_SAMPLES, default=0): cv.int_range(min=0, max=20),
        cv.Optional(CONF_MAX_JUMP, default=0.0): cv.float_,
        cv.Optional(CONF_MAX_JUMP_STREAK, default=3): cv.int_range(min=1, max=10),
        cv.Optional(CONF_VALUE_MIN): cv.float_,
        cv.Optional(CONF_VALUE_MAX): cv.float_,
    })


def diagnostics_schema(channel_type):
    """Schema for channel diagnostics configuration (Lot 4).
    
    Enables noise detection, stuck detection, and out-of-range flags.
    Sensors and binary_sensors are opt-in (only created if declared in YAML).
    
    - noise_window: Sliding window size for noise statistics
    - noise_warn_ptp: Peak-to-peak threshold for noisy flag
    - noise_warn_sigma: Standard deviation threshold for noisy flag
    - stuck_timeout: Duration without change to trigger stuck flag
    - stuck_threshold: Minimum change to consider "not stuck"
    - range_min/range_max: Out-of-range detection bounds
    - log_rate_limit: Rate limit for diagnostic log messages
    """
    defaults = CHANNEL_DEFAULTS.get(channel_type, {})
    
    return cv.Schema({
        # Noise detection thresholds
        cv.Optional(CONF_NOISE_WINDOW, default=10): cv.int_range(min=2, max=50),
        cv.Optional(CONF_NOISE_WARN_PTP, default=0.0): cv.float_,
        cv.Optional(CONF_NOISE_WARN_SIGMA, default=0.0): cv.float_,
        
        # Stuck detection
        cv.Optional(CONF_STUCK_TIMEOUT, default="0s"): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_STUCK_THRESHOLD, default=0.001): cv.positive_float,
        
        # Out-of-range detection (separate from filter clamp)
        cv.Optional(CONF_RANGE_MIN): cv.float_,
        cv.Optional(CONF_RANGE_MAX): cv.float_,
        
        # Log rate limiting
        cv.Optional(CONF_LOG_RATE_LIMIT, default="60s"): cv.positive_time_period_milliseconds,
        
        # Optional diagnostic sensors (opt-in, only created if declared)
        cv.Optional(CONF_MEAN_SENSOR): sensor.sensor_schema(
            unit_of_measurement=defaults.get("unit", ""),
            accuracy_decimals=defaults.get("accuracy", 2) + 1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:chart-bell-curve",
        ),
        cv.Optional(CONF_SIGMA_SENSOR): sensor.sensor_schema(
            unit_of_measurement=defaults.get("unit", ""),
            accuracy_decimals=defaults.get("accuracy", 2) + 2,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:sigma",
        ),
        cv.Optional(CONF_PTP_SENSOR): sensor.sensor_schema(
            unit_of_measurement=defaults.get("unit", ""),
            accuracy_decimals=defaults.get("accuracy", 2) + 1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:arrow-expand-vertical",
        ),
        
        # Optional diagnostic flags (binary_sensors, opt-in)
        cv.Optional(CONF_NOISY_FLAG): binary_sensor.binary_sensor_schema(
            DiagnosticNoisyFlag,
            icon="mdi:waveform",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        cv.Optional(CONF_STUCK_FLAG): binary_sensor.binary_sensor_schema(
            DiagnosticStuckFlag,
            icon="mdi:pause-circle",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        cv.Optional(CONF_OUT_OF_RANGE_FLAG): binary_sensor.binary_sensor_schema(
            DiagnosticOutOfRangeFlag,
            icon="mdi:alert-box",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_PROBLEM,
        ),
    })


def capturer_schema(channel_type):
    """Schema for capturer UI configuration.
    
    Lot 7 additions:
    - algorithm_select: Select entity to change calibration type at runtime
    - add_point_button: Button to add a new calibration point
    - remove_point_button: Button to remove the last calibration point
    - point_count_number: Number entity to control point count
    - draft_mode: Enable draft/commit workflow (default: false for backward compat)
    - commit_button: Button to commit draft changes to live
    - discard_button: Button to discard draft changes
    - draft_pending: Binary sensor showing uncommitted draft changes
    """
    return cv.Schema({
        cv.Optional(CONF_POINT_COUNT, default=3): cv.int_range(min=1, max=10),
        cv.Optional(CONF_CAPTURE_BUTTONS, default=True): cv.boolean,
        cv.Optional(CONF_POINT_NUMBERS, default=True): cv.boolean,
        cv.Optional(CONF_SAVE_BUTTON, default=True): cv.boolean,
        # DFRobot specific UI (only for ORP)
        cv.Optional(CONF_MID_NUMBER, default=False): cv.boolean,
        cv.Optional(CONF_OFFSET_NUMBER, default=False): cv.boolean,
        # Lot 7: Runtime algorithm selection
        cv.Optional(CONF_ALGORITHM_SELECT): select.select_schema(
            CalibrationAlgorithmSelect,
            icon="mdi:function-variant",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        # Lot 7: Add/remove point buttons
        cv.Optional(CONF_ADD_POINT_BUTTON): button.button_schema(
            CalibrationAddPointButton,
            icon="mdi:plus-circle",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_REMOVE_POINT_BUTTON): button.button_schema(
            CalibrationRemovePointButton,
            icon="mdi:minus-circle",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        # Lot 7: Point count number (alternative to add/remove buttons)
        cv.Optional(CONF_POINT_COUNT_NUMBER): number.number_schema(
            CalibrationPointCountNumber,
            icon="mdi:counter",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        # Lot 7: Draft/Commit workflow
        cv.Optional(CONF_DRAFT_MODE, default=False): cv.boolean,
        cv.Optional(CONF_COMMIT_BUTTON): button.button_schema(
            CalibrationCommitButton,
            icon="mdi:check-circle",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_DISCARD_BUTTON): button.button_schema(
            CalibrationDiscardButton,
            icon="mdi:close-circle",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_DRAFT_PENDING): binary_sensor.binary_sensor_schema(
            DraftPendingSensor,
            icon="mdi:alert-circle-outline",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    })


def temperature_compensation_schema(channel_type):
    """Schema for temperature compensation configuration (Lot 5).
    
    Provides temperature-compensated pH and ORP readings using documented models.
    
    ## pH Compensation (Nernstian Model)
    
    The Nernst equation's slope is temperature-dependent:
    
        slope(T) ≈ 59.16 × (T + 273.15) / 298.15  [mV/pH]
    
    Formula used:
    
        pH_compensated = neutral_ph + (pH_measured - neutral_ph) × slope_ratio
        slope_ratio = (T_ref + 273.15) / (T_measured + 273.15)
    
    At pH = neutral_ph (default 7.0), temperature has no effect (isopotential point).
    
    ## ORP Compensation (Linear Model, optional)
    
    ORP compensation is less standardized. A simple linear model is provided:
    
        ORP_compensated = ORP_measured - orp_coefficient × (T_measured - T_ref)
    
    Default orp_coefficient is 0 (disabled).
    
    Pipeline order: raw → median → calibrate → temp_comp → clamp → jump → publish
    """
    # Default values
    default_ref_temp = 25.0  # Standard calibration temperature
    default_neutral = 7.0   # Isopotential point
    default_orp_coeff = 0.0 # Disabled by default
    
    return cv.Schema({
        # Runtime enable/disable (switch entity preferred)
        cv.Optional(CONF_TEMP_COMP_ENABLED, default=False): cv.boolean,
        
        # Reference temperature for compensation (calibration standard)
        cv.Optional(CONF_TEMP_COMP_REFERENCE_TEMP, default=default_ref_temp): cv.float_range(
            min=-10.0, max=60.0
        ),
        
        # Isopotential point for pH (where temperature has no effect)
        cv.Optional(CONF_TEMP_COMP_NEUTRAL_PH, default=default_neutral): cv.float_range(
            min=0.0, max=14.0
        ),
        
        # ORP temperature coefficient (mV/°C, 0 = disabled)
        cv.Optional(CONF_TEMP_COMP_ORP_COEFFICIENT, default=default_orp_coeff): cv.float_,
        
        # Optional runtime enable/disable switch entity
        cv.Optional(CONF_TEMP_COMP_SWITCH): switch.switch_schema(
            TempCompensationSwitch,
            icon="mdi:thermometer-water",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
    })


def validate_threshold_operator(value):
    """Validate threshold operator string."""
    value = cv.string_strict(value)
    if value not in THRESHOLD_OPERATORS:
        raise cv.Invalid(
            f"Unknown threshold operator '{value}'. Valid operators: {list(THRESHOLD_OPERATORS.keys())}"
        )
    return value


def gate_condition_schema():
    """Schema for a single gate condition (Lot 6).
    
    Conditions can be:
    - binary_sensor with desired state (ON/OFF)
    - switch with desired state (ON/OFF)
    - sensor threshold comparison (>, >=, <, <=, ==)
    
    Examples:
      - binary_sensor_id: flow_switch, state: true  (sample when flow detected)
      - switch_id: filtration, state: false         (sample when filtration OFF)
      - sensor_id: pressure, operator: ">=", threshold: 0.5  (sample when pressure >= 0.5)
    """
    return cv.Schema({
        cv.Optional(CONF_NAME): cv.string,
        # Binary sensor condition
        cv.Optional(CONF_GATE_BINARY_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        # Switch condition
        cv.Optional(CONF_GATE_SWITCH): cv.use_id(switch.Switch),
        # Sensor threshold condition
        cv.Optional(CONF_GATE_SENSOR): cv.use_id(sensor.Sensor),
        cv.Optional(CONF_GATE_THRESHOLD_OP, default=">="): validate_threshold_operator,
        cv.Optional(CONF_GATE_THRESHOLD, default=0.0): cv.float_,
        cv.Optional(CONF_GATE_THRESHOLD_TOLERANCE, default=0.001): cv.float_,
        # Desired state for binary_sensor/switch conditions
        cv.Optional(CONF_GATE_DESIRED_STATE, default=True): cv.boolean,
    })


def gate_schema():
    """Schema for per-channel gate configuration (Lot 6).
    
    Gates block sampling/publishing unless all conditions are met.
    Multiple conditions use AND logic.
    
    Example:
      gate:
        conditions:
          - binary_sensor_id: flow_switch
            state: true
          - sensor_id: pressure
            operator: ">="
            threshold: 0.5
        invert: false
        gate_blocked:
          name: "pH Gate Blocked"
    """
    return cv.Schema({
        cv.Optional(CONF_GATE_CONDITIONS, default=[]): cv.ensure_list(gate_condition_schema()),
        cv.Optional(CONF_GATE_INVERT, default=False): cv.boolean,
        cv.Optional(CONF_GATE_BLOCKED): binary_sensor.binary_sensor_schema(
            GateBlockedBinarySensor,
            icon="mdi:gate-alert",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_PROBLEM,
        ),
    })


def campaign_prepare_action_schema():
    """Schema for a campaign prepare action (Lot 6).
    
    Actions are switch state changes (ON/OFF).
    """
    return cv.Schema({
        cv.Required(CONF_CAMPAIGN_ACTION_SWITCH): cv.use_id(switch.Switch),
        cv.Required(CONF_CAMPAIGN_ACTION_STATE): cv.boolean,
    })


def campaign_result_sensor_schema(channel_type):
    """Schema for campaign result sensor."""
    defaults = CHANNEL_DEFAULTS.get(channel_type, {})
    return cv.Schema({
        cv.GenerateID(): cv.declare_id(CampaignResultSensor),
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_UNIT_OF_MEASUREMENT, default=defaults.get("unit", "")): cv.string,
        cv.Optional(CONF_ACCURACY_DECIMALS, default=defaults.get("accuracy", 2)): cv.int_range(0, 5),
        cv.Optional(CONF_ICON, default=defaults.get("icon", "mdi:gauge")): cv.icon,
        cv.Optional("tag"): cv.string,  # Filter by conditions tag
    })


def campaign_schema():
    """Schema for a measurement campaign (Lot 6).
    
    Eric's canonical scenario:
      - prepare: turn relay_filtration OFF
      - delay: 60s (wait for water to settle)
      - sample: pH, ORP channels
      - restore: turn relay_filtration back to prior state
    
    State machine: idle → preparing → waiting → sampling → restoring → idle
    
    Example:
      campaigns:
        - name: "pH/ORP Quiescent"
          prepare:
            - switch_id: relay_filtration
              state: false
          delay: 60s
          sample_channels: [ph, orp]
          burst_samples: 3
          burst_delay: 500ms
          restore: true
          timeout: 120s
          start_button:
            name: "Start Quiescent Measurement"
          running:
            name: "Quiescent Measurement Running"
    """
    return cv.Schema({
        cv.GenerateID(): cv.declare_id(MeasurementCampaign),
        cv.Required(CONF_NAME): cv.string,
        # Prepare actions (switch state changes)
        cv.Optional(CONF_CAMPAIGN_PREPARE, default=[]): cv.ensure_list(campaign_prepare_action_schema()),
        # Delay before sampling
        cv.Optional(CONF_CAMPAIGN_DELAY, default="0s"): cv.positive_time_period_milliseconds,
        # Channels to sample (by name: pressure, ph, orp)
        cv.Required(CONF_CAMPAIGN_SAMPLE_CHANNELS): cv.ensure_list(cv.one_of("pressure", "ph", "orp", lower=True)),
        # Burst sampling
        cv.Optional(CONF_CAMPAIGN_BURST_SAMPLES, default=1): cv.int_range(min=1, max=10),
        cv.Optional(CONF_CAMPAIGN_BURST_DELAY, default="100ms"): cv.positive_time_period_milliseconds,
        # Restore actuators to prior state
        cv.Optional(CONF_CAMPAIGN_RESTORE, default=True): cv.boolean,
        # Timeout safety
        cv.Optional(CONF_CAMPAIGN_TIMEOUT, default="120s"): cv.positive_time_period_milliseconds,
        # Optional safety gate reference (by name from channel gates)
        cv.Optional(CONF_CAMPAIGN_SAFETY_GATE): cv.string,
        # Conditions tag for A/B results
        cv.Optional(CONF_CAMPAIGN_CONDITIONS_TAG): cv.string,
        # UI entities
        cv.Optional(CONF_CAMPAIGN_START_BUTTON): button.button_schema(
            CampaignStartButton,
            icon="mdi:play-circle",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_CAMPAIGN_ABORT_BUTTON): button.button_schema(
            CampaignAbortButton,
            icon="mdi:stop-circle",
            entity_category=ENTITY_CATEGORY_CONFIG,
        ),
        cv.Optional(CONF_CAMPAIGN_RUNNING): binary_sensor.binary_sensor_schema(
            CampaignRunningSensor,
            icon="mdi:run",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        # Optional result sensors per channel
        cv.Optional(CONF_CAMPAIGN_RESULT_SENSORS): cv.Schema({
            cv.Optional(CONF_PRESSURE): campaign_result_sensor_schema("pressure"),
            cv.Optional(CONF_PH): campaign_result_sensor_schema("ph"),
            cv.Optional(CONF_ORP): campaign_result_sensor_schema("orp"),
        }),
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
        # Calibrated sensor (pre-guard diagnostic value) - Lot 3
        cv.Optional(CONF_CALIBRATED_SENSOR): sensor.sensor_schema(
            unit_of_measurement=defaults.get("unit", ""),
            accuracy_decimals=defaults.get("accuracy", 2),
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon=defaults.get("icon", "mdi:gauge"),
        ),
        # Calibration configuration
        cv.Optional(CONF_CALIBRATION): calibration_schema(),
        # Filter configuration (Lot 3)
        cv.Optional(CONF_FILTERS): filters_schema(),
        # Diagnostics configuration (Lot 4)
        cv.Optional(CONF_DIAGNOSTICS): diagnostics_schema(channel_type),
        # Temperature compensation (Lot 5) - primarily for pH and ORP
        cv.Optional(CONF_TEMPERATURE_COMPENSATION): temperature_compensation_schema(channel_type),
        # Capturer UI for calibration
        cv.Optional(CONF_CAPTURER): capturer_schema(channel_type),
        # Calibration invalid binary sensor
        cv.Optional(CONF_CAL_INVALID): binary_sensor.binary_sensor_schema(
            CalibrationInvalidSensor,
            icon="mdi:alert-circle",
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            device_class=DEVICE_CLASS_PROBLEM,
        ),
        # Calibration temperature diagnostic (Lot 5) - shows Tw at last calibration
        cv.Optional(CONF_CALIBRATION_TEMP_SENSOR): sensor.sensor_schema(
            unit_of_measurement="°C",
            accuracy_decimals=1,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            icon="mdi:thermometer-check",
        ),
        # Gate configuration (Lot 6) - block sampling unless conditions met
        cv.Optional(CONF_GATE): gate_schema(),
        # Simple sample_when shorthand (alternative to full gate config)
        cv.Optional(CONF_SAMPLE_WHEN): cv.ensure_list(gate_condition_schema()),
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
    
    # Campaigns (Lot 6) - one-shot measurement sequences
    cv.Optional(CONF_CAMPAIGNS): cv.ensure_list(campaign_schema()),
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
    
    # =========================================================================
    # Lot 7: Advanced Capturer UI
    # =========================================================================
    
    # Algorithm select
    if CONF_ALGORITHM_SELECT in capturer_conf:
        algo_conf = capturer_conf[CONF_ALGORITHM_SELECT]
        algo_var = cg.new_Pvariable(algo_conf[CONF_ID])
        await cg.register_component(algo_var, algo_conf)
        await select.register_select(algo_var, algo_conf, options=[])
        cg.add(algo_var.set_parent(parent_var))
        cg.add(algo_var.set_channel_type(channel_type))
    
    # Add point button
    if CONF_ADD_POINT_BUTTON in capturer_conf:
        add_conf = capturer_conf[CONF_ADD_POINT_BUTTON]
        add_var = cg.new_Pvariable(add_conf[CONF_ID])
        await cg.register_component(add_var, add_conf)
        await button.register_button(add_var, add_conf)
        cg.add(add_var.set_parent(parent_var))
        cg.add(add_var.set_channel_type(channel_type))
    
    # Remove point button
    if CONF_REMOVE_POINT_BUTTON in capturer_conf:
        remove_conf = capturer_conf[CONF_REMOVE_POINT_BUTTON]
        remove_var = cg.new_Pvariable(remove_conf[CONF_ID])
        await cg.register_component(remove_var, remove_conf)
        await button.register_button(remove_var, remove_conf)
        cg.add(remove_var.set_parent(parent_var))
        cg.add(remove_var.set_channel_type(channel_type))
    
    # Point count number
    if CONF_POINT_COUNT_NUMBER in capturer_conf:
        count_conf = capturer_conf[CONF_POINT_COUNT_NUMBER]
        count_var = cg.new_Pvariable(count_conf[CONF_ID])
        await cg.register_component(count_var, count_conf)
        await number.register_number(
            count_var, count_conf,
            min_value=1.0,
            max_value=10.0,
            step=1.0,
        )
        cg.add(count_var.set_parent(parent_var))
        cg.add(count_var.set_channel_type(channel_type))
    
    # Draft mode configuration
    draft_mode_enabled = capturer_conf.get(CONF_DRAFT_MODE, False)
    cg.add(channel_var.set_draft_mode_enabled(draft_mode_enabled))
    
    # Commit button (only if draft mode enabled)
    if CONF_COMMIT_BUTTON in capturer_conf:
        commit_conf = capturer_conf[CONF_COMMIT_BUTTON]
        commit_var = cg.new_Pvariable(commit_conf[CONF_ID])
        await cg.register_component(commit_var, commit_conf)
        await button.register_button(commit_var, commit_conf)
        cg.add(commit_var.set_parent(parent_var))
        cg.add(commit_var.set_channel_type(channel_type))
    
    # Discard button (only if draft mode enabled)
    if CONF_DISCARD_BUTTON in capturer_conf:
        discard_conf = capturer_conf[CONF_DISCARD_BUTTON]
        discard_var = cg.new_Pvariable(discard_conf[CONF_ID])
        await cg.register_component(discard_var, discard_conf)
        await button.register_button(discard_var, discard_conf)
        cg.add(discard_var.set_parent(parent_var))
        cg.add(discard_var.set_channel_type(channel_type))
    
    # Draft pending sensor
    if CONF_DRAFT_PENDING in capturer_conf:
        pending_conf = capturer_conf[CONF_DRAFT_PENDING]
        pending_var = cg.new_Pvariable(pending_conf[CONF_ID])
        await cg.register_component(pending_var, pending_conf)
        await binary_sensor.register_binary_sensor(pending_var, pending_conf)
        cg.add(pending_var.set_parent(parent_var))
        cg.add(pending_var.set_channel_type(channel_type))


async def setup_diagnostics_ui(config, parent_var, channel_var, channel_type, channel_key):
    """Setup diagnostic sensors and flags for a channel (Lot 4)."""
    diag_conf = config.get(CONF_DIAGNOSTICS)
    if diag_conf is None:
        return
    
    # Configure diagnostics thresholds on channel
    cg.add(channel_var.set_diagnostics_enabled(True))
    cg.add(channel_var.set_noise_window(diag_conf[CONF_NOISE_WINDOW]))
    cg.add(channel_var.set_noise_warn_ptp(diag_conf[CONF_NOISE_WARN_PTP]))
    cg.add(channel_var.set_noise_warn_sigma(diag_conf[CONF_NOISE_WARN_SIGMA]))
    cg.add(channel_var.set_stuck_timeout_ms(diag_conf[CONF_STUCK_TIMEOUT]))
    cg.add(channel_var.set_stuck_threshold(diag_conf[CONF_STUCK_THRESHOLD]))
    cg.add(channel_var.set_log_rate_limit_ms(diag_conf[CONF_LOG_RATE_LIMIT]))
    
    if CONF_RANGE_MIN in diag_conf:
        cg.add(channel_var.set_diagnostics_range_min(diag_conf[CONF_RANGE_MIN]))
    if CONF_RANGE_MAX in diag_conf:
        cg.add(channel_var.set_diagnostics_range_max(diag_conf[CONF_RANGE_MAX]))
    
    # Optional diagnostic sensors (only create if declared)
    if CONF_MEAN_SENSOR in diag_conf:
        mean_sens_var = await sensor.new_sensor(diag_conf[CONF_MEAN_SENSOR])
        cg.add(channel_var.set_diag_mean_sensor(mean_sens_var))
    
    if CONF_SIGMA_SENSOR in diag_conf:
        sigma_sens_var = await sensor.new_sensor(diag_conf[CONF_SIGMA_SENSOR])
        cg.add(channel_var.set_diag_sigma_sensor(sigma_sens_var))
    
    if CONF_PTP_SENSOR in diag_conf:
        ptp_sens_var = await sensor.new_sensor(diag_conf[CONF_PTP_SENSOR])
        cg.add(channel_var.set_diag_ptp_sensor(ptp_sens_var))
    
    # Optional diagnostic flags (binary_sensors)
    if CONF_NOISY_FLAG in diag_conf:
        noisy_conf = diag_conf[CONF_NOISY_FLAG]
        noisy_var = cg.new_Pvariable(noisy_conf[CONF_ID])
        await cg.register_component(noisy_var, noisy_conf)
        await binary_sensor.register_binary_sensor(noisy_var, noisy_conf)
        cg.add(noisy_var.set_parent(parent_var))
        cg.add(noisy_var.set_channel_type(channel_type))
        cg.add(channel_var.set_diag_noisy_flag(noisy_var))
    
    if CONF_STUCK_FLAG in diag_conf:
        stuck_conf = diag_conf[CONF_STUCK_FLAG]
        stuck_var = cg.new_Pvariable(stuck_conf[CONF_ID])
        await cg.register_component(stuck_var, stuck_conf)
        await binary_sensor.register_binary_sensor(stuck_var, stuck_conf)
        cg.add(stuck_var.set_parent(parent_var))
        cg.add(stuck_var.set_channel_type(channel_type))
        cg.add(channel_var.set_diag_stuck_flag(stuck_var))
    
    if CONF_OUT_OF_RANGE_FLAG in diag_conf:
        oor_conf = diag_conf[CONF_OUT_OF_RANGE_FLAG]
        oor_var = cg.new_Pvariable(oor_conf[CONF_ID])
        await cg.register_component(oor_var, oor_conf)
        await binary_sensor.register_binary_sensor(oor_var, oor_conf)
        cg.add(oor_var.set_parent(parent_var))
        cg.add(oor_var.set_channel_type(channel_type))
        cg.add(channel_var.set_diag_out_of_range_flag(oor_var))


async def setup_temperature_compensation(config, parent_var, channel_var, channel_type, channel_key):
    """Setup temperature compensation for a channel (Lot 5)."""
    temp_comp_conf = config.get(CONF_TEMPERATURE_COMPENSATION)
    if temp_comp_conf is None:
        return
    
    # Enable temperature compensation on channel
    cg.add(channel_var.set_temp_compensation_enabled(temp_comp_conf[CONF_TEMP_COMP_ENABLED]))
    cg.add(channel_var.set_temp_comp_reference_temperature(temp_comp_conf[CONF_TEMP_COMP_REFERENCE_TEMP]))
    cg.add(channel_var.set_temp_comp_neutral_ph(temp_comp_conf[CONF_TEMP_COMP_NEUTRAL_PH]))
    cg.add(channel_var.set_temp_comp_orp_coefficient(temp_comp_conf[CONF_TEMP_COMP_ORP_COEFFICIENT]))
    
    # Optional runtime enable/disable switch
    if CONF_TEMP_COMP_SWITCH in temp_comp_conf:
        sw_conf = temp_comp_conf[CONF_TEMP_COMP_SWITCH]
        sw_var = cg.new_Pvariable(sw_conf[CONF_ID])
        await cg.register_component(sw_var, sw_conf)
        await switch.register_switch(sw_var, sw_conf)
        cg.add(sw_var.set_parent(parent_var))
        cg.add(sw_var.set_channel_type(channel_type))
        cg.add(channel_var.set_temp_compensation_switch(sw_var))


async def setup_channel_gate(config, parent_var, channel_var, channel_type, channel_key):
    """Setup gate for a channel (Lot 6)."""
    # Check for both gate: block and sample_when: shorthand
    gate_conf = config.get(CONF_GATE)
    sample_when_conf = config.get(CONF_SAMPLE_WHEN)
    
    if gate_conf is None and sample_when_conf is None:
        return
    
    # Create gate object
    gate_var = cg.new_Pvariable(cv.declare_id(MeasurementGate)(f"{channel_key}_gate"))
    cg.add(gate_var.set_name(f"{channel_key}_gate"))
    
    # Merge conditions from both sources
    conditions = []
    if gate_conf is not None:
        conditions.extend(gate_conf.get(CONF_GATE_CONDITIONS, []))
        # Set invert flag
        cg.add(gate_var.set_enabled(True))
        if gate_conf.get(CONF_GATE_INVERT, False):
            # Invert is set via config, need to add method to gate
            pass  # TODO: Add invert support in C++
    
    if sample_when_conf is not None:
        conditions.extend(sample_when_conf)
    
    # Add conditions to gate
    for cond_conf in conditions:
        cond_name = cond_conf.get(CONF_NAME, "")
        
        if CONF_GATE_BINARY_SENSOR in cond_conf:
            # Binary sensor condition
            bs_ref = await cg.get_variable(cond_conf[CONF_GATE_BINARY_SENSOR])
            desired_state = cond_conf.get(CONF_GATE_DESIRED_STATE, True)
            
            # Create condition struct and add to gate
            cg.add(gate_var.add_condition(cg.StructInitializer(
                pool_station_ns.struct("GateCondition"),
                ("type", pool_station_ns.GATE_COND_BINARY_SENSOR),
                ("binary_sensor_ref", bs_ref),
                ("desired_state", desired_state),
                ("name", cond_name),
            )))
        
        elif CONF_GATE_SWITCH in cond_conf:
            # Switch condition
            sw_ref = await cg.get_variable(cond_conf[CONF_GATE_SWITCH])
            desired_state = cond_conf.get(CONF_GATE_DESIRED_STATE, True)
            
            cg.add(gate_var.add_condition(cg.StructInitializer(
                pool_station_ns.struct("GateCondition"),
                ("type", pool_station_ns.GATE_COND_SWITCH),
                ("switch_ref", sw_ref),
                ("desired_state", desired_state),
                ("name", cond_name),
            )))
        
        elif CONF_GATE_SENSOR in cond_conf:
            # Sensor threshold condition
            sens_ref = await cg.get_variable(cond_conf[CONF_GATE_SENSOR])
            op_str = cond_conf.get(CONF_GATE_THRESHOLD_OP, ">=")
            op_val = THRESHOLD_OPERATORS.get(op_str, 1)  # Default to >=
            threshold = cond_conf.get(CONF_GATE_THRESHOLD, 0.0)
            tolerance = cond_conf.get(CONF_GATE_THRESHOLD_TOLERANCE, 0.001)
            
            cg.add(gate_var.add_condition(cg.StructInitializer(
                pool_station_ns.struct("GateCondition"),
                ("type", pool_station_ns.GATE_COND_SENSOR_THRESHOLD),
                ("sensor_ref", sens_ref),
                ("threshold_op", op_val),
                ("threshold_value", threshold),
                ("threshold_tolerance", tolerance),
                ("name", cond_name),
            )))
    
    # Set gate on channel
    cg.add(channel_var.set_gate(gate_var))
    
    # Optional gate_blocked binary sensor
    if gate_conf is not None and CONF_GATE_BLOCKED in gate_conf:
        blocked_conf = gate_conf[CONF_GATE_BLOCKED]
        blocked_var = cg.new_Pvariable(blocked_conf[CONF_ID])
        await cg.register_component(blocked_var, blocked_conf)
        await binary_sensor.register_binary_sensor(blocked_var, blocked_conf)
        cg.add(blocked_var.set_gate(gate_var))
        cg.add(channel_var.set_gate_blocked_sensor(blocked_var))


async def setup_campaigns(config, parent_var):
    """Setup measurement campaigns (Lot 6)."""
    campaigns_conf = config.get(CONF_CAMPAIGNS)
    if campaigns_conf is None:
        return
    
    channel_type_map = {
        "pressure": CHANNEL_TYPE_PRESSURE,
        "ph": CHANNEL_TYPE_PH,
        "orp": CHANNEL_TYPE_ORP,
    }
    
    for camp_conf in campaigns_conf:
        camp_name = camp_conf[CONF_NAME]
        camp_var = cg.new_Pvariable(camp_conf[CONF_ID])
        await cg.register_component(camp_var, camp_conf)
        
        cg.add(camp_var.set_name(camp_name))
        cg.add(camp_var.set_parent(parent_var))
        
        # Configure prepare actions
        for action_conf in camp_conf.get(CONF_CAMPAIGN_PREPARE, []):
            sw_ref = await cg.get_variable(action_conf[CONF_CAMPAIGN_ACTION_SWITCH])
            target_state = action_conf[CONF_CAMPAIGN_ACTION_STATE]
            
            action_type = pool_station_ns.ACTION_SWITCH_ON if target_state else pool_station_ns.ACTION_SWITCH_OFF
            
            cg.add(camp_var.add_prepare_action(cg.StructInitializer(
                pool_station_ns.struct("CampaignAction"),
                ("type", action_type),
                ("switch_ref", sw_ref),
                ("switch_id", str(action_conf[CONF_CAMPAIGN_ACTION_SWITCH])),
            )))
        
        # Configure delay
        delay_ms = camp_conf.get(CONF_CAMPAIGN_DELAY, 0)
        cg.add(camp_var.set_delay_ms(delay_ms))
        
        # Configure sample channels
        for ch_name in camp_conf.get(CONF_CAMPAIGN_SAMPLE_CHANNELS, []):
            ch_type = channel_type_map.get(ch_name, CHANNEL_TYPE_PRESSURE)
            cg.add(camp_var.add_sample_channel(ch_type))
        
        # Configure burst sampling
        cg.add(camp_var.set_burst_samples(camp_conf.get(CONF_CAMPAIGN_BURST_SAMPLES, 1)))
        cg.add(camp_var.set_burst_delay_ms(camp_conf.get(CONF_CAMPAIGN_BURST_DELAY, 100)))
        
        # Configure restore and timeout
        cg.add(camp_var.set_restore(camp_conf.get(CONF_CAMPAIGN_RESTORE, True)))
        cg.add(camp_var.set_timeout_ms(camp_conf.get(CONF_CAMPAIGN_TIMEOUT, 120000)))
        
        # Configure conditions tag
        if CONF_CAMPAIGN_CONDITIONS_TAG in camp_conf:
            cg.add(camp_var.set_conditions_tag(camp_conf[CONF_CAMPAIGN_CONDITIONS_TAG]))
        
        # Setup start button
        if CONF_CAMPAIGN_START_BUTTON in camp_conf:
            btn_conf = camp_conf[CONF_CAMPAIGN_START_BUTTON]
            btn_var = cg.new_Pvariable(btn_conf[CONF_ID])
            await cg.register_component(btn_var, btn_conf)
            await button.register_button(btn_var, btn_conf)
            cg.add(btn_var.set_campaign(camp_var))
        
        # Setup abort button
        if CONF_CAMPAIGN_ABORT_BUTTON in camp_conf:
            btn_conf = camp_conf[CONF_CAMPAIGN_ABORT_BUTTON]
            btn_var = cg.new_Pvariable(btn_conf[CONF_ID])
            await cg.register_component(btn_var, btn_conf)
            await button.register_button(btn_var, btn_conf)
            cg.add(btn_var.set_campaign(camp_var))
        
        # Setup running binary sensor
        if CONF_CAMPAIGN_RUNNING in camp_conf:
            running_conf = camp_conf[CONF_CAMPAIGN_RUNNING]
            running_var = cg.new_Pvariable(running_conf[CONF_ID])
            await cg.register_component(running_var, running_conf)
            await binary_sensor.register_binary_sensor(running_var, running_conf)
            cg.add(running_var.set_campaign(camp_var))
            cg.add(camp_var.set_running_sensor(running_var))
        
        # Setup result sensors
        if CONF_CAMPAIGN_RESULT_SENSORS in camp_conf:
            result_conf = camp_conf[CONF_CAMPAIGN_RESULT_SENSORS]
            for ch_key, ch_type in [
                (CONF_PRESSURE, CHANNEL_TYPE_PRESSURE),
                (CONF_PH, CHANNEL_TYPE_PH),
                (CONF_ORP, CHANNEL_TYPE_ORP),
            ]:
                if ch_key in result_conf:
                    res_sens_conf = result_conf[ch_key]
                    res_sens_var = cg.new_Pvariable(res_sens_conf[CONF_ID])
                    await cg.register_component(res_sens_var, res_sens_conf)
                    await sensor.register_sensor(res_sens_var, res_sens_conf)
                    cg.add(res_sens_var.set_campaign(camp_var))
                    cg.add(res_sens_var.set_channel_type(ch_type))
                    if "tag" in res_sens_conf:
                        cg.add(res_sens_var.set_tag(res_sens_conf["tag"]))
        
        # Register campaign with parent
        cg.add(parent_var.register_campaign(camp_var))


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
            
            # Create calibrated sensor (pre-guard diagnostic) if configured - Lot 3
            if CONF_CALIBRATED_SENSOR in ch_conf:
                cal_sens_conf = ch_conf[CONF_CALIBRATED_SENSOR]
                cal_sens_var = await sensor.new_sensor(cal_sens_conf)
                cg.add(ch_var.set_calibrated_sensor(cal_sens_var))
            
            # Configure filters (Lot 3)
            if CONF_FILTERS in ch_conf:
                flt_conf = ch_conf[CONF_FILTERS]
                cg.add(ch_var.set_filter_samples(flt_conf[CONF_FILTER_SAMPLES]))
                cg.add(ch_var.set_max_jump(flt_conf[CONF_MAX_JUMP]))
                cg.add(ch_var.set_max_jump_streak(flt_conf[CONF_MAX_JUMP_STREAK]))
                if CONF_VALUE_MIN in flt_conf:
                    cg.add(ch_var.set_value_min(flt_conf[CONF_VALUE_MIN]))
                if CONF_VALUE_MAX in flt_conf:
                    cg.add(ch_var.set_value_max(flt_conf[CONF_VALUE_MAX]))
            
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
            
            # Setup Diagnostics (Lot 4)
            await setup_diagnostics_ui(ch_conf, var, ch_var, channel_type, channel_key)
            
            # Setup Temperature Compensation (Lot 5)
            await setup_temperature_compensation(ch_conf, var, ch_var, channel_type, channel_key)
            
            # Setup Calibration Temperature Sensor (Lot 5)
            if CONF_CALIBRATION_TEMP_SENSOR in ch_conf:
                cal_temp_conf = ch_conf[CONF_CALIBRATION_TEMP_SENSOR]
                cal_temp_var = await sensor.new_sensor(cal_temp_conf)
                cg.add(ch_var.set_calibration_temp_sensor(cal_temp_var))
            
            # Setup Gate (Lot 6)
            await setup_channel_gate(ch_conf, var, ch_var, channel_type, channel_key)
            
            # Register channel with parent
            cg.add(var.register_channel(ch_var, channel_type))
    
    # Water temperature sensor binding (stub for future)
    if CONF_WATER_TEMPERATURE in config:
        wt_config = config[CONF_WATER_TEMPERATURE]
        if CONF_WATER_TEMPERATURE_SENSOR_ID in wt_config:
            wt_sensor = await cg.get_variable(wt_config[CONF_WATER_TEMPERATURE_SENSOR_ID])
            cg.add(var.set_water_temperature_sensor(wt_sensor))
    
    # Setup Campaigns (Lot 6)
    await setup_campaigns(config, var)
