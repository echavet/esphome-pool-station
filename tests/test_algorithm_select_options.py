#!/usr/bin/env python3
"""Unit tests for per-channel algorithm select options (v0.7.13).

v0.7.12 registered one shared list including dfrobot_orp on every channel,
so HA showed dfrobot_orp on pH and pressure. Options must be per-channel.

This module is free of esphome imports. It checks the shared option lists and
asserts codegen / C++ stay aligned.

Run with: python3 tests/test_algorithm_select_options.py
"""

import ast
import os
import re
import unittest

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
HELPER_PATH = os.path.join(
    ROOT, "components", "pool_station", "algorithm_select_options.py"
)
INIT_PATH = os.path.join(ROOT, "components", "pool_station", "__init__.py")
CPP_PATH = os.path.join(ROOT, "components", "pool_station", "calibration_ui.cpp")


def _load_helper():
    import importlib.util

    spec = importlib.util.spec_from_file_location(
        "algorithm_select_options", os.path.abspath(HELPER_PATH)
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


helper = _load_helper()

EXPECTED_GENERIC = (
    "none",
    "linear",
    "polynomial",
    "piecewise",
)
EXPECTED_ORP = EXPECTED_GENERIC + ("dfrobot_orp",)
UNIMPLEMENTED = ("exponential", "logarithmic", "power")


def _algorithm_select_codegen_block(source):
    """Return the capturer algorithm-select codegen snippet from __init__.py."""
    start = source.find("if CONF_ALGORITHM_SELECT in capturer_conf:")
    if start < 0:
        raise AssertionError("CONF_ALGORITHM_SELECT codegen block not found")
    end = source.find("# Add point button", start)
    if end < 0:
        end = start + 800
    return source[start:end]


def _register_select_options_ast(source):
    """Parse options= passed to select.register_select in the algo block."""
    tree = ast.parse(source)
    found = []

    class Visitor(ast.NodeVisitor):
        def visit_Call(self, node):
            func = node.func
            name = None
            if isinstance(func, ast.Attribute) and func.attr == "register_select":
                name = "register_select"
            if name == "register_select":
                for kw in node.keywords:
                    if kw.arg == "options":
                        found.append(ast.unparse(kw.value))
            self.generic_visit(node)

    Visitor().visit(tree)
    return found


def _cpp_setup_set_options(source):
    """Return (orp_literals, generic_literals) from CalibrationAlgorithmSelect::setup()."""
    setup_match = re.search(
        r"void CalibrationAlgorithmSelect::setup\(\)\s*\{(?P<body>.*?)^\}",
        source,
        re.DOTALL | re.MULTILINE,
    )
    if setup_match is None:
        raise AssertionError("CalibrationAlgorithmSelect::setup() not found")
    body = setup_match.group("body")
    orp_match = re.search(
        r"channel_type_\s*==\s*CHANNEL_TYPE_ORP\)\s*\{.*?set_options\(\{(?P<opts>[^}]+)\}\)",
        body,
        re.DOTALL,
    )
    else_match = re.search(
        r"else\s*\{[^}]*set_options\(\{(?P<opts>[^}]+)\}\)",
        body,
        re.DOTALL,
    )
    if orp_match is None or else_match is None:
        raise AssertionError(
            "C++ setup() must branch on CHANNEL_TYPE_ORP with two set_options lists"
        )
    orp_literals = tuple(re.findall(r'"([^"]+)"', orp_match.group("opts")))
    generic_literals = tuple(re.findall(r'"([^"]+)"', else_match.group("opts")))
    return orp_literals, generic_literals


class AlgorithmSelectOptionsTest(unittest.TestCase):
    def test_pressure_and_ph_exclude_dfrobot_orp(self):
        for channel_type in (
            helper.CHANNEL_TYPE_PRESSURE,
            helper.CHANNEL_TYPE_PH,
        ):
            options = helper.algorithm_select_options(channel_type)
            self.assertTrue(options, "algorithm select options must not be empty")
            self.assertEqual(options, EXPECTED_GENERIC)
            self.assertNotIn("dfrobot_orp", options)
            for name in UNIMPLEMENTED:
                self.assertNotIn(name, options)

    def test_orp_includes_dfrobot_orp(self):
        options = helper.algorithm_select_options(helper.CHANNEL_TYPE_ORP)
        self.assertTrue(options, "ORP algorithm select options must not be empty")
        self.assertEqual(options, EXPECTED_ORP)
        self.assertIn("dfrobot_orp", options)
        # CalibrationEngine also implements linear/polynomial/piecewise for ORP
        for name in EXPECTED_GENERIC:
            self.assertIn(name, options)
        for name in UNIMPLEMENTED:
            self.assertNotIn(name, options)

    def test_named_tuples_match_function(self):
        self.assertEqual(helper.ALGORITHM_SELECT_OPTIONS_GENERIC, EXPECTED_GENERIC)
        self.assertEqual(helper.ALGORITHM_SELECT_OPTIONS_ORP, EXPECTED_ORP)
        self.assertNotIn("dfrobot_orp", helper.ALGORITHM_SELECT_OPTIONS_GENERIC)
        self.assertIn("dfrobot_orp", helper.ALGORITHM_SELECT_OPTIONS_ORP)

    def test_codegen_does_not_register_empty_options(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        block = _algorithm_select_codegen_block(source)
        compact = block.replace(" ", "")
        self.assertNotIn("options=[]", compact)
        self.assertIn("algorithm_select_options", block)
        self.assertIn("algorithm_select_options(channel_type)", block)
        self.assertIn("await cg.register_component(", block)
        # Parent/channel must be set before register_component in this block
        parent_at = block.find("set_parent(")
        channel_at = block.find("set_channel_type(")
        register_at = block.find("await cg.register_component(")
        self.assertGreater(parent_at, 0)
        self.assertGreater(channel_at, 0)
        self.assertGreater(register_at, 0)
        self.assertLess(parent_at, register_at)
        self.assertLess(channel_at, register_at)

        options_args = _register_select_options_ast(source)
        self.assertTrue(options_args, "register_select options= not found")
        self.assertTrue(
            any("algorithm_select_options" in arg for arg in options_args),
            options_args,
        )
        self.assertFalse(
            any(arg in {"[]", "list()"} for arg in options_args),
            options_args,
        )

    def test_only_one_register_select_in_component(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertEqual(source.count("register_select("), 1)

    def test_cpp_set_options_are_channel_aware(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            source = handle.read()
        orp_literals, generic_literals = _cpp_setup_set_options(source)
        self.assertEqual(orp_literals, helper.ALGORITHM_SELECT_OPTIONS_ORP)
        self.assertEqual(generic_literals, helper.ALGORITHM_SELECT_OPTIONS_GENERIC)
        self.assertIn("dfrobot_orp", orp_literals)
        self.assertNotIn("dfrobot_orp", generic_literals)

    def test_cpp_falls_back_when_option_missing(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn('type_str = "none"', source)
        self.assertIn("is not in select options", source)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
