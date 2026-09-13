#!/usr/bin/env python3
"""Unit tests for Capturer UI refresh after add/remove (v0.7.14).

notify_calibration_updated() used to be a log-only stub, so HA number entities
stayed stale after Add/Remove even though CalibrationEngine count changed.

This module is free of esphome imports. It:
  1. Models the refresh contract (slots vs live engine count)
  2. Asserts C++ notify actually calls refresh_calibration_ui
  3. Asserts codegen registers algorithm_select / point_count_number on parent

Run with: python3 tests/test_notify_calibration_updated.py
"""

import ast
import math
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CPP_PATH = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
HEADER_PATH = os.path.join(ROOT, "components", "pool_station", "pool_station.h")
UI_CPP_PATH = os.path.join(ROOT, "components", "pool_station", "calibration_ui.cpp")
INIT_PATH = os.path.join(ROOT, "components", "pool_station", "__init__.py")
README_PATH = os.path.join(ROOT, "README.md")


# =============================================================================
# Python model of refresh_calibration_ui (mirrors C++ contract)
# =============================================================================


class FakePoint:
    def __init__(self, x, y):
        self.x = x
        self.y = y

    def is_valid(self):
        return not (math.isnan(self.x) or math.isnan(self.y))


class FakeEngine:
    def __init__(self, points=None, algo="linear"):
        self._points = list(points or [])
        self.algo = algo
        self.draft_pending = False

    def get_point_count(self):
        return len(self._points)

    def get_point(self, index):
        if index >= len(self._points):
            return FakePoint(float("nan"), float("nan"))
        x, y = self._points[index]
        return FakePoint(x, y)

    def add_point(self, x, y):
        self._points.append((x, y))

    def remove_point(self, index):
        del self._points[index]

    def is_valid(self):
        return len(self._points) >= 2 and self.algo != "none"


class FakeWidget:
    def __init__(self):
        self.state = None

    def publish_state(self, value):
        self.state = value


def refresh_calibration_ui(
    engine,
    point_x,
    point_y,
    point_count_number=None,
    algorithm_select=None,
    cal_invalid=None,
    draft_pending=None,
):
    """Mirror PoolStationComponent::refresh_calibration_ui for one channel."""
    for idx, num in point_x.items():
        pt = engine.get_point(idx)
        num.publish_state(pt.x if not math.isnan(pt.x) else float("nan"))
    for idx, num in point_y.items():
        pt = engine.get_point(idx)
        num.publish_state(pt.y if not math.isnan(pt.y) else float("nan"))
    if point_count_number is not None:
        point_count_number.publish_state(float(engine.get_point_count()))
    if algorithm_select is not None:
        algorithm_select.publish_state(engine.algo)
    if cal_invalid is not None:
        cal_invalid.publish_state(not engine.is_valid())
    if draft_pending is not None:
        draft_pending.publish_state(engine.draft_pending)


def make_slots(count):
    return {i: FakeWidget() for i in range(count)}


class NotifyRefreshContractTest(unittest.TestCase):
    def test_add_point_updates_count_and_existing_slots(self):
        engine = FakeEngine([(0.5, 0.0), (2.5, 5.0), (4.5, 10.0)])
        slots = 5  # preallocated HA slots (codegen point_count)
        point_x = make_slots(slots)
        point_y = make_slots(slots)
        count = FakeWidget()
        invalid = FakeWidget()

        refresh_calibration_ui(engine, point_x, point_y, count, cal_invalid=invalid)
        self.assertEqual(count.state, 3.0)
        self.assertEqual(point_x[0].state, 0.5)
        self.assertEqual(point_y[2].state, 10.0)
        self.assertTrue(math.isnan(point_x[3].state))
        self.assertFalse(invalid.state)

        engine.add_point(0.0, 0.0)
        refresh_calibration_ui(engine, point_x, point_y, count, cal_invalid=invalid)

        self.assertEqual(count.state, 4.0)
        self.assertEqual(point_x[3].state, 0.0)
        self.assertEqual(point_y[3].state, 0.0)
        self.assertTrue(math.isnan(point_x[4].state))
        self.assertFalse(invalid.state)

    def test_remove_point_clears_last_slot_and_decrements_count(self):
        engine = FakeEngine([(0.5, 0.0), (2.5, 5.0), (4.5, 10.0)])
        point_x = make_slots(3)
        point_y = make_slots(3)
        count = FakeWidget()

        engine.remove_point(engine.get_point_count() - 1)
        refresh_calibration_ui(engine, point_x, point_y, count)

        self.assertEqual(count.state, 2.0)
        self.assertEqual(point_x[0].state, 0.5)
        self.assertEqual(point_y[1].state, 5.0)
        self.assertTrue(math.isnan(point_x[2].state))
        self.assertTrue(math.isnan(point_y[2].state))

    def test_add_beyond_codegen_slots_cannot_create_ha_entity(self):
        """Structural HA limit: only preallocated slots exist."""
        engine = FakeEngine([(0.5, 0.0), (2.5, 5.0), (4.5, 10.0)])
        point_x = make_slots(3)  # deploy YAML point_count: 3
        point_y = make_slots(3)
        count = FakeWidget()

        engine.add_point(1.0, 3.0)  # engine now has 4 points
        refresh_calibration_ui(engine, point_x, point_y, count)

        self.assertEqual(count.state, 4.0)
        self.assertEqual(set(point_x.keys()), {0, 1, 2})
        self.assertNotIn(3, point_x)

    def test_algorithm_and_draft_widgets_refresh(self):
        engine = FakeEngine([(1.0, 4.0), (1.5, 7.0)], algo="piecewise")
        engine.draft_pending = True
        algo = FakeWidget()
        draft = FakeWidget()
        refresh_calibration_ui(
            engine, {}, {}, algorithm_select=algo, draft_pending=draft
        )
        self.assertEqual(algo.state, "piecewise")
        self.assertTrue(draft.state)

    def test_capture_publishes_x_even_if_y_is_nan(self):
        """Capturer writes X first; HA must show volts before Y is edited."""
        engine = FakeEngine([(0.46, float("nan"))])
        point_x = make_slots(3)
        point_y = make_slots(3)
        refresh_calibration_ui(engine, point_x, point_y)
        self.assertAlmostEqual(point_x[0].state, 0.46)
        self.assertTrue(math.isnan(point_y[0].state))

    def test_capture_forced_slot_publish_wins(self):
        """After notify, Capturer writes raw V onto the pressed slot's X number."""
        engine = FakeEngine()
        point_x = make_slots(3)
        refresh_calibration_ui(engine, point_x, {})
        self.assertTrue(math.isnan(point_x[0].state))
        captured = 0.46
        point_x[0].publish_state(captured)
        self.assertAlmostEqual(point_x[0].state, 0.46)


class NotifyImplementationTest(unittest.TestCase):
    def test_notify_is_not_a_stub(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            source = handle.read()
        match = re.search(
            r"void PoolStationChannelSensor::notify_calibration_updated\(\)\s*\{(?P<body>.*?)^\}",
            source,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(match, "notify_calibration_updated() not found")
        body = match.group("body")
        self.assertIn("refresh_calibration_ui", body)
        self.assertNotIn("We could trigger number entity updates", body)

    def test_refresh_updates_count_points_algo_and_flags(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            source = handle.read()
        match = re.search(
            r"void PoolStationComponent::refresh_calibration_ui\([^)]*\)\s*\{(?P<body>.*?)^\}",
            source,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(match, "refresh_calibration_ui() not found")
        body = match.group("body")
        for needle in (
            "point_x_numbers_",
            "point_y_numbers_",
            "algorithm_selects_",
            "point_count_numbers_",
            "cal_invalid_sensors_",
            "draft_pending_sensors_",
            "update_from_calibration()",
        ):
            self.assertIn(needle, body, needle)

        self.assertGreaterEqual(body.count("update_from_calibration()"), 6)

    def test_header_declares_register_and_refresh(self):
        with open(HEADER_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("void refresh_calibration_ui(uint8_t channel_type);", source)
        self.assertIn("register_algorithm_select", source)
        self.assertIn("register_point_count_number", source)
        self.assertIn("register_draft_pending_sensor", source)
        self.assertIn("point_count_numbers_", source)
        self.assertIn("algorithm_selects_", source)

    def test_setup_and_save_also_notify(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            channel_cpp = handle.read()
        setup = re.search(
            r"void PoolStationChannelSensor::setup\(\)\s*\{(?P<body>.*?)^void ",
            channel_cpp,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(setup)
        setup_body = setup.group("body")
        self.assertIn("load_from_preferences", setup_body)
        self.assertIn("notify_calibration_updated", setup_body)
        self.assertLess(
            setup_body.find("load_from_preferences"),
            setup_body.find("notify_calibration_updated"),
        )

        with open(UI_CPP_PATH, encoding="utf-8") as handle:
            ui = handle.read()
        self.assertIn("channel->notify_calibration_updated()", ui)
        self.assertGreaterEqual(ui.count("notify_calibration_updated()"), 6)

    def test_capture_refreshes_x_number(self):
        with open(UI_CPP_PATH, encoding="utf-8") as handle:
            ui = handle.read()
        match = re.search(
            r"void CalibrationCaptureButton::press_action\(\)\s*\{(?P<body>.*?)^\}",
            ui,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(match, "CalibrationCaptureButton::press_action() not found")
        body = match.group("body")
        self.assertIn("set_point", body)
        self.assertIn("notify_calibration_updated", body)
        self.assertIn("publish_captured_point_x", body)
        self.assertLess(body.find("notify_calibration_updated"), body.find("publish_captured_point_x"))

        x_update = re.search(
            r"void CalibrationPointXNumber::update_from_calibration\(\)\s*\{(?P<body>.*?)^\}",
            ui,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(x_update)
        x_body = x_update.group("body")
        self.assertIn("isnan(pt.x)", x_body)
        self.assertNotIn("pt.is_valid()", x_body)

        with open(HEADER_PATH, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("publish_captured_point_x", header)


class CodegenRegistrationTest(unittest.TestCase):
    def test_codegen_registers_algo_and_point_count_on_parent(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("register_algorithm_select", source)
        self.assertIn("register_point_count_number", source)
        self.assertIn("register_draft_pending_sensor", source)

        tree = ast.parse(source)
        attrs = []

        class Visitor(ast.NodeVisitor):
            def visit_Attribute(self, node):
                attrs.append(node.attr)
                self.generic_visit(node)

        Visitor().visit(tree)
        self.assertIn("register_algorithm_select", attrs)
        self.assertIn("register_point_count_number", attrs)
        self.assertIn("register_draft_pending_sensor", attrs)
        self.assertIn("register_point_x_number", attrs)
        self.assertIn("register_point_y_number", attrs)

    def test_point_numbers_register_component_before_ha_discovery(self):
        """Capturer X/Y numbers need setup() so publish_state reaches HA (0.7.12 class)."""
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        start = source.find("# Point X number (raw voltage)")
        end = source.find("# Save button", start)
        self.assertGreater(start, 0)
        self.assertGreater(end, start)
        block = source[start:end]
        self.assertGreaterEqual(block.count("await cg.register_component("), 2)
        self.assertIn("register_point_x_number", block)
        self.assertIn("register_point_y_number", block)
        self.assertLess(block.find("set_parent("), block.find("await cg.register_component("))
        self.assertLess(
            block.find("register_point_x_number"),
            block.find("await cg.register_component("),
        )

    def test_docs_distinguish_slots_from_live_count(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            init = handle.read()
        self.assertIn("max number of HA number/button slots", init)
        self.assertIn("Recommend 5", init)

        with open(README_PATH, encoding="utf-8") as handle:
            readme = handle.read()
        self.assertIn("max HA slots", readme)
        self.assertIn("preallocated HA slots", readme)
        self.assertIn("point_count_number", readme)
        self.assertIn("cannot** create new Home Assistant entities", readme)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
