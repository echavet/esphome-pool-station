#!/usr/bin/env python3
"""Unit tests for algorithm select option registration (v0.7.12).

Reproduces the live HA bug: select.register_select(..., options=[]) advertises
an empty list, so entities stay `unknown` with `options: []`.

This module is free of esphome imports. It checks the shared option list and
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

EXPECTED_OPTIONS = (
    "none",
    "linear",
    "polynomial",
    "piecewise",
    "dfrobot_orp",
)


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


class AlgorithmSelectOptionsTest(unittest.TestCase):
    def test_shared_options_are_nonempty_implemented_types(self):
        options = helper.ALGORITHM_SELECT_OPTIONS
        self.assertTrue(options, "algorithm select options must not be empty")
        self.assertEqual(options, EXPECTED_OPTIONS)
        self.assertNotIn("exponential", options)
        self.assertNotIn("logarithmic", options)
        self.assertNotIn("power", options)

    def test_codegen_does_not_register_empty_options(self):
        with open(INIT_PATH, encoding="utf-8") as handle:
            source = handle.read()
        block = _algorithm_select_codegen_block(source)
        compact = block.replace(" ", "")
        self.assertNotIn("options=[]", compact)
        self.assertIn("ALGORITHM_SELECT_OPTIONS", block)
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
            any("ALGORITHM_SELECT_OPTIONS" in arg for arg in options_args),
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

    def test_cpp_set_options_matches_shared_list(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            source = handle.read()
        match = re.search(
            r"this->traits\.set_options\(\{([^}]+)\}\)",
            source,
        )
        self.assertIsNotNone(match, "C++ set_options initializer not found")
        literals = re.findall(r'"([^"]+)"', match.group(1))
        self.assertEqual(tuple(literals), helper.ALGORITHM_SELECT_OPTIONS)

    def test_cpp_falls_back_when_option_missing(self):
        with open(CPP_PATH, encoding="utf-8") as handle:
            source = handle.read()
        self.assertIn('type_str = "none"', source)
        self.assertIn("is not in select options", source)


if __name__ == "__main__":
    raise SystemExit(unittest.main())
