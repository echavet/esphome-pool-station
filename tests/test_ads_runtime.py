#!/usr/bin/env python3
"""Lot A unit tests: FSR table, saturation hysteresis, ads: source validator.

Mirrors ads_runtime.h / ads_runtime.py. No ESPHome import.

Run with: python3 tests/test_ads_runtime.py
"""

import ast
import math
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HELPER_PATH = os.path.join(ROOT, "components", "pool_station", "ads_runtime.py")
ADS_H = os.path.join(ROOT, "components", "pool_station", "ads_runtime.h")
INIT_PATH = os.path.join(ROOT, "components", "pool_station", "__init__.py")
ENGINE_H = os.path.join(ROOT, "components", "pool_station", "calibration_engine.h")
UI_CPP = os.path.join(ROOT, "components", "pool_station", "calibration_ui.cpp")


def _load_helper():
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "ads_runtime", os.path.abspath(HELPER_PATH)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


ads = _load_helper()

# Live Eric rail: 4.094 V @ gain 4.096 = 99.95 % FSR
LIVE_RAIL_V = 4.094
LIVE_FSR = 4.096

FSR_TABLE = {
    0b000: 6.144,
    0b001: 4.096,
    0b010: 2.048,
    0b011: 1.024,
    0b100: 0.512,
    0b101: 0.256,
}


class AdsFsrTableTest(unittest.TestCase):
    def test_fsr_matches_esphome_pga(self):
        for code, fsr in FSR_TABLE.items():
            self.assertAlmostEqual(ads.fsr_volts_for_gain(code), fsr, places=6)

    def test_98_percent_thresholds(self):
        expected = {
            0b000: 6.02112,
            0b001: 4.01408,
            0b010: 2.00704,
            0b011: 1.00352,
            0b100: 0.50176,
            0b101: 0.25088,
        }
        for code, thresh in expected.items():
            fsr = ads.fsr_volts_for_gain(code)
            self.assertAlmostEqual(0.98 * fsr, thresh, places=5)
            self.assertTrue(ads.above_sat_on(thresh, fsr, 0.98))
            self.assertFalse(ads.above_sat_on(thresh - 0.001, fsr, 0.98))

    def test_live_rail_would_have_screamed(self):
        self.assertGreater(ads.fsr_percent(LIVE_RAIL_V, LIVE_FSR), 99.9)
        self.assertTrue(ads.above_sat_on(LIVE_RAIL_V, LIVE_FSR, 0.98))

    def test_6p144_clears_live_rail(self):
        fsr = ads.fsr_volts_for_gain(ads.GAIN_6P144)
        self.assertFalse(ads.above_sat_on(LIVE_RAIL_V, fsr, 0.98))
        self.assertLess(ads.fsr_percent(LIVE_RAIL_V, fsr), 70.0)

    def test_header_fsr_literals_match(self):
        with open(ADS_H, encoding="utf-8") as handle:
            source = handle.read()
        for fsr in FSR_TABLE.values():
            self.assertIn(f"{fsr}f", source)

    def test_header_magic_is_sidecar_not_cal(self):
        with open(ADS_H, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("0xA0511115", source)
        with open(ENGINE_H, encoding="utf-8") as handle:
            engine = handle.read()
        self.assertIn("0xCA110005", engine)
        self.assertNotIn("0xA0511115", engine)
        self.assertEqual(ads.RUNTIME_PREFS_MAGIC, 0xA0511115)
        self.assertEqual(ads.CALIBRATION_PREFS_MAGIC, 0xCA110005)
        self.assertNotEqual(ads.RUNTIME_PREFS_MAGIC, ads.CALIBRATION_PREFS_MAGIC)


class AdsHysteresisTest(unittest.TestCase):
    def test_on_at_98_off_at_95(self):
        fsr = 4.096
        state = False
        state = ads.saturation_hysteresis(0.97 * fsr, fsr, 0.98, 0.95, state)
        self.assertFalse(state)
        state = ads.saturation_hysteresis(0.98 * fsr, fsr, 0.98, 0.95, state)
        self.assertTrue(state)
        # Deadband: stay ON between 95 and 98
        state = ads.saturation_hysteresis(0.96 * fsr, fsr, 0.98, 0.95, state)
        self.assertTrue(state)
        state = ads.saturation_hysteresis(0.95 * fsr, fsr, 0.98, 0.95, state)
        self.assertFalse(state)

    def test_nan_and_zero_fsr(self):
        # NaN / invalid FSR keep the previous latch (do not clear a rail alarm).
        self.assertTrue(ads.saturation_hysteresis(math.nan, 4.096, 0.98, 0.95, True))
        self.assertFalse(ads.saturation_hysteresis(math.nan, 4.096, 0.98, 0.95, False))
        self.assertTrue(ads.saturation_hysteresis(4.0, 0.0, 0.98, 0.95, True))
        self.assertFalse(ads.saturation_hysteresis(4.0, 0.0, 0.98, 0.95, False))
        self.assertFalse(ads.above_sat_on(math.nan, 4.096))
        self.assertTrue(math.isnan(ads.fsr_percent(math.nan, 4.096)))

    def test_hold_expiry_is_uint32_wrap_safe(self):
        hold = 15000
        self.assertFalse(ads.saturation_hold_expired(100, 0, hold))
        self.assertTrue(ads.saturation_hold_expired(15000, 0, hold))
        last_on = 0xFFFFFFF0
        now_short = (last_on + 200) & 0xFFFFFFFF
        self.assertFalse(ads.saturation_hold_expired(now_short, last_on, hold))
        now_expired = (last_on + 20000) & 0xFFFFFFFF
        self.assertTrue(ads.saturation_hold_expired(now_expired, last_on, hold))


class AdsSourceValidatorTest(unittest.TestCase):
    def test_ads1115_type_accepted(self):
        self.assertTrue(ads.source_type_is_ads1115_sensor("ADS1115Sensor"))
        self.assertTrue(
            ads.source_type_is_ads1115_sensor("esphome::ads1115::ADS1115Sensor")
        )

    def test_non_ads_rejected(self):
        self.assertFalse(ads.source_type_is_ads1115_sensor("Sensor"))
        self.assertFalse(ads.source_type_is_ads1115_sensor("DallasTemperatureSensor"))
        self.assertFalse(ads.source_type_is_ads1115_sensor(""))
        self.assertFalse(ads.source_type_is_ads1115_sensor("FakeADS1115Sensor"))
        self.assertFalse(ads.source_type_is_ads1115_sensor("ADS1115SensorWrapper"))

    def test_overlay_error_only_when_ads_block(self):
        self.assertIsNone(ads.ads_overlay_source_error(False, "Sensor"))
        self.assertIsNone(ads.ads_overlay_source_error(True, "ADS1115Sensor"))
        err = ads.ads_overlay_source_error(True, "Sensor")
        self.assertIsNotNone(err)
        self.assertIn("ads1115", err.lower())

    def test_id_type_name_from_mock(self):
        class TypeObj:
            _name = "ADS1115Sensor"

        class IdObj:
            type = TypeObj()

        self.assertEqual(ads.id_type_name(IdObj()), "ADS1115Sensor")
        self.assertEqual(ads.id_type_name(None), "")

    def test_inherits_from_true_accepted_when_importable(self):
        class FakeAds:
            pass

        class TypeYes:
            _name = "NotASubstringMatch"

            def inherits_from(self, cls):
                return cls is FakeAds

        class IdYes:
            type = TypeYes()

        orig = ads._try_import_ads1115_sensor
        ads._try_import_ads1115_sensor = lambda: FakeAds
        try:
            self.assertTrue(ads.source_id_is_ads1115_sensor(IdYes()))
        finally:
            ads._try_import_ads1115_sensor = orig

    def test_inherits_false_falls_through_to_type_name(self):
        """inherits_from False must not skip type-name / platform fallbacks."""

        class FakeAds:
            pass

        class TypeSpoof:
            _name = "ADS1115Sensor"

            def inherits_from(self, cls):
                return False

        class IdSpoof:
            type = TypeSpoof()

        orig = ads._try_import_ads1115_sensor
        ads._try_import_ads1115_sensor = lambda: FakeAds
        try:
            self.assertTrue(ads.source_id_is_ads1115_sensor(IdSpoof()))
        finally:
            ads._try_import_ads1115_sensor = orig

    def test_inherits_false_plus_platform_ads1115_ok(self):
        class FakeAds:
            pass

        class TypeObj:
            _name = "Sensor"

            def inherits_from(self, cls):
                return False

        class IdObj:
            id = "ads_ph"
            type = TypeObj()

        orig = ads._try_import_ads1115_sensor
        ads._try_import_ads1115_sensor = lambda: FakeAds
        try:
            self.assertTrue(
                ads.source_id_is_ads1115_sensor(IdObj(), platform="ads1115")
            )
            self.assertTrue(
                ads.source_id_is_ads1115_sensor(
                    IdObj(),
                    sensor_entries=[{"id": "ads_ph", "platform": "ads1115"}],
                )
            )
        finally:
            ads._try_import_ads1115_sensor = orig

    def test_inherits_from_throws_falls_through_to_platform(self):
        class FakeAds:
            pass

        class TypeObj:
            _name = "Sensor"

            def inherits_from(self, cls):
                raise RuntimeError("ADS1115Sensor not the declared ID type")

        class IdObj:
            id = "ads_orp"
            type = TypeObj()

        orig = ads._try_import_ads1115_sensor
        ads._try_import_ads1115_sensor = lambda: FakeAds
        try:
            self.assertTrue(
                ads.source_id_is_ads1115_sensor(IdObj(), platform="ads1115")
            )
        finally:
            ads._try_import_ads1115_sensor = orig

    def test_ads1115_source_plus_ads_ok(self):
        class TypeObj:
            _name = "Sensor"

        class IdObj:
            id = "ads_ph"
            type = TypeObj()

        entries = [
            {"id": "ads_ph", "platform": "ads1115"},
            {"id": "water_temp", "platform": "dallas"},
        ]
        source = IdObj()
        self.assertTrue(ads.source_id_is_ads1115_sensor(source, sensor_entries=entries))
        self.assertIsNone(
            ads.ads_overlay_channel_error(
                {"source_id": source, "ads": {"gain": 4.096}},
                sensor_entries=entries,
            )
        )

    def test_dallas_plus_ads_invalid(self):
        class TypeObj:
            _name = "Sensor"

        class IdObj:
            id = "water_temp"
            type = TypeObj()

        entries = [{"id": "water_temp", "platform": "dallas"}]
        source = IdObj()
        self.assertFalse(ads.source_id_is_ads1115_sensor(source, sensor_entries=entries))
        err = ads.ads_overlay_channel_error(
            {"source_id": source, "ads": {"gain": 4.096}},
            sensor_entries=entries,
        )
        self.assertIsNotNone(err)
        self.assertIn("ads1115", err.lower())
        self.assertIn("overlay", err.lower())

    def test_wifi_signal_plus_ads_invalid(self):
        class TypeObj:
            _name = "Sensor"

        class IdObj:
            id = "wifi_rssi"
            type = TypeObj()

        entries = [{"id": "wifi_rssi", "platform": "wifi_signal"}]
        err = ads.ads_overlay_channel_error(
            {"source_id": IdObj(), "ads": {"gain": 4.096}},
            sensor_entries=entries,
        )
        self.assertIsNotNone(err)
        self.assertFalse(
            ads.source_id_is_ads1115_sensor(IdObj(), sensor_entries=entries)
        )

    def test_dallas_without_ads_ok(self):
        class TypeObj:
            _name = "Sensor"

        class IdObj:
            id = "water_temp"
            type = TypeObj()

        self.assertIsNone(
            ads.ads_overlay_channel_error(
                {"source_id": IdObj()},
                sensor_entries=[{"id": "water_temp", "platform": "dallas"}],
            )
        )

    def test_source_id_falls_back_to_type_name_without_import(self):
        class TypeObj:
            _name = "ADS1115Sensor"

        class IdObj:
            type = TypeObj()

        self.assertTrue(ads.source_id_is_ads1115_sensor(IdObj()))
        self.assertFalse(ads.source_id_is_ads1115_sensor(None))

    def test_sensor_platform_lookup_and_generic_type(self):
        class IdObj:
            id = "ads_pressure"

        self.assertEqual(
            ads.sensor_platform_for_id(
                IdObj(),
                [{"id": "ads_pressure", "platform": "ads1115"}],
            ),
            "ads1115",
        )
        self.assertTrue(ads.source_platform_is_ads1115("ads1115"))
        self.assertTrue(ads.source_platform_is_ads1115("ADS1115"))
        self.assertFalse(ads.source_platform_is_ads1115("fake_ads1115"))
        self.assertFalse(ads.source_platform_is_ads1115("dallas"))
        self.assertTrue(ads.is_generic_sensor_type_name("Sensor"))
        self.assertTrue(ads.is_generic_sensor_type_name("esphome::sensor::Sensor"))
        self.assertTrue(ads.is_generic_sensor_type_name("<esphome.codegen.MockType object>"))
        self.assertFalse(ads.is_generic_sensor_type_name("DallasTemperatureSensor"))
        self.assertEqual(ads.normalize_config_id(IdObj()), "ads_pressure")
        self.assertEqual(ads.normalize_config_id("ads_ph"), "ads_ph")


class AdsCodegenContractTest(unittest.TestCase):
    def test_gain_select_options_non_empty(self):
        self.assertEqual(len(ads.GAIN_SELECT_OPTIONS), 6)
        self.assertIn("6.144", ads.GAIN_SELECT_OPTIONS)
        self.assertIn("0.256", ads.GAIN_SELECT_OPTIONS)

    def test_init_registers_gain_select_with_options(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("CONF_ADS", source)
        self.assertIn("setup_ads_overlay", source)
        self.assertIn("GAIN_SELECT_OPTIONS", source)
        self.assertIn("generate_runtime_preferences_key", source)
        self.assertNotIn('DEPENDENCIES = ["ads1115"]', source)
        self.assertIn("DEPENDENCIES = []", source)
        self.assertIn("ensure_use_ads1115_define", source)
        self.assertIn("cg.add_define", source)
        auto_load = source.split("AUTO_LOAD")[1].split("\n", 1)[0]
        self.assertNotIn("ads1115", auto_load)

    def test_register_select_gain_not_empty(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        tree = ast.parse(source)
        options_args = []

        class Visitor(ast.NodeVisitor):
            def visit_Call(self, node):
                func = node.func
                if isinstance(func, ast.Attribute) and func.attr == "register_select":
                    for kw in node.keywords:
                        if kw.arg == "options":
                            options_args.append(ast.unparse(kw.value))
                self.generic_visit(node)

        Visitor().visit(tree)
        self.assertGreaterEqual(len(options_args), 2)
        self.assertTrue(any("GAIN_SELECT_OPTIONS" in arg for arg in options_args))
        self.assertFalse(any(arg in {"[]", "list()"} for arg in options_args))

    def test_save_refuses_saturation_and_remembers_gain(self):
        with open(UI_CPP, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("refuse_calibration_save_if_saturated", source)
        self.assertIn("remember_gain_at_cal_save", source)
        self.assertIn("Calibration Mode ON", source)
        self.assertIn("get_saturation_on()", source)
        self.assertNotIn("100.0f * ads_runtime::SAT_ON_DEFAULT", source)

    def test_no_cal_magic_bump(self):
        with open(ENGINE_H, encoding="utf-8") as handle:
            source = handle.read()
        self.assertRegex(source, r"0xCA110005")
        self.assertNotIn("0xCA110006", source)

    def test_cpp_does_not_rescale_cal_points(self):
        cpp = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
        with open(cpp, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("calibration volts unchanged", source)
        self.assertNotIn("live_points_", source.split("apply_gain_runtime")[1][:800])

    def test_apply_gain_runtime_persists_even_if_unchanged(self):
        cpp = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
        with open(cpp, encoding="utf-8") as handle:
            source = handle.read()
        apply = source.split("void PoolStationChannelSensor::apply_gain_runtime")[1].split(
            "void PoolStationChannelSensor::reset_ads_to_yaml"
        )[0]
        self.assertIn("if (persist)", apply)
        self.assertNotIn("persist && changed", apply)
        self.assertIn("is_calibration_mode()", apply)

    def test_overlay_enable_requires_ads_bind(self):
        cpp = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
        with open(cpp, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("enabled && (this->ads_ != nullptr)", source)

    def test_hold_uses_wrap_safe_helper(self):
        cpp = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
        with open(cpp, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("saturation_hold_expired", source)

    def test_sidecar_packed_layout_is_32_and_aligned(self):
        import struct

        self.assertEqual(ads.CHANNEL_RUNTIME_PREFS_SIZE, 32)
        self.assertEqual(ads.CHANNEL_RUNTIME_PREFS_FLOAT_OFFSET, 12)
        self.assertEqual(struct.calcsize(ads.CHANNEL_RUNTIME_PREFS_FORMAT), 32)
        with open(ADS_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("pad_align_[2]", header)
        self.assertIn("alignas(4)", header)
        self.assertIn("alignof(ChannelRuntimePrefsData) >= 4", header)
        self.assertIn("sizeof(ChannelRuntimePrefsData) == 32", header)
        self.assertIn("offsetof(ChannelRuntimePrefsData, max_jump) == 12", header)
        self.assertIn("ADS1115_GAIN_6P144", header)
        self.assertIn("USE_ADS1115", header)

    def test_validator_walks_core_config_not_only_inherits(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("FINAL_VALIDATE_SCHEMA", source)
        self.assertIn("final_validate_ads_overlay", source)
        self.assertIn("try_core_sensor_entries", source)
        self.assertIn("inherits_from(ADS1115Sensor)", source)
        self.assertIn("ads_overlay_channel_error", source)
        with open(HELPER_PATH, encoding="utf-8") as handle:
            helper = handle.read()
        self.assertIn("source_id_is_ads1115_sensor", helper)
        self.assertIn("sensor_platform_for_id", helper)
        # inherits False / exception must fall through (no early return False)
        self.assertNotIn("except Exception:\n            return False", helper)


class UseAds1115DefineTest(unittest.TestCase):
    """Codegen path for USE_ADS1115 — no ESPHome import, mock add_define."""

    def test_ads_block_emits_define(self):
        added = []
        ok = ads.ensure_use_ads1115_define(
            {"channels": {"ph": {"source_id": "ads_ph", "ads": {"gain": 4.096}}}},
            added.append,
        )
        self.assertTrue(ok)
        self.assertEqual(added, [ads.USE_ADS1115_DEFINE])
        self.assertEqual(ads.USE_ADS1115_DEFINE, "USE_ADS1115")

    def test_empty_ads_dict_still_emits_define(self):
        added = []
        self.assertTrue(
            ads.ensure_use_ads1115_define(
                {"channels": {"orp": {"source_id": "ads_orp", "ads": {}}}},
                added.append,
            )
        )
        self.assertEqual(added, ["USE_ADS1115"])

    def test_ads1115_source_without_ads_emits_define(self):
        added = []

        class IdObj:
            id = "ads_ph"

        ok = ads.ensure_use_ads1115_define(
            {"channels": {"ph": {"source_id": IdObj()}}},
            added.append,
            sensor_entries=[{"id": "ads_ph", "platform": "ads1115"}],
        )
        self.assertTrue(ok)
        self.assertEqual(added, ["USE_ADS1115"])

    def test_dallas_only_does_not_emit_define(self):
        added = []

        class IdObj:
            id = "water_temp"

        ok = ads.ensure_use_ads1115_define(
            {"channels": {"ph": {"source_id": IdObj()}}},
            added.append,
            sensor_entries=[{"id": "water_temp", "platform": "dallas"}],
        )
        self.assertFalse(ok)
        self.assertEqual(added, [])

    def test_empty_config_no_define(self):
        added = []
        self.assertFalse(ads.ensure_use_ads1115_define({}, added.append))
        self.assertFalse(ads.ensure_use_ads1115_define(None, added.append))
        self.assertFalse(ads.ensure_use_ads1115_define({"channels": {}}, added.append))
        self.assertFalse(ads.config_needs_use_ads1115({}))
        self.assertEqual(added, [])

    def test_add_define_none_is_noop(self):
        self.assertFalse(
            ads.ensure_use_ads1115_define(
                {"channels": {"ph": {"ads": {"gain": 4.096}}}},
                None,
            )
        )

    def test_config_needs_define_from_ads_or_platform(self):
        self.assertTrue(
            ads.config_needs_use_ads1115(
                {"channels": {"orp": {"source_id": "ads_orp", "ads": {}}}}
            )
        )
        self.assertTrue(
            ads.config_needs_use_ads1115(
                {"channels": {"ph": {"source_id": "ads_ph"}}},
                sensor_entries=[{"id": "ads_ph", "platform": "ads1115"}],
            )
        )
        self.assertFalse(
            ads.config_needs_use_ads1115(
                {"channels": {"ph": {"source_id": "water_temp"}}},
                sensor_entries=[{"id": "water_temp", "platform": "dallas"}],
            )
        )


class AdsGainRoundTripTest(unittest.TestCase):
    def test_label_code_roundtrip(self):
        for label in ads.GAIN_SELECT_OPTIONS:
            code = ads.gain_label_to_code(label)
            self.assertEqual(ads.gain_code_to_label(code), label)
            self.assertEqual(ads.gain_float_to_code(float(label)), code)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
