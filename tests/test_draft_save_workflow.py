#!/usr/bin/env python3
"""Draft/Save calibration UX (v0.7.17 / v0.7.18).

Eric's contract:
  - With draft_mode true, point/count/capture/algo edits touch draft only
  - Published calibrate() / is_valid() keep the last committed live set
  - Publish gate uses live type (draft algo none must not drop live cal)
  - Save = commit draft → live + prefs + clear dirty, only if draft is valid
  - Discard restores draft from live (browser refresh is not undo)
  - draft_pending is true while dirty

This module is free of esphome imports. It:
  1. Models CalibrationEngine draft/commit/discard/save
  2. Asserts C++ Save commits when draft_mode is on
  3. Asserts prefs load resyncs draft from live

Run with: python3 tests/test_draft_save_workflow.py
"""

import math
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ENGINE_CPP = os.path.join(ROOT, "components", "pool_station", "calibration_engine.cpp")
ENGINE_H = os.path.join(ROOT, "components", "pool_station", "calibration_engine.h")
UI_CPP = os.path.join(ROOT, "components", "pool_station", "calibration_ui.cpp")
CHANNEL_CPP = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
README_PATH = os.path.join(ROOT, "README.md")
MIGRATION_PATH = os.path.join(ROOT, "docs", "MIGRATION.md")
CHANGELOG_PATH = os.path.join(ROOT, "CHANGELOG.md")
EXAMPLE_YAML = os.path.join(ROOT, "examples", "pool-station-minimal.yaml")


# =============================================================================
# Python model (mirrors C++ CalibrationEngine draft/live split)
# =============================================================================


def _linear(x, points):
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


class DraftEngine:
    """Minimal live/draft engine used by the UX tests."""

    def __init__(self, points, algo="linear", draft_mode=False):
        self.live_points = list(points)
        self.draft_points = list(points)
        self.live_type = algo
        self.type = algo
        self.draft_mode_enabled = False
        self.has_draft_changes = False
        self.saved_points = None
        self.saved_type = None
        if draft_mode:
            self.enable_draft_mode()

    def _working(self):
        return self.draft_points if self.draft_mode_enabled else self.live_points

    def enable_draft_mode(self):
        if self.draft_mode_enabled:
            return
        self.live_type = self.type
        self.draft_mode_enabled = True
        self.sync_draft_from_live()

    def sync_draft_from_live(self):
        self.draft_points = list(self.live_points)
        self.type = self.live_type
        self.has_draft_changes = False

    def mark_dirty(self):
        if self.draft_mode_enabled:
            self.has_draft_changes = True

    def get_point_count(self):
        return len(self._working())

    def get_live_point_count(self):
        return len(self.live_points)

    def get_point(self, index):
        pts = self._working()
        if index >= len(pts):
            return (float("nan"), float("nan"))
        return pts[index]

    def add_point(self, x, y):
        self._working().append((x, y))
        self.mark_dirty()

    def set_point(self, index, x, y):
        pts = self._working()
        if index >= len(pts):
            return
        pts[index] = (x, y)
        self.mark_dirty()

    def remove_point(self, index):
        pts = self._working()
        if index >= len(pts):
            return
        del pts[index]
        self.mark_dirty()

    def set_point_count(self, target):
        target = max(1, min(10, int(target)))
        while self.get_point_count() < target:
            self.add_point(0.0, 0.0)
        while self.get_point_count() > target:
            self.remove_point(self.get_point_count() - 1)

    def set_type_runtime(self, algo):
        if self.type == algo:
            return
        self.type = algo
        if self.draft_mode_enabled:
            self.mark_dirty()
        else:
            self.live_type = algo

    def is_valid(self):
        if self.live_type == "none":
            return False
        if self.live_type == "dfrobot_orp":
            return True
        min_pts = 3 if self.live_type == "polynomial" else 2
        if len(self.live_points) < min_pts:
            return False
        return all(not (math.isnan(x) or math.isnan(y)) for x, y in self.live_points)

    def is_draft_valid(self):
        pts = self._working()
        if self.type == "none":
            return False
        if self.type == "dfrobot_orp":
            return True
        min_pts = 3 if self.type == "polynomial" else 2
        if len(pts) < min_pts:
            return False
        return all(not (math.isnan(x) or math.isnan(y)) for x, y in pts)

    def is_draft_invalid(self):
        return self.draft_mode_enabled and not self.is_draft_valid()

    def calibrate(self, raw):
        if self.live_type == "linear":
            return _linear(raw, self.live_points)
        return raw

    def published_value(self, raw, channel="ph"):
        # Mirrors on_source_value_ Step 4: live type + is_valid(), not draft type.
        if self.live_type != "none" and self.is_valid():
            return self.calibrate(raw)
        if channel == "orp":
            return raw * 1000.0
        return raw

    def commit_draft(self):
        if not self.draft_mode_enabled:
            return
        if not self.is_draft_valid():
            return
        self.live_points = list(self.draft_points)
        self.live_type = self.type
        self.has_draft_changes = False
        self.save_to_preferences()

    def discard_draft(self):
        if not self.draft_mode_enabled:
            return
        self.sync_draft_from_live()

    def save_to_preferences(self):
        self.saved_points = list(self.live_points)
        self.saved_type = self.live_type

    def load_from_preferences(self):
        if self.saved_points is None:
            return
        self.live_points = list(self.saved_points)
        self.live_type = self.saved_type
        self.type = self.saved_type
        if self.draft_mode_enabled:
            self.sync_draft_from_live()

    def save_button(self):
        if self.draft_mode_enabled:
            if not self.is_draft_valid():
                return
            self.commit_draft()
        else:
            self.save_to_preferences()


SEED = [(0.5, 0.0), (2.5, 5.0), (4.5, 10.0)]


class DraftSaveContractTest(unittest.TestCase):
    def test_reduce_point_count_without_save_then_discard_restores(self):
        engine = DraftEngine(SEED, draft_mode=True)
        live_before = list(engine.live_points)
        published = engine.calibrate(2.5)

        engine.set_point_count(1)
        self.assertEqual(engine.get_point_count(), 1)
        self.assertTrue(engine.has_draft_changes)
        self.assertEqual(engine.live_points, live_before)
        self.assertAlmostEqual(engine.calibrate(2.5), published)
        self.assertTrue(engine.is_valid())
        self.assertFalse(engine.is_draft_valid())

        engine.discard_draft()
        self.assertEqual(engine.get_point_count(), 3)
        self.assertEqual(engine.draft_points, live_before)
        self.assertEqual(engine.live_points, live_before)
        self.assertFalse(engine.has_draft_changes)
        self.assertAlmostEqual(engine.calibrate(2.5), published)

    def test_save_persists_draft_to_live_and_prefs(self):
        engine = DraftEngine(SEED, draft_mode=True)
        engine.set_point_count(2)
        engine.set_point(0, 1.0, 2.0)
        engine.set_point(1, 3.0, 8.0)
        self.assertNotEqual(engine.live_points, engine.draft_points)

        engine.save_button()

        self.assertEqual(engine.live_points, [(1.0, 2.0), (3.0, 8.0)])
        self.assertEqual(engine.saved_points, engine.live_points)
        self.assertFalse(engine.has_draft_changes)
        self.assertAlmostEqual(engine.calibrate(1.0), 2.0)
        self.assertAlmostEqual(engine.calibrate(3.0), 8.0)

    def test_calibrate_uses_live_until_commit(self):
        engine = DraftEngine(SEED, draft_mode=True)
        before = engine.calibrate(2.5)

        engine.set_point(1, 2.5, 99.0)
        self.assertAlmostEqual(engine.calibrate(2.5), before)
        self.assertEqual(engine.live_points[1], (2.5, 5.0))
        self.assertEqual(engine.get_point(1), (2.5, 99.0))

        engine.commit_draft()
        self.assertEqual(engine.live_points[1], (2.5, 99.0))
        self.assertNotAlmostEqual(engine.calibrate(2.5), before)

    def test_algorithm_change_is_draft_only_until_save(self):
        engine = DraftEngine(SEED, algo="linear", draft_mode=True)
        before = engine.calibrate(2.5)

        engine.set_type_runtime("piecewise")
        self.assertEqual(engine.type, "piecewise")
        self.assertEqual(engine.live_type, "linear")
        self.assertTrue(engine.has_draft_changes)
        self.assertAlmostEqual(engine.calibrate(2.5), before)

        engine.discard_draft()
        self.assertEqual(engine.type, "linear")
        self.assertFalse(engine.has_draft_changes)

        engine.set_type_runtime("piecewise")
        engine.save_button()
        self.assertEqual(engine.live_type, "piecewise")
        self.assertEqual(engine.saved_type, "piecewise")
        self.assertFalse(engine.has_draft_changes)

    def test_capture_marks_dirty_without_mutating_live(self):
        engine = DraftEngine(SEED, draft_mode=True)
        engine.set_point(0, 0.46, 0.0)  # capture writes X, keeps Y
        self.assertTrue(engine.has_draft_changes)
        self.assertEqual(engine.live_points[0], (0.5, 0.0))
        self.assertEqual(engine.get_point(0), (0.46, 0.0))

    def test_prefs_load_resyncs_draft(self):
        engine = DraftEngine(SEED, draft_mode=True)
        engine.save_to_preferences()
        engine.set_point_count(1)
        self.assertEqual(engine.get_point_count(), 1)

        engine.load_from_preferences()
        self.assertEqual(engine.live_points, SEED)
        self.assertEqual(engine.draft_points, SEED)
        self.assertFalse(engine.has_draft_changes)

    def test_legacy_save_without_draft_writes_live(self):
        engine = DraftEngine(SEED, draft_mode=False)
        engine.set_point_count(1)
        self.assertEqual(engine.live_points, [(0.5, 0.0)])
        self.assertFalse(engine.has_draft_changes)
        engine.save_button()
        self.assertEqual(engine.saved_points, [(0.5, 0.0)])

    def test_draft_algo_none_keeps_published_live_calibration(self):
        engine = DraftEngine(SEED, draft_mode=True)
        before = engine.published_value(2.5)
        engine.set_type_runtime("none")
        self.assertEqual(engine.type, "none")
        self.assertEqual(engine.live_type, "linear")
        self.assertTrue(engine.is_valid())
        self.assertFalse(engine.is_draft_valid())
        self.assertTrue(engine.is_draft_invalid())
        self.assertAlmostEqual(engine.published_value(2.5), before)
        self.assertNotAlmostEqual(engine.published_value(2.5), 2.5)

    def test_save_refuses_invalid_draft(self):
        engine = DraftEngine(SEED, draft_mode=True)
        live_before = list(engine.live_points)
        published = engine.calibrate(2.5)
        engine.set_point_count(1)
        self.assertFalse(engine.is_draft_valid())
        self.assertTrue(engine.is_draft_invalid())

        engine.save_button()

        self.assertEqual(engine.live_points, live_before)
        self.assertIsNone(engine.saved_points)
        self.assertTrue(engine.has_draft_changes)
        self.assertAlmostEqual(engine.calibrate(2.5), published)
        self.assertEqual(engine.get_point_count(), 1)


class SourceScanTest(unittest.TestCase):
    def setUp(self):
        with open(ENGINE_CPP, encoding="utf-8") as handle:
            self.engine = handle.read()
        with open(ENGINE_H, encoding="utf-8") as handle:
            self.header = handle.read()
        with open(UI_CPP, encoding="utf-8") as handle:
            self.ui = handle.read()
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            self.channel = handle.read()

    def _fn(self, source, signature):
        match = re.search(
            rf"{re.escape(signature)}\s*(?:\([^;]*?\))?\s*\{{(?P<body>.*?)^\}}",
            source,
            re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(match, f"{signature} not found")
        return match.group("body")

    def test_save_button_commits_when_draft_mode(self):
        body = self._fn(self.ui, "void CalibrationSaveButton::press_action()")
        self.assertIn("is_draft_mode()", body)
        self.assertIn("is_draft_valid()", body)
        self.assertIn("commit_draft()", body)
        self.assertIn("save_to_preferences()", body)
        self.assertLess(body.find("is_draft_valid()"), body.find("commit_draft()"))
        self.assertLess(body.find("is_draft_mode()"), body.find("commit_draft()"))
        self.assertIn("notify_calibration_updated()", body)

    def test_load_resyncs_draft_from_live(self):
        body = self._fn(self.engine, "void CalibrationEngine::load_from_preferences()")
        self.assertIn("draft_mode_enabled_", body)
        self.assertIn("sync_draft_from_live()", body)

    def test_calibrate_uses_live_type_and_points(self):
        cal = self._fn(self.engine, "float CalibrationEngine::calibrate(float raw_voltage) const")
        self.assertIn("this->live_type_", cal)
        self.assertNotIn("this->type_", cal)

        # v0.10.6: live points are read inside ensure_*_cache_ (hot-path caches)
        linear_cache = self._fn(self.engine, "void CalibrationEngine::ensure_linear_cache_() const")
        self.assertIn("this->live_points_", linear_cache)
        linear = self._fn(self.engine, "float CalibrationEngine::calibrate_linear_(float x) const")
        self.assertIn("ensure_linear_cache_", linear)
        poly = self.engine[
            self.engine.find("void CalibrationEngine::compute_polynomial_coefficients_") :
        ]
        poly = poly.split("void CalibrationEngine::ensure_piecewise_cache_")[0]
        self.assertIn("this->live_points_", poly)
        piece_cache = self._fn(self.engine, "void CalibrationEngine::ensure_piecewise_cache_() const")
        self.assertIn("this->live_points_", piece_cache)
        piece = self._fn(self.engine, "float CalibrationEngine::calibrate_piecewise_(float x) const")
        self.assertIn("ensure_piecewise_cache_", piece)

    def test_is_valid_uses_live_not_working(self):
        body = self._fn(self.engine, "bool CalibrationEngine::is_valid() const")
        self.assertIn("live_points_", body)
        self.assertIn("live_type_", body)
        draft = self._fn(self.engine, "bool CalibrationEngine::is_draft_valid() const")
        self.assertIn("this->points_()", draft)
        self.assertIn("this->type_", draft)

    def test_point_mutations_mark_draft_dirty(self):
        for name in ("add_point", "set_point", "remove_point", "clear_points"):
            body = self._fn(self.engine, f"void CalibrationEngine::{name}")
            self.assertIn("mark_draft_dirty_()", body, name)

    def test_runtime_algo_marks_draft_dirty(self):
        body = self._fn(self.engine, "void CalibrationEngine::set_type_runtime(CalibrationType type)")
        self.assertIn("mark_draft_dirty_()", body)
        self.assertIn("[DRAFT]", body)

    def test_commit_copies_params_and_saves(self):
        body = self._fn(self.engine, "void CalibrationEngine::commit_draft()")
        self.assertIn("is_draft_valid()", body)
        self.assertLess(body.find("is_draft_valid()"), body.find("this->live_points_ = this->draft_points_"))
        self.assertIn("this->live_points_ = this->draft_points_", body)
        self.assertIn("commit_working_params_to_live_()", body)
        self.assertIn("save_to_preferences()", body)
        self.assertIn("has_draft_changes_ = false", body)

    def test_publish_gate_uses_live_type(self):
        body = self._fn(self.channel, "void PoolStationChannelSensor::on_source_value_")
        self.assertIn("get_live_type()", body)
        self.assertLess(body.find("get_live_type()"), body.find("calibrate("))
        apply = body[body.find("Step 4") :]
        self.assertIn("get_live_type()", apply)
        self.assertIn("get_live_type() != CAL_TYPE_NONE", apply)
        self.assertIn("is_valid()", apply)
        self.assertNotIn("get_type()", apply)

    def test_commit_button_refuses_invalid_draft(self):
        body = self._fn(self.ui, "void CalibrationCommitButton::press_action()")
        self.assertIn("is_draft_valid()", body)
        self.assertIn("commit_draft()", body)
        self.assertLess(body.find("is_draft_valid()"), body.find("commit_draft()"))

    def test_draft_invalid_sensor_uses_draft_validity(self):
        body = self._fn(self.ui, "void DraftInvalidSensor::update_from_calibration()")
        self.assertIn("is_draft_mode()", body)
        self.assertIn("is_draft_valid()", body)
        self.assertIn("publish_state(invalid)", body)
        refresh = self._fn(self.channel, "void PoolStationComponent::refresh_calibration_ui")
        self.assertIn("draft_invalid_sensors_", refresh)

    def test_type_name_and_min_points_use_live(self):
        name = self._fn(self.engine, "const char *CalibrationEngine::get_type_name() const")
        self.assertIn("this->live_type_", name)
        self.assertNotIn("this->type_", name)
        mins = self._fn(self.engine, "uint8_t CalibrationEngine::get_minimum_points() const")
        self.assertIn("this->live_type_", mins)
        self.assertNotIn("this->type_", mins)

    def test_discard_resyncs_from_live(self):
        body = self._fn(self.engine, "void CalibrationEngine::discard_draft()")
        self.assertIn("sync_draft_from_live()", body)

    def test_xy_and_dfrobot_controls_notify(self):
        for name in (
            "CalibrationPointXNumber::control",
            "CalibrationPointYNumber::control",
            "DFRobotMidNumber::control",
            "DFRobotOffsetNumber::control",
        ):
            body = self._fn(self.ui, f"void {name}(float value)")
            self.assertIn("notify_calibration_updated()", body, name)

    def test_save_does_not_drop_legacy_path(self):
        body = self._fn(self.ui, "void CalibrationSaveButton::press_action()")
        self.assertIn("} else {", body)
        self.assertIn("save_to_preferences()", body)

    def test_docs_recommend_save_plus_discard(self):
        with open(README_PATH, encoding="utf-8") as handle:
            readme = handle.read()
        self.assertIn("draft_mode: true", readme)
        self.assertIn("discard_button", readme)
        self.assertIn("draft_pending", readme)
        self.assertIn("draft_invalid", readme)
        self.assertIn("Save", readme)

        with open(MIGRATION_PATH, encoding="utf-8") as handle:
            migration = handle.read()
        self.assertIn("0.7.17", migration)
        self.assertIn("0.7.18", migration)
        self.assertIn("draft_mode: true", migration)
        self.assertIn("discard_button", migration)
        self.assertIn("draft_pending", migration)
        self.assertIn("draft_invalid", migration)

        with open(CHANGELOG_PATH, encoding="utf-8") as handle:
            changelog = handle.read()
        self.assertIn("## [0.7.17]", changelog)
        self.assertIn("## [0.7.18]", changelog)
        self.assertIn("## [0.8.1]", changelog)

        with open(EXAMPLE_YAML, encoding="utf-8") as handle:
            example = handle.read()
        self.assertIn("Pool ORP Discard Changes", example)
        self.assertIn("Pool ORP Draft Pending", example)
        self.assertIn("Pool ORP Draft Invalid", example)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
