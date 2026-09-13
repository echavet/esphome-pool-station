"""Keep pool_station channel YAML compatible with sensor.register_sensor().

After PR #18, channel codegen calls ESPHome's sensor.register_sensor() so
entity metadata (name, unit, icon, state_class, device_class) is set via
the 2026.x helpers instead of manual set_name setters.

pool_station channels already use the YAML key `filters:` for custom
channel guards (filter_samples, max_jump, max_jump_streak, value_min,
value_max). ESPHome's sensor core treats CONF_FILTERS as the standard
filter registry (median, sliding_window, …). Passing the pool_station
dict through register_sensor makes build_filters() iterate that dict and
crash:

    AttributeError: 'EStr' object has no attribute 'items'

This module is intentionally free of esphome imports so the strip logic
can be unit-tested without a full ESPHome install.
"""

# Keys consumed by ESPHome 2026.x sensor.setup_sensor_core_ / setup_entity
# that would be misinterpreted if a pool_station channel dict is passed
# through sensor.register_sensor() unchanged.
#
# Scan vs channel_schema() extra keys:
#   source_id, update_interval, raw_sensor, calibrated_sensor, calibration,
#   filters, diagnostics, temperature_compensation, capturer, cal_invalid,
#   calibration_temp_sensor, gate, sample_when
#
# ESPHome sensor core reads (among entity metadata):
#   device_class, state_class, unit_of_measurement, accuracy_decimals,
#   force_update, filters, on_value, on_raw_value, on_value_range,
#   mqtt_id, expire_after, web_server
#
# Only `filters` exists on both sides with different semantics. Other
# pool_station-only keys are ignored by setup_sensor_core_ today.
SENSOR_REGISTER_STRIP_KEYS = frozenset({
    "filters",
})


def sensor_register_config(ch_conf):
    """Return a shallow copy of channel config safe for register_sensor().

    Entity metadata keys are preserved. pool_station-only keys that collide
    with sensor entity setup are omitted. The original mapping is not mutated
    so callers can still apply set_filter_* from ch_conf['filters'].
    """
    return {key: value for key, value in ch_conf.items()
            if key not in SENSOR_REGISTER_STRIP_KEYS}
