#!/usr/bin/env python3
"""NVS deferred flush (v0.10.4) unit tests.

Tests that the dirty flag + debounce mechanism is correctly implemented
to prevent Wi-Fi starvation from rapid NVS writes.

Run with: python3 tests/test_nvs_deferred_flush.py
"""

import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HELPER_PATH = os.path.join(ROOT, "components", "pool_station", "ads_runtime.py")
ADS_H = os.path.join(ROOT, "components", "pool_station", "ads_runtime.h")
CHANNEL_CPP = os.path.join(ROOT, "components", "pool_station", "pool_station.cpp")
CHANNEL_H = os.path.join(ROOT, "components", "pool_station", "pool_station.h")


def _load_helper():
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "ads_runtime", os.path.abspath(HELPER_PATH)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _fn(source, signature):
    """Extract a function body from C++ source."""
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


class NvsDebounceConstantsTest(unittest.TestCase):
    """Verify debounce constant values match header and helper."""

    def test_debounce_ms_in_header(self):
        with open(ADS_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("NVS_FLUSH_DEBOUNCE_MS = 3000", header)

    def test_debounce_ms_in_helper(self):
        self.assertEqual(ads.NVS_FLUSH_DEBOUNCE_MS, 3000)

    def test_debounce_reasonable_range(self):
        self.assertGreaterEqual(ads.NVS_FLUSH_DEBOUNCE_MS, 2000)
        self.assertLessEqual(ads.NVS_FLUSH_DEBOUNCE_MS, 5000)


class NvsDirtyFlagDeclarationTest(unittest.TestCase):
    """Verify dirty flag members are declared in the header."""

    def test_dirty_flag_declared(self):
        with open(CHANNEL_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("nvs_dirty_{false}", header)
        self.assertIn("nvs_dirty_since_ms_{0}", header)
        self.assertIn("nvs_pending_stamp_filters_{false}", header)
        self.assertIn("nvs_pending_stamp_interval_{false}", header)
        self.assertIn("nvs_shutdown_hook_registered_{false}", header)

    def test_flush_methods_declared(self):
        with open(CHANNEL_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("mark_nvs_dirty_", header)
        self.assertIn("flush_nvs_if_idle_", header)
        self.assertIn("flush_nvs_now_", header)
        self.assertIn("register_shutdown_hook_", header)


class NvsMarkDirtyImplementationTest(unittest.TestCase):
    """Verify mark_nvs_dirty_() implementation logic."""

    def test_mark_nvs_dirty_sets_flag_and_timestamp(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::mark_nvs_dirty_")
        self.assertIn("nvs_dirty_ = true", body)
        self.assertIn("nvs_dirty_since_ms_ = millis()", body)

    def test_mark_nvs_dirty_accumulates_stamps(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::mark_nvs_dirty_")
        self.assertIn("nvs_pending_stamp_filters_ =", body)
        self.assertIn("nvs_pending_stamp_interval_ =", body)
        self.assertIn("||", body)

    def test_mark_nvs_dirty_bails_on_no_prefs_key(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::mark_nvs_dirty_")
        self.assertIn("runtime_prefs_key_ == 0", body)
        self.assertIn("return", body)


class NvsFlushIfIdleImplementationTest(unittest.TestCase):
    """Verify flush_nvs_if_idle_() implementation logic."""

    def test_flush_checks_dirty_flag(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_if_idle_")
        self.assertIn("nvs_dirty_", body)

    def test_flush_checks_debounce_period(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_if_idle_")
        self.assertIn("NVS_FLUSH_DEBOUNCE_MS", body)

    def test_flush_calls_flush_nvs_now(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_if_idle_")
        self.assertIn("flush_nvs_now_", body)


class NvsFlushNowImplementationTest(unittest.TestCase):
    """Verify flush_nvs_now_() implementation logic."""

    def test_flush_now_calls_save_runtime_preferences(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_now_")
        self.assertIn("save_runtime_preferences", body)

    def test_flush_now_uses_pending_stamps(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_now_")
        self.assertIn("nvs_pending_stamp_filters_", body)
        self.assertIn("nvs_pending_stamp_interval_", body)

    def test_flush_now_clears_dirty_state(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_now_")
        self.assertIn("nvs_dirty_ = false", body)
        self.assertIn("nvs_pending_stamp_filters_ = false", body)
        self.assertIn("nvs_pending_stamp_interval_ = false", body)

    def test_flush_now_bails_if_not_dirty(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::flush_nvs_now_")
        lines = body.split("\n")
        early_return = False
        for line in lines[:10]:
            if "!this->nvs_dirty_" in line or "nvs_dirty_" in line:
                for following in lines[:10]:
                    if "return" in following:
                        early_return = True
                        break
        self.assertTrue(early_return or "if (!this->nvs_dirty_)" in body)


class NvsShutdownHookTest(unittest.TestCase):
    """Verify shutdown hook registration."""

    def test_shutdown_hook_registers(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::register_shutdown_hook_")
        self.assertIn("App.register_shutdown_hook", body)
        self.assertIn("flush_nvs_now_", body)

    def test_shutdown_hook_guards_double_registration(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::register_shutdown_hook_")
        self.assertIn("nvs_shutdown_hook_registered_", body)

    def test_shutdown_hook_called_in_setup(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::setup")
        self.assertIn("register_shutdown_hook_", body)


class NvsLoopFlushTest(unittest.TestCase):
    """Verify loop() calls flush_nvs_if_idle_()."""

    def test_loop_calls_flush(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::loop")
        self.assertIn("flush_nvs_if_idle_", body)


class NvsApplyMethodsUseDirtyFlagTest(unittest.TestCase):
    """Verify apply_* methods use mark_nvs_dirty_() instead of direct save."""

    def test_apply_gain_runtime_uses_dirty_flag(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::apply_gain_runtime")
        self.assertIn("mark_nvs_dirty_", body)
        self.assertNotIn("this->save_runtime_preferences()", body.replace(
            "// Design §5.3", "").split("mark_nvs_dirty_")[0])

    def test_persist_filters_if_uses_dirty_flag(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::persist_filters_if_")
        self.assertIn("mark_nvs_dirty_", body)
        self.assertNotIn("save_runtime_preferences(true, false)", body)

    def test_persist_interval_if_uses_dirty_flag(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::persist_interval_if_")
        self.assertIn("mark_nvs_dirty_", body)
        self.assertNotIn("save_runtime_preferences(false, true)", body)

    def test_reset_filters_to_yaml_uses_dirty_flag(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::reset_filters_to_yaml")
        self.assertIn("mark_nvs_dirty_", body)

    def test_remember_gain_at_cal_save_flushes_immediately(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        body = _fn(source, "void PoolStationChannelSensor::remember_gain_at_cal_save")
        self.assertIn("save_runtime_preferences()", body)
        self.assertIn("nvs_dirty_ = false", body)


class NvsNoDirectSaveCallsTest(unittest.TestCase):
    """Verify no unintended direct calls to save_runtime_preferences()."""

    def test_only_allowed_direct_saves(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        save_calls = [
            m.start() for m in re.finditer(r"save_runtime_preferences\s*\(", source)
        ]
        allowed_callers = [
            "void PoolStationChannelSensor::save_runtime_preferences",
            "void PoolStationChannelSensor::remember_gain_at_cal_save",
            "void PoolStationChannelSensor::flush_nvs_now_",
        ]
        for pos in save_calls:
            context_start = max(0, pos - 500)
            context = source[context_start:pos + 100]
            is_allowed = any(caller in context for caller in allowed_callers)
            self.assertTrue(
                is_allowed,
                f"Unexpected save_runtime_preferences() call near: ...{source[max(0, pos - 50):pos + 50]}..."
            )


class NvsCommentDocumentationTest(unittest.TestCase):
    """Verify the deferred flush is documented in the code."""

    def test_header_documents_debounce_purpose(self):
        with open(ADS_H, encoding="utf-8") as handle:
            header = handle.read()
        self.assertIn("deferred flush", header.lower())
        self.assertIn("coalesce", header.lower())

    def test_cpp_documents_v0104(self):
        with open(CHANNEL_CPP, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("v0.10.4", source)
        self.assertIn("deferred flush", source.lower())


if __name__ == "__main__":
    raise SystemExit(unittest.main())
