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
        self.assertFalse(ads.saturation_hysteresis(math.nan, 4.096, 0.98, 0.95, True))
        self.assertFalse(ads.saturation_hysteresis(4.0, 0.0, 0.98, 0.95, True))
        self.assertFalse(ads.above_sat_on(math.nan, 4.096))
        self.assertTrue(math.isnan(ads.fsr_percent(math.nan, 4.096)))


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


class AdsGainRoundTripTest(unittest.TestCase):
    def test_label_code_roundtrip(self):
        for label in ads.GAIN_SELECT_OPTIONS:
            code = ads.gain_label_to_code(label)
            self.assertEqual(ads.gain_code_to_label(code), label)
            self.assertEqual(ads.gain_float_to_code(float(label)), code)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
