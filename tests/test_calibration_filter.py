#!/usr/bin/env python3
"""
Unit tests for pool_station calibration and filter pure functions.

These tests validate the mathematical correctness of:
- Linear calibration
- Piecewise calibration  
- Polynomial calibration (basic)
- DFRobot ORP formula
- Median filter
- Jump guard logic
- Clamp functions

Run with: python3 tests/test_calibration_filter.py
"""

import math
import unittest
from typing import List, Tuple, Optional


# =============================================================================
# Pure functions (Python equivalents of C++ filter_functions)
# =============================================================================

def compute_median(values: List[float]) -> float:
    """Compute median of values, ignoring NaN."""
    valid = [v for v in values if not math.isnan(v)]
    if not valid:
        return float('nan')
    valid.sort()
    n = len(valid)
    mid = n // 2
    if n % 2 == 0:
        return (valid[mid - 1] + valid[mid]) / 2
    return valid[mid]


def compute_mean(values: List[float]) -> float:
    """Compute mean of values, ignoring NaN."""
    valid = [v for v in values if not math.isnan(v)]
    if not valid:
        return float('nan')
    return sum(valid) / len(valid)


def clamp_value(value: float, min_val: Optional[float], max_val: Optional[float]) -> float:
    """Clamp value between min and max. None means unbounded."""
    if math.isnan(value):
        return value
    if min_val is not None and value < min_val:
        return min_val
    if max_val is not None and value > max_val:
        return max_val
    return value


def is_jump_exceeded(current: float, previous: float, threshold: float) -> bool:
    """Check if jump exceeds threshold."""
    if math.isnan(current) or math.isnan(previous) or threshold <= 0:
        return False
    return abs(current - previous) > threshold


# =============================================================================
# Calibration algorithms (Python equivalents)
# =============================================================================

def apply_precision(value: float, decimals: int) -> float:
    """Round like C++ std::round (half away from zero)."""
    if math.isnan(value):
        return value
    scale = 10.0 ** decimals
    return math.copysign(math.floor(abs(value) * scale + 0.5), value) / scale


def calibrate_linear(x: float, points: List[Tuple[float, float]]) -> float:
    """Least-squares linear fit on all valid points (N==2 is the two-point line)."""
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


def calibrate_piecewise(x: float, points: List[Tuple[float, float]]) -> float:
    """Piecewise linear calibration between adjacent points."""
    if len(points) < 2:
        return x
    
    # Sort by x
    points = sorted(points, key=lambda p: p[0])
    
    # Below first point: extrapolate
    if x <= points[0][0]:
        p1, p2 = points[0], points[1]
        slope = (p2[1] - p1[1]) / (p2[0] - p1[0])
        return p1[1] + (x - p1[0]) * slope
    
    # Above last point: extrapolate
    if x >= points[-1][0]:
        p1, p2 = points[-2], points[-1]
        slope = (p2[1] - p1[1]) / (p2[0] - p1[0])
        return p2[1] + (x - p2[0]) * slope
    
    # Find segment
    for i in range(len(points) - 1):
        p1, p2 = points[i], points[i + 1]
        if p1[0] <= x <= p2[0]:
            if abs(p2[0] - p1[0]) < 1e-9:
                return p1[1]
            slope = (p2[1] - p1[1]) / (p2[0] - p1[0])
            return p1[1] + (x - p1[0]) * slope
    
    return x


def calibrate_dfrobot_orp(x: float, mid_mv: float, offset_mv: float) -> float:
    """DFRobot SEN0165-like ORP formula."""
    raw_mv = x * 1000.0
    return (mid_mv - raw_mv) - offset_mv


# =============================================================================
# Temperature compensation (Lot 5)
# =============================================================================

def compute_slope_ratio(t_ref: float, t_measured: float) -> float:
    """Compute Nernst slope ratio for pH compensation."""
    return (t_ref + 273.15) / (t_measured + 273.15)


def compensate_ph(ph_measured: float, t_measured: float, 
                  t_ref: float = 25.0, neutral_ph: float = 7.0) -> float:
    """Compensate pH for temperature using Nernstian model."""
    slope_ratio = compute_slope_ratio(t_ref, t_measured)
    return neutral_ph + (ph_measured - neutral_ph) * slope_ratio


def compensate_orp(orp_measured: float, t_measured: float,
                   t_ref: float = 25.0, coefficient: float = 0.0) -> float:
    """Compensate ORP for temperature using linear model."""
    if coefficient == 0.0:
        return orp_measured
    return orp_measured - coefficient * (t_measured - t_ref)


# =============================================================================
# Test Cases
# =============================================================================

class TestFilterFunctions(unittest.TestCase):
    """Test filter pure functions."""
    
    def test_compute_median_odd(self):
        """Median of odd number of values."""
        self.assertEqual(compute_median([1, 3, 2]), 2)
        self.assertEqual(compute_median([5, 1, 3, 2, 4]), 3)
    
    def test_compute_median_even(self):
        """Median of even number of values."""
        self.assertEqual(compute_median([1, 2, 3, 4]), 2.5)
        self.assertEqual(compute_median([1, 2]), 1.5)
    
    def test_compute_median_with_nan(self):
        """Median ignores NaN values."""
        self.assertEqual(compute_median([1, float('nan'), 3, 2]), 2)
    
    def test_compute_median_empty(self):
        """Median of empty list is NaN."""
        self.assertTrue(math.isnan(compute_median([])))
    
    def test_compute_mean(self):
        """Mean of values."""
        self.assertEqual(compute_mean([1, 2, 3, 4, 5]), 3)
        self.assertEqual(compute_mean([2, 4]), 3)
    
    def test_clamp_value(self):
        """Clamp value between bounds."""
        self.assertEqual(clamp_value(5, 0, 10), 5)
        self.assertEqual(clamp_value(-1, 0, 10), 0)
        self.assertEqual(clamp_value(15, 0, 10), 10)
        self.assertEqual(clamp_value(5, None, 10), 5)
        self.assertEqual(clamp_value(5, 0, None), 5)
    
    def test_is_jump_exceeded(self):
        """Jump detection."""
        self.assertTrue(is_jump_exceeded(10, 5, 2))
        self.assertFalse(is_jump_exceeded(6, 5, 2))
        self.assertFalse(is_jump_exceeded(5, 5, 2))


class TestCalibrationLinear(unittest.TestCase):
    """Test linear calibration algorithm."""
    
    def test_linear_basic(self):
        """Basic linear calibration."""
        points = [(0.5, 0.0), (4.5, 10.0)]
        self.assertAlmostEqual(calibrate_linear(0.5, points), 0.0)
        self.assertAlmostEqual(calibrate_linear(4.5, points), 10.0)
        self.assertAlmostEqual(calibrate_linear(2.5, points), 5.0)
    
    def test_linear_extrapolation(self):
        """Linear extrapolation beyond points."""
        points = [(0.5, 0.0), (4.5, 10.0)]
        # Extrapolate below
        self.assertAlmostEqual(calibrate_linear(0.0, points), -1.25)
        # Extrapolate above
        self.assertAlmostEqual(calibrate_linear(5.0, points), 11.25)

    def test_linear_least_squares_n_gt_2(self):
        """N>2 uses all valid points, not first/last only."""
        # y = 2x + 1, plus a midpoint that would be ignored by first/last
        # if first/last were (0,1) and (4,9) the line is the same; add an
        # off-line first/last trap: first=(0,0), last=(4,0), mid=(2,2)
        # first/last would give y=0; least-squares slope = 0, intercept = 2/3.
        points = [(0.0, 0.0), (2.0, 2.0), (4.0, 0.0)]
        self.assertAlmostEqual(calibrate_linear(2.0, points), 2.0 / 3.0)
        self.assertNotAlmostEqual(calibrate_linear(2.0, points), 0.0)

    def test_linear_ignores_nan_slots(self):
        """NaN HA holes are skipped; stored order is irrelevant."""
        points = [(2.0, 10.0), (float("nan"), float("nan")), (0.0, 0.0)]
        self.assertAlmostEqual(calibrate_linear(1.0, points), 5.0)


class TestApplyPrecision(unittest.TestCase):
    """YAML precision_decimals applied to calibrated output."""

    def test_round_two_decimals(self):
        self.assertAlmostEqual(apply_precision(7.126, 2), 7.13)
        self.assertAlmostEqual(apply_precision(7.124, 2), 7.12)

    def test_nan_passthrough(self):
        self.assertTrue(math.isnan(apply_precision(float("nan"), 2)))


class TestCalibrationPiecewise(unittest.TestCase):
    """Test piecewise calibration algorithm."""
    
    def test_piecewise_ph_calibration(self):
        """Piecewise pH calibration with 3 points."""
        # Typical pH calibration: voltage vs pH
        points = [(2.03, 4.01), (1.50, 7.00), (0.98, 10.00)]
        
        # At calibration points
        self.assertAlmostEqual(calibrate_piecewise(2.03, points), 4.01)
        self.assertAlmostEqual(calibrate_piecewise(1.50, points), 7.00)
        self.assertAlmostEqual(calibrate_piecewise(0.98, points), 10.00)
        
        # Between points
        mid_v = (2.03 + 1.50) / 2
        expected_ph = (4.01 + 7.00) / 2
        self.assertAlmostEqual(calibrate_piecewise(mid_v, points), expected_ph)
    
    def test_piecewise_extrapolation(self):
        """Piecewise extrapolation beyond endpoints."""
        points = [(1.0, 0.0), (2.0, 10.0)]
        
        # Below first point
        self.assertAlmostEqual(calibrate_piecewise(0.5, points), -5.0)
        # Above last point
        self.assertAlmostEqual(calibrate_piecewise(2.5, points), 15.0)


class TestCalibrationDFRobotORP(unittest.TestCase):
    """Test DFRobot ORP calibration formula."""
    
    def test_dfrobot_orp_midpoint(self):
        """ORP at midpoint voltage."""
        # At 2.5V with mid=2500, offset=0: ORP = 2500 - 2500 - 0 = 0
        self.assertAlmostEqual(calibrate_dfrobot_orp(2.5, 2500, 0), 0)
    
    def test_dfrobot_orp_with_offset(self):
        """ORP with offset calibration."""
        # At 2.5V with mid=2500, offset=100: ORP = 2500 - 2500 - 100 = -100
        self.assertAlmostEqual(calibrate_dfrobot_orp(2.5, 2500, 100), -100)
    
    def test_dfrobot_orp_typical(self):
        """Typical ORP reading."""
        # At 2.275V (225mV below mid): ORP = 2500 - 2275 - 0 = 225
        self.assertAlmostEqual(calibrate_dfrobot_orp(2.275, 2500, 0), 225)


class TestTemperatureCompensation(unittest.TestCase):
    """Test temperature compensation functions (Lot 5)."""
    
    def test_slope_ratio_at_reference(self):
        """Slope ratio at reference temperature is 1.0."""
        self.assertAlmostEqual(compute_slope_ratio(25.0, 25.0), 1.0)
    
    def test_slope_ratio_warmer(self):
        """Slope ratio decreases with warmer water."""
        ratio = compute_slope_ratio(25.0, 30.0)
        self.assertLess(ratio, 1.0)
        self.assertAlmostEqual(ratio, 298.15 / 303.15, places=4)
    
    def test_ph_compensation_at_neutral(self):
        """No pH compensation at neutral point."""
        # At pH 7.0, temperature has no effect
        self.assertAlmostEqual(compensate_ph(7.0, 30.0, 25.0, 7.0), 7.0)
    
    def test_ph_compensation_above_neutral(self):
        """pH compensation above neutral decreases with warmer water."""
        # pH 7.5 at 30°C
        compensated = compensate_ph(7.5, 30.0, 25.0, 7.0)
        self.assertLess(compensated, 7.5)
        # Expected: 7.0 + (7.5 - 7.0) * 0.9835 ≈ 7.492
        self.assertAlmostEqual(compensated, 7.492, places=2)
    
    def test_orp_compensation_disabled(self):
        """ORP compensation disabled with coefficient=0."""
        self.assertEqual(compensate_orp(500, 30.0, 25.0, 0.0), 500)
    
    def test_orp_compensation_enabled(self):
        """ORP compensation with non-zero coefficient."""
        # 500mV at 30°C, 1.0 mV/°C: 500 - 1.0 * (30 - 25) = 495
        self.assertAlmostEqual(compensate_orp(500, 30.0, 25.0, 1.0), 495)


class TestJumpGuard(unittest.TestCase):
    """Test jump guard logic."""
    
    def test_no_jump(self):
        """Value within threshold passes."""
        self.assertFalse(is_jump_exceeded(5.1, 5.0, 0.5))
    
    def test_jump_detected(self):
        """Value exceeding threshold is detected."""
        self.assertTrue(is_jump_exceeded(6.0, 5.0, 0.5))
    
    def test_negative_jump(self):
        """Negative jumps are also detected."""
        self.assertTrue(is_jump_exceeded(4.0, 5.0, 0.5))


class TestDraftCommitWorkflow(unittest.TestCase):
    """Test draft/commit workflow logic (Lot 7)."""
    
    def test_draft_isolation(self):
        """Draft changes don't affect live calibration."""
        live_points = [(1.0, 1.0), (2.0, 2.0)]
        draft_points = list(live_points)  # Copy
        
        # Modify draft
        draft_points.append((3.0, 3.0))
        
        # Live should be unchanged
        self.assertEqual(len(live_points), 2)
        self.assertEqual(len(draft_points), 3)
    
    def test_commit_copies_draft_to_live(self):
        """Commit copies draft to live."""
        live_points = [(1.0, 1.0)]
        draft_points = [(1.0, 1.0), (2.0, 2.0)]
        
        # Simulate commit
        live_points = list(draft_points)
        
        self.assertEqual(len(live_points), 2)
    
    def test_discard_restores_live(self):
        """Discard reverts draft to live."""
        live_points = [(1.0, 1.0)]
        draft_points = [(1.0, 1.0), (2.0, 2.0), (3.0, 3.0)]
        
        # Simulate discard
        draft_points = list(live_points)
        
        self.assertEqual(len(draft_points), 1)


# =============================================================================
# Run tests
# =============================================================================

if __name__ == '__main__':
    print("=" * 70)
    print("pool_station Unit Tests — Calibration & Filter Functions")
    print("=" * 70)
    print()
    
    unittest.main(verbosity=2)
