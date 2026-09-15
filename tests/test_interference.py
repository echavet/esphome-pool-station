#!/usr/bin/env python3
"""Lot 8 interference math — Python mirror of interference_math.h.

Run: python3 tests/test_interference.py
"""

from __future__ import annotations

import math
import os
import re
import unittest
from pathlib import Path

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
MATH_H = os.path.join(ROOT, "components", "pool_station", "interference_math.h")
DET_H = os.path.join(ROOT, "components", "pool_station", "interference_detector.h")
DET_CPP = os.path.join(ROOT, "components", "pool_station", "interference_detector.cpp")
CDC = os.path.join(ROOT, "docs", "POOL-STATION-CDC.md")
README = os.path.join(ROOT, "README.md")
CHANGELOG = os.path.join(ROOT, "CHANGELOG.md")
INIT_PY = os.path.join(ROOT, "components", "pool_station", "__init__.py")
STATION_CPP = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
STATION_H = os.path.join(ROOT, "components", "pool_station", "pool_station.h")
EXAMPLE = os.path.join(ROOT, "examples", "pool-station-minimal.yaml")


def is_jump(previous, current, threshold):
    if math.isnan(previous) or math.isnan(current) or not (threshold > 0):
        return False
    return abs(current - previous) >= threshold


def _u32(value):
    return value & 0xFFFFFFFF


def time_delta_ms(a, b):
    a, b = _u32(a), _u32(b)
    d = _u32(a - b)
    if d > 0x7FFFFFFF:
        d = _u32(b - a)
    return d


def later_ms(a, b):
    a, b = _u32(a), _u32(b)
    d = _u32(a - b)
    return a if d <= 0x7FFFFFFF else b


def is_not_later(a, b):
    a, b = _u32(a), _u32(b)
    d = _u32(a - b)
    return d > 0x7FFFFFFF or d == 0


def coincident_in_window(t_a, t_b, window_ms):
    return time_delta_ms(t_a, t_b) <= window_ms


def coincident_jump(jump_a, t_a, jump_b, t_b, now_ms, window_ms):
    if not jump_a or not jump_b:
        return False
    if not coincident_in_window(t_a, t_b, window_ms):
        return False
    return coincident_in_window(now_ms, later_ms(t_a, t_b), window_ms)


def shared_noise(sigma_a, sigma_b, thresh_a, thresh_b):
    if math.isnan(sigma_a) or math.isnan(sigma_b):
        return False
    if not (thresh_a > 0) or not (thresh_b > 0):
        return False
    return sigma_a >= thresh_a and sigma_b >= thresh_b


def tw_coupling(delta_ph, delta_tw, ph_threshold, tw_threshold):
    if math.isnan(delta_ph) or math.isnan(delta_tw):
        return False
    if not (ph_threshold > 0) or not (tw_threshold > 0):
        return False
    if abs(delta_ph) < ph_threshold or abs(delta_tw) < tw_threshold:
        return False
    return (delta_ph > 0) == (delta_tw > 0)


def stddev(values):
    valid = [v for v in values if not math.isnan(v)]
    if len(valid) < 2:
        return float("nan")
    mean = sum(valid) / len(valid)
    acc = sum((v - mean) ** 2 for v in valid)
    return math.sqrt(acc / len(valid))


class Track:
    def __init__(self):
        self.prev = float("nan")
        self.value = float("nan")
        self.t = 0
        self.window = []
        self.window_t = []
        self.has_sample = False
        self.has_jump = False
        self.last_jump_ms = 0

    def push(self, value, ts):
        self.prev = self.value if self.has_sample else float("nan")
        self.value = value
        self.t = ts
        self.has_sample = True
        self.window.append(value)
        self.window_t.append(ts)
        if len(self.window) > 32:
            self.window.pop(0)
            self.window_t.pop(0)

    def sigma(self):
        return stddev(self.window)

    def delta_over(self, now_ms, window_ms):
        if not self.has_sample or math.isnan(self.value):
            return float("nan")
        oldest_t = None
        oldest_v = float("nan")
        for v, t in zip(self.window, self.window_t):
            if time_delta_ms(now_ms, t) > window_ms:
                continue
            if oldest_t is None or is_not_later(t, oldest_t):
                oldest_t = t
                oldest_v = v
        if oldest_t is None or math.isnan(oldest_v):
            return float("nan")
        return self.value - oldest_v


class DetectorSim:
    """Mirrors InterferenceDetector::evaluate (last-jump latch + hold + cal skip)."""

    def __init__(self):
        self.orp = Track()
        self.pressure = Track()
        self.ph = Track()
        self.tw = Track()
        self.orp_jump = 40.0
        self.pressure_jump = 0.15
        self.ph_sigma = 0.08
        self.orp_sigma = 10.0
        self.ph_jump = 0.12
        self.tw_jump = 1.0
        self.jump_window_ms = 2000
        self.tw_window_ms = 30000
        self.hold_ms = 30000
        self.last_hit = 0
        self.hold_active = False
        self.flags = {"coincident_jump": False, "shared_noise": False,
                      "tw_coupling": False, "suspected": False}

    def reset(self):
        self.orp = Track()
        self.pressure = Track()
        self.ph = Track()
        self.tw = Track()
        self.last_hit = 0
        self.hold_active = False
        self.flags = {k: False for k in self.flags}

    def evaluate(self, now_ms, calibration_mode=False):
        if calibration_mode:
            self.flags = {k: False for k in self.flags}
            self.hold_active = False
            return self.flags

        if is_jump(self.orp.prev, self.orp.value, self.orp_jump):
            self.orp.has_jump = True
            self.orp.last_jump_ms = self.orp.t
        if is_jump(self.pressure.prev, self.pressure.value, self.pressure_jump):
            self.pressure.has_jump = True
            self.pressure.last_jump_ms = self.pressure.t

        live_cj = coincident_jump(
            self.orp.has_jump, self.orp.last_jump_ms,
            self.pressure.has_jump, self.pressure.last_jump_ms,
            now_ms, self.jump_window_ms,
        )
        live_sn = shared_noise(self.ph.sigma(), self.orp.sigma(), self.ph_sigma, self.orp_sigma)
        live_tw = tw_coupling(
            self.ph.delta_over(now_ms, self.tw_window_ms),
            self.tw.delta_over(now_ms, self.tw_window_ms),
            self.ph_jump, self.tw_jump,
        )
        live_any = live_cj or live_sn or live_tw
        if live_any:
            self.last_hit = now_ms
            self.hold_active = True
            self.flags = {
                "coincident_jump": live_cj,
                "shared_noise": live_sn,
                "tw_coupling": live_tw,
                "suspected": True,
            }
        elif self.hold_active:
            if time_delta_ms(now_ms, self.last_hit) >= self.hold_ms:
                self.hold_active = False
                self.flags = {k: False for k in self.flags}
            else:
                self.flags["suspected"] = True
        else:
            self.flags = {k: False for k in self.flags}
        return self.flags


class TestIsJump(unittest.TestCase):
    def test_jump_detected(self):
        self.assertTrue(is_jump(700.0, 760.0, 40.0))

    def test_below_threshold(self):
        self.assertFalse(is_jump(700.0, 720.0, 40.0))

    def test_nan_or_disabled(self):
        self.assertFalse(is_jump(float("nan"), 760.0, 40.0))
        self.assertFalse(is_jump(700.0, 760.0, 0.0))
        self.assertFalse(is_jump(700.0, 760.0, -1.0))


class TestCoincidentJump(unittest.TestCase):
    def test_pump_orp_spike(self):
        self.assertTrue(coincident_jump(True, 1000, True, 1500, 1500, 2000))

    def test_delayed_second_channel_still_pairs(self):
        # Pressure jumped at t=1000, ORP at t=2500, window 2000 ms.
        self.assertTrue(coincident_jump(True, 1000, True, 2500, 2500, 2000))

    def test_stale_pair_does_not_latch(self):
        self.assertFalse(coincident_jump(True, 1000, True, 1500, 10000, 2000))

    def test_orp_alone_is_not_interference(self):
        self.assertFalse(coincident_jump(True, 1000, False, 1000, 1000, 2000))

    def test_outside_window(self):
        self.assertFalse(coincident_jump(True, 1000, True, 5000, 5000, 2000))


class TestSharedNoise(unittest.TestCase):
    def test_both_noisy(self):
        self.assertTrue(shared_noise(0.12, 15.0, 0.08, 10.0))

    def test_only_ph_noisy(self):
        self.assertFalse(shared_noise(0.12, 2.0, 0.08, 10.0))

    def test_nan(self):
        self.assertFalse(shared_noise(float("nan"), 15.0, 0.08, 10.0))


class TestTwCoupling(unittest.TestCase):
    def test_same_sign(self):
        self.assertTrue(tw_coupling(0.20, 1.5, 0.12, 1.0))

    def test_opposite_sign_not_coupling(self):
        self.assertFalse(tw_coupling(0.20, -1.5, 0.12, 1.0))

    def test_small_tw_ignored(self):
        self.assertFalse(tw_coupling(0.20, 0.2, 0.12, 1.0))


class TestStddev(unittest.TestCase):
    def test_known_values(self):
        self.assertAlmostEqual(stddev([2, 4, 4, 4, 5, 5, 7, 9]), 2.0, places=5)

    def test_too_few(self):
        self.assertTrue(math.isnan(stddev([1.0])))


class TestDetectorSim(unittest.TestCase):
    def test_pump_then_orp_within_window(self):
        d = DetectorSim()
        d.pressure.push(1.50, 1000)
        d.evaluate(1000)
        d.pressure.push(1.80, 1000)
        flags = d.evaluate(1000)
        self.assertFalse(flags["suspected"])
        d.orp.push(700.0, 2500)
        d.evaluate(2500)
        d.orp.push(760.0, 2500)
        flags = d.evaluate(2500)
        self.assertTrue(flags["coincident_jump"])
        self.assertTrue(flags["suspected"])

    def test_hold_then_expire(self):
        d = DetectorSim()
        d.pressure.push(1.5, 0)
        d.pressure.push(1.8, 1000)
        d.orp.push(700.0, 0)
        d.orp.push(760.0, 1000)
        flags = d.evaluate(1000)
        self.assertTrue(flags["suspected"])
        flags = d.evaluate(20000)
        self.assertTrue(flags["suspected"])
        flags = d.evaluate(40000)
        self.assertFalse(flags["suspected"])

    def test_calibration_mode_clears(self):
        d = DetectorSim()
        d.pressure.push(1.5, 0)
        d.pressure.push(1.8, 1000)
        d.orp.push(700.0, 0)
        d.orp.push(760.0, 1000)
        self.assertTrue(d.evaluate(1000)["suspected"])
        self.assertFalse(d.evaluate(1100, calibration_mode=True)["suspected"])

    def test_shared_noise_both_channels(self):
        d = DetectorSim()
        for i, v in enumerate([7.0, 7.2, 6.8, 7.3, 6.7, 7.4, 6.6, 7.5, 6.5, 7.6]):
            d.ph.push(v, i * 1000)
        for i, v in enumerate([700, 720, 680, 730, 670, 740, 660, 750, 650, 760]):
            d.orp.push(v, i * 1000)
        flags = d.evaluate(9000)
        self.assertTrue(flags["shared_noise"])

    def test_tw_coupling_over_window(self):
        d = DetectorSim()
        d.ph.push(7.20, 0)
        d.tw.push(24.0, 0)
        d.ph.push(7.40, 20000)
        d.tw.push(25.5, 20000)
        flags = d.evaluate(20000)
        self.assertTrue(flags["tw_coupling"])

    def test_pressure_2s_orp_60s_pairs_with_90s_window(self):
        d = DetectorSim()
        d.jump_window_ms = 90000
        d.pressure.push(1.50, 0)
        d.pressure.push(1.80, 2000)
        d.orp.push(700.0, 0)
        d.evaluate(2000)
        d.orp.push(760.0, 60000)
        flags = d.evaluate(60000)
        self.assertTrue(flags["coincident_jump"])

    def test_pressure_2s_orp_60s_misses_with_2s_window(self):
        d = DetectorSim()
        d.jump_window_ms = 2000
        d.pressure.push(1.50, 0)
        d.pressure.push(1.80, 2000)
        d.orp.push(700.0, 0)
        d.evaluate(2000)
        d.orp.push(760.0, 60000)
        flags = d.evaluate(60000)
        self.assertFalse(flags["coincident_jump"])

    def test_tw_window_shorter_than_ph_interval_no_coupling(self):
        d = DetectorSim()
        d.tw_window_ms = 30000
        d.ph.push(7.20, 0)
        d.tw.push(24.0, 0)
        d.ph.push(7.40, 60000)
        d.tw.push(25.5, 60000)
        flags = d.evaluate(60000)
        self.assertFalse(flags["tw_coupling"])

    def test_tw_window_covers_two_ph_samples(self):
        d = DetectorSim()
        d.tw_window_ms = 180000
        d.ph.push(7.20, 0)
        d.tw.push(24.0, 0)
        d.ph.push(7.40, 60000)
        d.tw.push(25.5, 60000)
        flags = d.evaluate(60000)
        self.assertTrue(flags["tw_coupling"])

    def test_calibrator_tour_without_reset_trips_shared_noise(self):
        d = DetectorSim()
        for v, t in ((4.0, 0), (7.0, 10000), (10.0, 20000)):
            d.ph.push(v, t)
        for v, t in ((200.0, 0), (650.0, 10000), (700.0, 20000)):
            d.orp.push(v, t)
        self.assertTrue(d.evaluate(20000)["shared_noise"])

    def test_reset_after_calibrator_tour_clears_shared_noise(self):
        d = DetectorSim()
        for v, t in ((4.0, 0), (7.0, 10000), (10.0, 20000)):
            d.ph.push(v, t)
        for v, t in ((200.0, 0), (650.0, 10000), (700.0, 20000)):
            d.orp.push(v, t)
        d.reset()
        self.assertFalse(d.evaluate(21000)["shared_noise"])
        self.assertTrue(math.isnan(d.ph.sigma()))


class TestMillisWrap(unittest.TestCase):
    def test_delta_across_wrap_is_small(self):
        self.assertEqual(time_delta_ms(0x100, 0xFFFFFF00), 0x200)
        self.assertTrue(coincident_in_window(0x100, 0xFFFFFF00, 2000))

    def test_later_ms_picks_post_wrap_timestamp(self):
        self.assertEqual(later_ms(0x100, 0xFFFFFF00), 0x100)

    def test_naive_compare_would_fail(self):
        a, b = 0x100, 0xFFFFFF00
        self.assertFalse(a >= b)
        self.assertEqual(later_ms(a, b), a)


class TestCppContract(unittest.TestCase):
    def setUp(self):
        self.math = Path(MATH_H).read_text(encoding="utf-8")
        self.det_h = Path(DET_H).read_text(encoding="utf-8")
        self.det_cpp = Path(DET_CPP).read_text(encoding="utf-8")
        self.init = Path(INIT_PY).read_text(encoding="utf-8")
        self.station = Path(STATION_CPP).read_text(encoding="utf-8")
        self.station_h = Path(STATION_H).read_text(encoding="utf-8")
        self.cdc = Path(CDC).read_text(encoding="utf-8")
        self.readme = Path(README).read_text(encoding="utf-8")
        self.changelog = Path(CHANGELOG).read_text(encoding="utf-8")
        self.example = Path(EXAMPLE).read_text(encoding="utf-8")

    def test_math_header_has_four_predicates(self):
        for name in ("is_jump", "coincident_jump", "shared_noise", "tw_coupling"):
            self.assertIn(f"inline bool {name}(", self.math)

    def test_detector_skips_in_calibration_mode(self):
        self.assertIn("if (calibration_mode)", self.det_cpp)
        self.assertIn("this->hold_active_ = false", self.det_cpp)

    def test_parent_skips_push_and_resets_on_calibration_toggle(self):
        self.assertIn("if (this->calibration_mode_active_)\n    return;", self.station)
        self.assertIn("this->interference_.reset()", self.station)
        self.assertIn("!this->calibration_mode_active_", self.station)

    def test_samples_are_pre_jump_guard(self):
        self.assertIn("on_channel_sample(static_cast<uint8_t>(this->channel_type_), compensated, now)",
                      self.station)
        self.assertNotIn("on_channel_sample(static_cast<uint8_t>(this->channel_type_), guarded, now)",
                         self.station)

    def test_defaults_cover_slow_orp_and_ph(self):
        self.assertIn('default="90s"', self.init)
        self.assertIn('default="180s"', self.init)
        self.assertIn("jump_window_ms{90000}", self.det_h)
        self.assertIn("tw_window_ms{180000}", self.det_h)
        self.assertIn("jump_window: 90s", self.example)

    def test_time_delta_is_modular(self):
        self.assertIn("if (d > 0x7FFFFFFFu)", self.math)
        self.assertIn("inline uint32_t later_ms(", self.math)

    def test_last_jump_latch(self):
        self.assertIn("last_jump_ms", self.det_h)
        self.assertIn("last_jump_ms", self.det_cpp)

    def test_channel_notifies_parent(self):
        self.assertIn("on_channel_sample", self.station)
        self.assertIn("on_channel_sample", self.station_h)

    def test_yaml_key_present(self):
        self.assertIn('CONF_INTERFERENCE = "interference"', self.init)
        self.assertIn("async def setup_interference", self.init)

    def test_lot8_docs_exclude_ezo(self):
        lot8 = re.search(r"## 7\. Roadmap.*?(?=## 8\.)", self.cdc, re.S)
        self.assertIsNotNone(lot8)
        row = [line for line in lot8.group(0).splitlines() if re.search(r"\|\s*\**8\**\s*\|", line)]
        self.assertTrue(row, msg="CDC roadmap missing Lot 8 row")
        self.assertNotIn("EZO", row[0])
        self.assertIn("Interference", row[0])

    def test_changelog_0_8_0(self):
        self.assertIn("## [0.8.0]", self.changelog)
        section = self.changelog.split("## [0.8.0]")[1].split("## [0.7.18]")[0]
        self.assertIn("interference", section.lower())
        self.assertNotIn("EZO support", section)

    def test_readme_lot8_done(self):
        self.assertIn("Interference Detection", self.readme)
        matrix = [line for line in self.readme.splitlines() if "Interference Detection" in line]
        self.assertTrue(matrix)
        self.assertIn("Done", matrix[0])


if __name__ == "__main__":
    unittest.main()
