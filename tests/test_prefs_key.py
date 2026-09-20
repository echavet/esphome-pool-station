#!/usr/bin/env python3
"""Unit tests for stable NVS preferences keys (v0.7.16).

The previous helper used Python's randomized hash(). These tests lock the
MD5 scheme so a recompile cannot change keys, and so a scheme bump is
visible.

Run with: python3 tests/test_prefs_key.py
"""

import hashlib
import importlib.util
import os
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HELPER_PATH = os.path.join(ROOT, "components", "pool_station", "prefs_key.py")
INIT_PATH = os.path.join(ROOT, "components", "pool_station", "__init__.py")


def _load_helper():
    spec = importlib.util.spec_from_file_location(
        "prefs_key", os.path.abspath(HELPER_PATH)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


helper = _load_helper()

# Golden value for scheme ps-md5-v1. Independent of PYTHONHASHSEED.
# payload = b"ps-md5-v1:pool:0"
GOLDEN_POOL_PRESSURE = int.from_bytes(
    hashlib.md5(b"ps-md5-v1:pool:0").digest()[:4], "big"
)
GOLDEN_RUNTIME_POOL_PRESSURE = int.from_bytes(
    hashlib.md5(b"ps-md5-rt-v1:pool:0").digest()[:4], "big"
)


class PrefsKeyTest(unittest.TestCase):
    def test_scheme_prefix(self):
        self.assertEqual(helper.PREFS_KEY_SCHEME, "ps-md5-v1")

    def test_golden_key_stable(self):
        self.assertEqual(helper.generate_preferences_key("pool", 0), GOLDEN_POOL_PRESSURE)
        self.assertEqual(GOLDEN_POOL_PRESSURE, 0x867CA26C)

    def test_deterministic_across_calls(self):
        a = helper.generate_preferences_key("pool", 1)
        b = helper.generate_preferences_key("pool", 1)
        self.assertEqual(a, b)

    def test_channel_types_differ(self):
        keys = [
            helper.generate_preferences_key("pool", ch)
            for ch in (0, 1, 2)
        ]
        self.assertEqual(len(set(keys)), 3)

    def test_component_id_changes_key(self):
        self.assertNotEqual(
            helper.generate_preferences_key("pool", 0),
            helper.generate_preferences_key("pool_alt", 0),
        )

    def test_uint32_range(self):
        key = helper.generate_preferences_key("pool", 2)
        self.assertGreaterEqual(key, 0)
        self.assertLessEqual(key, 0xFFFFFFFF)
        self.assertIsInstance(key, int)

    def test_does_not_use_builtin_hash(self):
        """Same inputs must not depend on hash() randomization."""
        # Built-in hash of a tuple is process-salted; our key must match MD5.
        salted = hash(("pool", 0)) & 0xFFFFFFFF
        stable = helper.generate_preferences_key("pool", 0)
        self.assertEqual(
            stable,
            int.from_bytes(hashlib.md5(b"ps-md5-v1:pool:0").digest()[:4], "big"),
        )
        # They *may* collide by chance; the contract is MD5 identity, not inequality.
        _ = salted

    def test_init_imports_helper_not_builtin_hash(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn("from .prefs_key import generate_preferences_key", source)
        self.assertIn("generate_runtime_preferences_key", source)
        self.assertNotIn("hash((base_key, channel_type))", source)
        self.assertNotIn("PYTHONHASHSEED", source.split("generate_preferences_key")[0][-80:])

    def test_runtime_scheme_prefix(self):
        self.assertEqual(helper.RUNTIME_PREFS_KEY_SCHEME, "ps-md5-rt-v1")
        self.assertEqual(helper.PREFS_KEY_SCHEME, "ps-md5-v1")

    def test_runtime_key_differs_from_cal_key(self):
        cal = helper.generate_preferences_key("pool", 0)
        runtime = helper.generate_runtime_preferences_key("pool", 0)
        self.assertEqual(cal, GOLDEN_POOL_PRESSURE)
        self.assertEqual(GOLDEN_POOL_PRESSURE, 0x867CA26C)
        self.assertEqual(runtime, GOLDEN_RUNTIME_POOL_PRESSURE)
        self.assertNotEqual(cal, runtime)

    def test_runtime_key_deterministic_and_per_channel(self):
        a = helper.generate_runtime_preferences_key("pool", 1)
        b = helper.generate_runtime_preferences_key("pool", 1)
        self.assertEqual(a, b)
        keys = [
            helper.generate_runtime_preferences_key("pool", ch) for ch in (0, 1, 2)
        ]
        self.assertEqual(len(set(keys)), 3)
        self.assertLessEqual(keys[0], 0xFFFFFFFF)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
