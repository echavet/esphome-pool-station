#!/usr/bin/env python3
"""Lot B unit tests: dynamic filters, sidecar merge, resize, draft isolation.

Mirrors ads_runtime.h / channel apply_*_runtime. No ESPHome import.

Run with: python3 tests/test_filter_runtime.py
"""

import math
import os
import re
import struct
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HELPER_PATH = os.path.join(ROOT, "components", "pool_station", "ads_runtime.py")
ADS_H = os.path.join(ROOT, "components", "pool_station", "ads_runtime.h")
INIT_PATH = os.path.join(ROOT, "components", "pool_station", "__init__.py")
ENGINE_H = os.path.join(ROOT, "components", "pool_station", "calibration_engine.h")
CHANNEL_CPP = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
CHANNEL_H = os.path.join(ROOT, "components", "pool_station", "pool_station.h")
UI_CPP = os.path.join(ROOT, "components", "pool_station", "calibration_ui.cpp")
FILTER_CPP = os.path.join(ROOT, "components", "pool_station", "channel_filter.cpp")
PREFS_PATH = os.path.join(ROOT, "components", "pool_station", "prefs_key.py")


def _load_helper():
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "ads_runtime", os.path.abspath(HELPER_PATH)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _fn(source, signature):
    start = source.find(signature)
    if start < 0:
        return ""
    brace = source.find("{", start)
    depth = 0
    for idx in range(brace, len(source)):
        if source[idx] == "{":
            depth += 1
        elif source[idx] == "}":
            depth -= 1
            if depth == 0:
                return source[start : idx + 1]
    return source[start:]


ads = _load_helper()

YAML_PH = {
    "filter_samples": 5,
    "max_jump": 0.5,
    "max_jump_streak": 3,
    "value_min": 0.0,
    "value_max": 14.0,
    "update_interval_ms": 60000,
}


class FilterClampContractTest(unittest.TestCase):
    def test_filter_samples_range(self):
        self.assertEqual(ads.clamp_filter_samples(0), 0)
        self.assertEqual(ads.clamp_filter_samples(5), 5)
        self.assertEqual(ads.clamp_filter_samples(20), 20)
        self.assertEqual(ads.clamp_filter_samples(-3), 0)
        self.assertEqual(ads.clamp_filter_samples(99), 20)

    def test_max_jump_streak_range(self):
        self.assertEqual(ads.clamp_max_jump_streak(1), 1)
        self.assertEqual(ads.clamp_max_jump_streak(10), 10)
        self.assertEqual(ads.clamp_max_jump_streak(0), 1)
        self.assertEqual(ads.clamp_max_jump_streak(99), 10)

    def test_max_jump_zero_is_off_not_nan(self):
        self.assertEqual(ads.clamp_max_jump(0.0), 0.0)
        self.assertEqual(ads.clamp_max_jump(-1.0), 0.0)
        self.assertEqual(ads.clamp_max_jump(0.5), 0.5)
        self.assertEqual(ads.clamp_max_jump(math.nan), 0.0)

    def test_update_interval_seconds_bounds(self):
        self.assertEqual(ads.clamp_update_interval_ms(500), 1000)
        self.assertEqual(ads.clamp_update_interval_ms(60000), 60000)
        self.assertEqual(ads.clamp_update_interval_ms(4000000), 3600000)

    def test_header_literals_match(self):
        with open(ADS_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("FILTER_SAMPLES_MAX = 20", header)
        self.assertIn("MAX_JUMP_STREAK_MAX = 10", header)
        self.assertIn("UPDATE_INTERVAL_MS_MAX = 3600000", header)
        self.assertIn("FILTER_RT_UPDATE_INTERVAL_S = 5", header)


class NvsMergeTest(unittest.TestCase):
    def test_lot_a_unmanaged_filters_keep_yaml(self):
        nvs = {
            "filter_samples": ads.GAIN_UNMANAGED,
            "max_jump_streak": ads.GAIN_UNMANAGED,
            "max_jump": math.nan,
            "flags": ads.RT_FLAG_HAS_GAIN,
            "update_interval_ms": 0,
        }
        merged = ads.merge_nvs_filters(YAML_PH, nvs, persist=True)
        self.assertEqual(merged["filter_samples"], 5)
        self.assertEqual(merged["max_jump"], 0.5)
        self.assertEqual(merged["max_jump_streak"], 3)
        self.assertEqual(merged["value_min"], 0.0)
        self.assertEqual(merged["value_max"], 14.0)
        self.assertEqual(merged["update_interval_ms"], 60000)

    def test_nvs_wins_managed_fields(self):
        nvs = {
            "filter_samples": 9,
            "max_jump": 0.2,
            "max_jump_streak": 4,
            "value_min": 1.0,
            "value_max": 12.0,
            "flags": ads.RT_FLAG_HAS_VMIN | ads.RT_FLAG_HAS_VMAX,
            "update_interval_ms": 30000,
        }
        merged = ads.merge_nvs_filters(YAML_PH, nvs, persist=True)
        self.assertEqual(merged["filter_samples"], 9)
        self.assertEqual(merged["max_jump"], 0.2)
        self.assertEqual(merged["max_jump_streak"], 4)
        self.assertEqual(merged["value_min"], 1.0)
        self.assertEqual(merged["value_max"], 12.0)
        self.assertEqual(merged["update_interval_ms"], 30000)

    def test_persist_false_keeps_yaml(self):
        nvs = {"filter_samples": 1, "max_jump": 0.1, "flags": 0, "update_interval_ms": 1000}
        merged = ads.merge_nvs_filters(YAML_PH, nvs, persist=False)
        self.assertEqual(merged, YAML_PH)

    def test_zero_samples_is_managed_off(self):
        nvs = {"filter_samples": 0}
        merged = ads.merge_nvs_filters(YAML_PH, nvs, persist=True)
        self.assertEqual(merged["filter_samples"], 0)

    def test_header_sentinels_documented(self):
        with open(ADS_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("0xFF / NAN / interval 0 = unmanaged", header)
        self.assertIn("is_managed_u8", header)
        self.assertIn("is_managed_interval_ms", header)


class SidecarLayoutTest(unittest.TestCase):
    def test_pack_roundtrip_filters_without_layout_bump(self):
        blob = ads.pack_runtime_prefs({
            "magic": ads.RUNTIME_PREFS_MAGIC,
            "version": 1,
            "ads_gain": ads.GAIN_6P144,
            "ads_sps": ads.GAIN_UNMANAGED,
            "filter_samples": 5,
            "max_jump_streak": 3,
            "flags": ads.RT_FLAG_HAS_GAIN | ads.RT_FLAG_HAS_VMIN | ads.RT_FLAG_HAS_VMAX,
            "max_jump": 0.5,
            "value_min": 0.0,
            "value_max": 14.0,
            "update_interval_ms": 60000,
            "gain_at_last_cal_save": ads.GAIN_6P144,
        })
        self.assertEqual(len(blob), 32)
        self.assertEqual(struct.calcsize(ads.CHANNEL_RUNTIME_PREFS_FORMAT), 32)
        unpacked = ads.unpack_runtime_prefs(blob)
        self.assertEqual(unpacked["magic"], 0xA0511115)
        self.assertEqual(unpacked["filter_samples"], 5)
        self.assertAlmostEqual(unpacked["max_jump"], 0.5, places=5)
        self.assertAlmostEqual(unpacked["value_min"], 0.0, places=5)
        self.assertAlmostEqual(unpacked["value_max"], 14.0, places=5)
        self.assertEqual(unpacked["update_interval_ms"], 60000)
        self.assertEqual(unpacked["ads_gain"], ads.GAIN_6P144)

    def test_cal_magic_not_bumped(self):
        with open(ENGINE_H, encoding="utf-8") as handle:
            engine = handle.read()
        self.assertIn("0xCA110005", engine)
        self.assertNotIn("0xCA110006", engine)
        self.assertNotIn("0xA0511115", engine)
        with open(PREFS_PATH, encoding="utf-8") as handle:
            prefs = handle.read()
        self.assertIn("ps-md5-rt-v1", prefs)
        self.assertIn("ps-md5-v1", prefs)
        self.assertNotIn("ps-md5-rt-v2", prefs)
        self.assertEqual(ads.CALIBRATION_PREFS_MAGIC, 0xCA110005)
        self.assertEqual(ads.RUNTIME_PREFS_MAGIC, 0xA0511115)


class MedianResizeTest(unittest.TestCase):
    def test_set_size_clears_and_refills(self):
        window = ads.SlidingWindow()
        window.set_size(5)
        for value in (1.0, 2.0, 3.0, 4.0, 5.0):
            window.push(value)
        self.assertTrue(window.is_full())
        window.set_size(3)
        self.assertEqual(window.count, 0)
        self.assertFalse(window.is_full())
        window.push(7.0)
        self.assertEqual(window.count, 1)
        self.assertFalse(window.is_full())
        window.push(8.0)
        window.push(9.0)
        self.assertTrue(window.is_full())

    def test_cpp_set_size_clears(self):
        with open(FILTER_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void SlidingWindow::set_size")
        self.assertIn("this->count_ = 0", body)
        self.assertIn("this->head_ = 0", body)


class CodegenContractTest(unittest.TestCase):
    def test_runtime_opt_in_and_interval_number(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("CONF_FILTER_RUNTIME", source)
        self.assertIn('CONF_FILTER_RUNTIME = "runtime"', source)
        self.assertIn("CONF_UPDATE_INTERVAL_NUMBER", source)
        self.assertIn("setup_filter_runtime", source)
        self.assertIn("FILTER_NUMBER_SPECS", source)
        self.assertIn("set_filters_persist", source)
        self.assertIn("set_interval_persist", source)
        self.assertIn("generate_runtime_preferences_key", source)
        self.assertNotIn('DEPENDENCIES = ["ads1115"]', source)
        self.assertNotIn("CONF_CALIBRATION_INTERVAL_NUMBER", source)

    def test_apply_distinct_from_codegen_setters(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        setter = _fn(source, "void PoolStationChannelSensor::set_filter_samples")
        apply = _fn(source, "void PoolStationChannelSensor::apply_filter_samples_runtime")
        self.assertIn("filter_config_.filter_samples = samples", setter)
        self.assertNotIn("set_size", setter)
        self.assertNotIn("sync_median_window_", setter)
        self.assertIn("sync_median_window_", apply)
        self.assertIn("set_size", _fn(source, "void PoolStationChannelSensor::sync_median_window_"))

    def test_apply_does_not_dirty_capturer_draft(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        with open(UI_CPP, encoding="utf-8") as handle:
            ui = handle.read()
        for name in (
            "apply_filter_samples_runtime",
            "apply_max_jump_runtime",
            "apply_max_jump_streak_runtime",
            "apply_value_min_runtime",
            "apply_value_max_runtime",
            "apply_update_interval_runtime",
            "reset_filters_to_yaml",
        ):
            body = _fn(source, f"void PoolStationChannelSensor::{name}")
            self.assertTrue(body, name)
            self.assertNotIn("notify_calibration_updated", body, name)
            self.assertNotIn("commit_draft", body, name)
            self.assertNotIn("discard_draft", body, name)
            self.assertNotIn("enable_draft_mode", body, name)
        control = _fn(ui, "void FilterRuntimeNumber::control")
        self.assertNotIn("notify_calibration_updated", control)
        self.assertNotIn("is_calibration_mode", control)
        reset = _fn(ui, "void FiltersResetYamlButton::press_action")
        self.assertNotIn("is_calibration_mode", reset)
        self.assertIn("reset_filters_to_yaml", reset)

    def test_lot8_still_pre_jump_guard(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        pipeline = _fn(source, "void PoolStationChannelSensor::on_source_value_")
        self.assertIn("on_channel_sample", pipeline)
        self.assertIn("compensated", pipeline)
        self.assertIn("Pre-jump-guard", pipeline)
        sample_call = re.search(
            r"on_channel_sample\(\s*static_cast<uint8_t>\(this->channel_type_\),\s*(\w+)",
            pipeline,
        )
        self.assertIsNotNone(sample_call)
        self.assertEqual(sample_call.group(1), "compensated")

    def test_no_ads_ownership_lot_c(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        self.assertNotIn("request_measurement", source)
        self.assertNotIn("set_continuous_mode", source)
        self.assertNotIn("set_multiplexer", source)
        self.assertNotIn("ads_->set_update_interval", source)

    def test_cal_mode_interval_still_priority(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        pipeline = _fn(source, "void PoolStationChannelSensor::on_source_value_")
        self.assertIn("is_calibration_mode()", pipeline)
        self.assertIn("get_calibration_interval()", pipeline)
        apply_iv = _fn(source, "void PoolStationChannelSensor::apply_update_interval_runtime")
        self.assertIn("configured_interval_", apply_iv)
        self.assertNotIn("set_calibration_interval", apply_iv)

    def test_jump_apply_resets_guard(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        jump = _fn(source, "void PoolStationChannelSensor::apply_max_jump_runtime")
        streak = _fn(source, "void PoolStationChannelSensor::apply_max_jump_streak_runtime")
        self.assertIn("sync_jump_guard_(true)", jump)
        self.assertIn("sync_jump_guard_(true)", streak)
        self.assertIn("Lot 8 still pre-guard", jump)

    def test_clamp_semantics_unchanged(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        pipeline = _fn(source, "void PoolStationChannelSensor::on_source_value_")
        self.assertIn("clamp_value", pipeline)
        self.assertNotIn("last_good", pipeline)
        vmin = _fn(source, "void PoolStationChannelSensor::apply_value_min_runtime")
        self.assertIn("clamp, not j5 reject", vmin)

    def test_save_preserves_gain_when_writing_filters(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        save = _fn(source, "void PoolStationChannelSensor::save_runtime_preferences")
        self.assertIn("ads_overlay_enabled_", save)
        self.assertIn("filters_persist_", save)
        self.assertIn("interval_persist_", save)
        self.assertIn("RT_FLAG_HAS_GAIN", save)
        self.assertIn("RT_FLAG_HAS_VMIN", save)
        self.assertIn("filter_samples", save)
        load = _fn(source, "void PoolStationChannelSensor::load_runtime_preferences")
        self.assertIn("apply_nvs_filter_fields_", load)

    def test_yaml_seed_snapshot_before_nvs(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        setup = _fn(source, "void PoolStationChannelSensor::setup")
        self.assertLess(
            setup.find("snapshot_yaml_filter_seeds_"),
            setup.find("load_runtime_preferences"),
        )
        reset = _fn(source, "void PoolStationChannelSensor::reset_filters_to_yaml")
        self.assertIn("yaml_filter_samples_", reset)
        self.assertIn("yaml_update_interval_ms_", reset)

    def test_header_declares_lot_b_apply(self):
        with open(CHANNEL_H, encoding="utf-8") as handle:
            header = handle.read()
        for name in (
            "apply_filter_samples_runtime",
            "apply_max_jump_runtime",
            "apply_max_jump_streak_runtime",
            "apply_value_min_runtime",
            "apply_value_max_runtime",
            "apply_update_interval_runtime",
            "reset_filters_to_yaml",
        ):
            self.assertIn(name, header)

    def test_number_steps_match_design(self):
        self.assertAlmostEqual(ads.FILTER_NUMBER_SPECS["ph"]["jump_step"], 0.05)
        self.assertAlmostEqual(ads.FILTER_NUMBER_SPECS["orp"]["jump_step"], 1.0)
        self.assertAlmostEqual(ads.FILTER_NUMBER_SPECS["pressure"]["jump_step"], 0.01)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
