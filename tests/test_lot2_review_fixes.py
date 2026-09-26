#!/usr/bin/env python3
"""Specification + source-scan tests for v0.7.16 Lot-2 review-fix ports.

Covers:
  - HA slot-stable calibration points (no in-place sort/compact)
  - apply_precision at end of calibrate
  - is_implemented() for stub algorithms
  - nested calibration_mode set_parent
  - least-squares linear on all valid points

This module is free of esphome imports.

Run with: python3 tests/test_lot2_review_fixes.py
"""

import math
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ENGINE_CPP = os.path.join(ROOT, "components", "pool_station", "calibration_engine.cpp")
ENGINE_H = os.path.join(ROOT, "components", "pool_station", "calibration_engine.h")
INIT_PATH = os.path.join(ROOT, "components", "pool_station", "__init__.py")
SWITCH_PY = os.path.join(ROOT, "components", "pool_station", "switch.py")


# =============================================================================
# Python models (mirrors C++ contract)
# =============================================================================

IMPLEMENTED = {"none", "linear", "polynomial", "piecewise", "dfrobot_orp"}
STUBS = {"exponential", "logarithmic", "power"}


def is_implemented(cal_type):
    return cal_type in IMPLEMENTED


def is_valid(cal_type, points, min_points):
    if not is_implemented(cal_type):
        return False
    if cal_type == "none":
        return False
    if cal_type == "dfrobot_orp":
        return True
    if len(points) < min_points:
        return False
    return all(not (math.isnan(x) or math.isnan(y)) for x, y in points)


def apply_precision(value, decimals):
    if math.isnan(value):
        return value
    scale = 10.0 ** decimals
    return math.copysign(math.floor(abs(value) * scale + 0.5), value) / scale


def calibrate_linear(x, points):
    valid = [(px, py) for px, py in points if not (math.isnan(px) or math.isnan(py))]
    if len(valid) < 2:
        return x
    n = float(len(valid))
    sum_x = sum(p[0] for p in valid)
    sum_y = sum(p[1] for p in valid)
    sum_xx = sum(p[0] * p[0] for p in valid)
    sum_xy = sum(p[0] * p[1] for p in valid)
    denom = n * sum_xx - sum_x * sum_x
    if abs(denom) < 1e-12:
        return sum_y / n
    slope = (n * sum_xy - sum_x * sum_y) / denom
    intercept = (sum_y - slope * sum_x) / n
    return intercept + slope * x


def calibrate_piecewise(x, stored_points):
    """Sort a working copy only; caller keeps stored_points in slot order."""
    points = [
        p for p in stored_points if not (math.isnan(p[0]) or math.isnan(p[1]))
    ]
    points = sorted(points, key=lambda p: p[0])
    if len(points) < 2:
        return x
    if x <= points[0][0]:
        p1, p2 = points[0], points[1]
        slope = (p2[1] - p1[1]) / (p2[0] - p1[0])
        return p1[1] + (x - p1[0]) * slope
    if x >= points[-1][0]:
        p1, p2 = points[-2], points[-1]
        slope = (p2[1] - p1[1]) / (p2[0] - p1[0])
        return p2[1] + (x - p2[0]) * slope
    for i in range(len(points) - 1):
        p1, p2 = points[i], points[i + 1]
        if p1[0] <= x <= p2[0]:
            if abs(p2[0] - p1[0]) < 1e-9:
                return p1[1]
            slope = (p2[1] - p1[1]) / (p2[0] - p1[0])
            return p1[1] + (x - p1[0]) * slope
    return x


def set_point(points, index, x, y):
    """In-place slot update without sort (HA index contract)."""
    points[index] = (x, y)
    return points


class SlotStablePointsTest(unittest.TestCase):
    def test_set_point_does_not_reshuffle_indices(self):
        # YAML/HA order: slot0=pH4 (high V), slot1=pH7, slot2=pH10 (low V)
        slots = [(2.03, 4.01), (1.50, 7.00), (0.98, 10.00)]
        set_point(slots, 0, 2.10, 4.01)
        self.assertEqual(slots[0], (2.10, 4.01))
        self.assertEqual(slots[1], (1.50, 7.00))
        self.assertEqual(slots[2], (0.98, 10.00))

    def test_piecewise_uses_sorted_copy_stored_order_unchanged(self):
        slots = [(2.03, 4.01), (1.50, 7.00), (0.98, 10.00)]
        snapshot = list(slots)
        mid_v = (2.03 + 1.50) / 2
        expected = (4.01 + 7.00) / 2
        self.assertAlmostEqual(calibrate_piecewise(mid_v, slots), expected)
        self.assertEqual(slots, snapshot)

    def test_add_keeps_append_index(self):
        slots = [(2.03, 4.01), (1.50, 7.00)]
        slots.append((0.98, 10.00))
        self.assertEqual([p[1] for p in slots], [4.01, 7.00, 10.00])


class PrecisionAndLinearTest(unittest.TestCase):
    def test_calibrate_applies_precision(self):
        raw = calibrate_linear(2.5, [(0.5, 0.0), (4.5, 10.0)])
        self.assertAlmostEqual(raw, 5.0)
        self.assertAlmostEqual(apply_precision(7.126, 2), 7.13)

    def test_least_squares_not_first_last(self):
        points = [(0.0, 0.0), (2.0, 2.0), (4.0, 0.0)]
        self.assertAlmostEqual(calibrate_linear(2.0, points), 2.0 / 3.0)


class ImplementedAlgoTest(unittest.TestCase):
    def test_stubs_make_is_valid_false(self):
        pts = [(1.0, 1.0), (2.0, 2.0)]
        for stub in STUBS:
            self.assertFalse(is_implemented(stub))
            self.assertFalse(is_valid(stub, pts, 2))

    def test_linear_with_points_is_valid(self):
        self.assertTrue(is_implemented("linear"))
        self.assertTrue(is_valid("linear", [(1.0, 1.0), (2.0, 2.0)], 2))

    def test_none_implemented_but_invalid(self):
        self.assertTrue(is_implemented("none"))
        self.assertFalse(is_valid("none", [], 0))


class SourceScanTest(unittest.TestCase):
    def setUp(self):
        with open(ENGINE_CPP, encoding="utf-8") as handle:
            self.cpp = handle.read()
        with open(ENGINE_H, encoding="utf-8") as handle:
            self.header = handle.read()
        with open(INIT_PATH, encoding="utf-8") as handle:
            self.init = handle.read()

    def test_no_inplace_sort_of_stored_slots(self):
        self.assertNotIn("std::sort(this->live_points_", self.cpp)
        self.assertNotIn("std::sort(points.begin()", self.cpp)
        self.assertIn("std::sort(out.begin()", self.cpp)
        self.assertIn("valid_points_copy_", self.cpp)

    def test_load_does_not_compact_invalid_slots(self):
        load = self.cpp[self.cpp.find("void CalibrationEngine::load_from_preferences") :]
        load = load.split("void CalibrationEngine::save_to_preferences")[0]
        self.assertNotIn("if (pt.is_valid())", load)
        self.assertIn("this->live_points_.push_back(pt);", load)

    def test_calibrate_applies_precision(self):
        self.assertIn("apply_precision_", self.cpp)
        cal = self.cpp[self.cpp.find("float CalibrationEngine::calibrate(") :]
        cal = cal.split("float CalibrationEngine::calibrate_linear_")[0]
        self.assertIn("return this->apply_precision_(result);", cal)

    def test_is_implemented_declared_and_used(self):
        self.assertIn("is_type_implemented", self.header)
        self.assertIn("bool is_implemented() const", self.header)
        self.assertIn("if (!is_type_implemented(type))", self.cpp)
        self.assertIn("case CAL_TYPE_EXPONENTIAL:", self.cpp)
        impl = self.cpp[self.cpp.find("CalibrationEngine::is_type_implemented") :]
        impl = impl.split("bool CalibrationEngine::is_valid")[0]
        self.assertIn("return false;", impl)

    def test_nested_calibration_mode_binds_parent(self):
        start = self.init.find("# Setup calibration mode switch")
        self.assertGreater(start, 0)
        end = self.init.find("# Setup channels", start)
        block = self.init[start:end]
        self.assertIn("cal_switch.set_parent(var)", block)
        self.assertIn("var.set_calibration_mode_switch(cal_switch)", block)
        # Do not reintroduce double-register on the nested path.
        self.assertNotIn("await cg.register_component(cal_switch", block)

    def test_standalone_switch_still_binds_parent(self):
        with open(SWITCH_PY, encoding="utf-8") as handle:
            switch_py = handle.read()
        self.assertIn("var.set_parent(parent)", switch_py)
        self.assertIn("parent.set_calibration_mode_switch(var)", switch_py)

    def test_linear_is_least_squares(self):
        # v0.10.6: least-squares lives in ensure_linear_cache_ (cached hot path)
        linear = self.cpp[self.cpp.find("void CalibrationEngine::ensure_linear_cache_") :]
        linear = linear.split("float CalibrationEngine::calibrate_linear_")[0]
        self.assertIn("sum_xy", linear)
        self.assertNotIn("live_points_.front()", linear)
        self.assertNotIn("live_points_.back()", linear)
        cal = self._fn_linear()
        self.assertIn("ensure_linear_cache_", cal)

    def _fn_linear(self):
        start = self.cpp.find("float CalibrationEngine::calibrate_linear_")
        end = self.cpp.find("float CalibrationEngine::calibrate_polynomial_", start)
        return self.cpp[start:end]

    def test_init_does_not_register_component_on_numbers(self):
        """Guard against the Lot-2 WIP / v0.7.14 double-register regression."""
        start = self.init.find("# Point X number (raw voltage)")
        end = self.init.find("# Save button", start)
        block = self.init[start:end]
        self.assertNotIn("await cg.register_component(", block)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
