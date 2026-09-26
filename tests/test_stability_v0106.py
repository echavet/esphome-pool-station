#!/usr/bin/env python3
"""v0.10.6 stability pass: heap caches, publish gating, NVS quiet-period, YAML notes.

Source-scan tests (same style as test_nvs_deferred_flush.py).
Run: python3 -m unittest tests.test_stability_v0106 -v
"""

from __future__ import annotations

import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
ENGINE_CPP = os.path.join(ROOT, "components", "pool_station", "calibration_engine.cpp")
ENGINE_H = os.path.join(ROOT, "components", "pool_station", "calibration_engine.h")
FILTER_CPP = os.path.join(ROOT, "components", "pool_station", "channel_filter.cpp")
FILTER_H = os.path.join(ROOT, "components", "pool_station", "channel_filter.h")
DIAG_CPP = os.path.join(ROOT, "components", "pool_station", "channel_diagnostics.cpp")
CHANNEL_CPP = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
CHANNEL_H = os.path.join(ROOT, "components", "pool_station", "pool_station.h")
ADS_H = os.path.join(ROOT, "components", "pool_station", "ads_runtime.h")
ADS_PY = os.path.join(ROOT, "components", "pool_station", "ads_runtime.py")
EXAMPLE_YAML = os.path.join(ROOT, "examples", "pool-station-minimal.yaml")


def _load_helper():
    import importlib.util

    spec = importlib.util.spec_from_file_location("ads_runtime", ADS_PY)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _fn(source: str, signature: str) -> str:
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


class HeapCacheCalibrationTest(unittest.TestCase):
    def test_fit_cache_members_declared(self):
        with open(ENGINE_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("linear_cache_valid_", header)
        self.assertIn("linear_slope_", header)
        self.assertIn("piecewise_cache_valid_", header)
        self.assertIn("piecewise_pts_", header)
        self.assertIn("invalidate_fit_caches_", header)
        self.assertIn("ensure_linear_cache_", header)
        self.assertIn("ensure_piecewise_cache_", header)

    def test_calibrate_linear_uses_cache(self):
        with open(ENGINE_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "float CalibrationEngine::calibrate_linear_")
        self.assertIn("ensure_linear_cache_", body)
        self.assertNotIn("valid_points_copy_", body)

    def test_calibrate_piecewise_uses_cache(self):
        with open(ENGINE_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "float CalibrationEngine::calibrate_piecewise_")
        self.assertIn("ensure_piecewise_cache_", body)
        self.assertNotIn("valid_points_copy_", body)

    def test_point_mutations_invalidate_caches(self):
        with open(ENGINE_CPP, encoding="utf-8") as handle:
            source = handle.read()
        for name in ("add_point", "set_point", "remove_point", "clear_points", "set_seed_points"):
            body = _fn(source, f"void CalibrationEngine::{name}")
            self.assertIn("invalidate_fit_caches_", body, name)


class HeapFilterScratchTest(unittest.TestCase):
    def test_sliding_window_has_scratch(self):
        with open(FILTER_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("scratch_", header)

    def test_median_reuses_scratch(self):
        with open(FILTER_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "float SlidingWindow::median() const")
        self.assertIn("this->scratch_", body)
        self.assertNotIn("std::vector<float> valid", body)

    def test_mean_is_allocation_free(self):
        with open(FILTER_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "float SlidingWindow::mean() const")
        self.assertNotIn("std::vector<float>", body)

    def test_compute_median_takes_reference(self):
        with open(FILTER_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("compute_median(std::vector<float> &values)", header)
        self.assertNotIn("compute_median(std::vector<float> values)", header)


class HeapDiagnosticsTest(unittest.TestCase):
    def test_compute_stats_no_temp_vector(self):
        with open(DIAG_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "DiagnosticsStats DiagnosticsWindow::compute_stats() const")
        self.assertNotIn("std::vector<float> valid", body)
        self.assertIn("m2", body)  # Welford


class PublishGatingTest(unittest.TestCase):
    def test_fsr_constants(self):
        with open(ADS_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("FSR_PUBLISH_DELTA_PERCENT = 0.5f", header)
        self.assertIn("FSR_PUBLISH_MAX_INTERVAL_MS = 5000", header)
        self.assertEqual(ads.FSR_PUBLISH_DELTA_PERCENT, 0.5)
        self.assertEqual(ads.FSR_PUBLISH_MAX_INTERVAL_MS, 5000)

    def test_fsr_gated_helper_exists(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::publish_fsr_percent_gated_")
        self.assertIn("FSR_PUBLISH_DELTA_PERCENT", body)
        self.assertIn("FSR_PUBLISH_MAX_INTERVAL_MS", body)
        self.assertIn("last_published_fsr_percent_", body)

    def test_update_saturation_uses_gated_publish(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::update_saturation_")
        self.assertIn("publish_fsr_percent_gated_", body)
        self.assertIn("publish_binary_if_changed_", body)
        # Must not unconditionally publish fsr every sample
        self.assertNotIn("fsr_percent_sensor_->publish_state(this->get_fsr_percent", body)

    def test_diag_flags_transition_only(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        # on_source_value_ should use helper for diag flags
        self.assertIn("publish_binary_if_changed_(this->diag_noisy_flag_", source)
        self.assertIn("publish_binary_if_changed_(this->diag_stuck_flag_", source)
        self.assertIn("publish_binary_if_changed_(this->diag_out_of_range_flag_", source)

    def test_interference_transition_only(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationComponent::publish_interference_")
        self.assertIn("has_state()", body)
        # Direct unconditional publish_state(flags.*) should be gone
        self.assertNotIn("interference_suspected_sensor_->publish_state(flags.suspected)", body)


class YamlProdHardeningTest(unittest.TestCase):
    def test_example_mentions_interval_alignment(self):
        with open(EXAMPLE_YAML, encoding="utf-8") as handle:
            yaml = handle.read()
        self.assertIn("Align ADS", yaml)
        self.assertIn("continuous_mode", yaml)
        self.assertIn("i2c.timeout", yaml)
        self.assertIn("Free Heap", yaml)
        self.assertIn("Loop Time", yaml)


class ChangelogStabilityTest(unittest.TestCase):
    def test_changelog_has_0104_0105_0106(self):
        with open(os.path.join(ROOT, "CHANGELOG.md"), encoding="utf-8") as handle:
            cl = handle.read()
        self.assertIn("## [0.10.6]", cl)
        self.assertIn("## [0.10.5]", cl)
        self.assertIn("## [0.10.4]", cl)
        self.assertIn("quiet-period", cl.lower())


if __name__ == "__main__":
    raise SystemExit(unittest.main())
