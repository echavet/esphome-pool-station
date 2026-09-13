#!/usr/bin/env python3
"""Unit tests for the sensor.register_sensor config strip (v0.7.11).

Reproduces the ESPHome 2026.x crash: sensor.setup_sensor_core_ treats
CONF_FILTERS as a list of registry filter dicts. pool_station channels
use `filters:` as a dict of channel guards. Iterating that dict yields
string keys; build_filters then calls .items() on each key.

Run with: python3 tests/test_sensor_register_config.py
"""

import importlib.util
import os
import sys
import unittest

HELPER_PATH = os.path.join(
    os.path.dirname(__file__),
    "..",
    "components",
    "pool_station",
    "sensor_register_compat.py",
)


def _load_helper():
    spec = importlib.util.spec_from_file_location(
        "sensor_register_compat", os.path.abspath(HELPER_PATH)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


compat = _load_helper()


class FakeEStr(str):
    """Minimal stand-in for esphome.core.EStr (string without .items())."""


def build_filters_like_esphome(filters_conf):
    """Mimic ESPHome build_filters / build_registry_list iteration."""
    built = []
    for filter_conf in filters_conf:
        built.append(dict(filter_conf.items()))
    return built


class SensorRegisterConfigTest(unittest.TestCase):
    def test_strips_filters_and_keeps_entity_metadata(self):
        ch_conf = {
            "id": "ph_channel",
            "name": "Pool pH",
            "unit_of_measurement": "pH",
            "accuracy_decimals": 2,
            "icon": "mdi:ph",
            "state_class": "measurement",
            "source_id": "ads_ph",
            "update_interval": "1s",
            "filters": {
                "filter_samples": 5,
                "max_jump": 0.5,
                "max_jump_streak": 3,
                "value_min": 0.0,
                "value_max": 14.0,
            },
            "calibration": {"type": "linear"},
        }

        entity_conf = compat.sensor_register_config(ch_conf)

        self.assertNotIn("filters", entity_conf)
        self.assertEqual(entity_conf["name"], "Pool pH")
        self.assertEqual(entity_conf["unit_of_measurement"], "pH")
        self.assertEqual(entity_conf["accuracy_decimals"], 2)
        self.assertEqual(entity_conf["icon"], "mdi:ph")
        self.assertEqual(entity_conf["state_class"], "measurement")
        self.assertEqual(entity_conf["id"], "ph_channel")
        # Original conf is unchanged so set_filter_* can still run
        self.assertEqual(ch_conf["filters"]["filter_samples"], 5)
        self.assertIsNot(entity_conf, ch_conf)

    def test_no_filters_key_is_passthrough_copy(self):
        ch_conf = {"id": "pressure", "name": "Pool Pressure"}
        entity_conf = compat.sensor_register_config(ch_conf)
        self.assertEqual(entity_conf, ch_conf)
        self.assertIsNot(entity_conf, ch_conf)

    def test_estr_filter_keys_do_not_reach_build_filters(self):
        filters = {
            FakeEStr("filter_samples"): 5,
            FakeEStr("max_jump"): 0.5,
        }
        ch_conf = {"name": "Pool pH", "filters": filters}

        with self.assertRaises(AttributeError) as ctx:
            build_filters_like_esphome(ch_conf["filters"])
        self.assertIn("items", str(ctx.exception))

        entity_conf = compat.sensor_register_config(ch_conf)
        self.assertNotIn("filters", entity_conf)
        # Channel-guard path still sees the original dict
        self.assertEqual(ch_conf["filters"][FakeEStr("filter_samples")], 5)

    def test_only_filters_collides_with_sensor_core(self):
        """Documented scan: pool_station channel keys vs sensor core keys."""
        sensor_core_keys = {
            "device_class",
            "state_class",
            "unit_of_measurement",
            "accuracy_decimals",
            "force_update",
            "filters",
            "on_value",
            "on_raw_value",
            "on_value_range",
            "mqtt_id",
            "expire_after",
            "web_server",
            "name",
            "icon",
            "entity_category",
            "disabled_by_default",
            "internal",
            "id",
        }
        pool_station_only_channel_keys = {
            "source_id",
            "update_interval",
            "raw_sensor",
            "calibrated_sensor",
            "calibration",
            "filters",
            "diagnostics",
            "temperature_compensation",
            "capturer",
            "cal_invalid",
            "calibration_temp_sensor",
            "gate",
            "sample_when",
        }
        collisions = sensor_core_keys & pool_station_only_channel_keys
        self.assertEqual(collisions, {"filters"})
        self.assertEqual(compat.SENSOR_REGISTER_STRIP_KEYS, collisions)


if __name__ == "__main__":
    sys.exit(unittest.main())
