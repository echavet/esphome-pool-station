"""Stable NVS / ESPHome preferences keys for pool_station calibration.

Python's built-in ``hash()`` is randomized per process (PYTHONHASHSEED) since
Python 3.3. Using ``hash((base_key, channel_type)) & 0xFFFFFFFF`` as a flash
key meant a firmware recompile could orphan previously saved calibration.

This module uses MD5 of a versioned payload so the same
``(component_id, channel_type)`` always yields the same uint32 key.

Scheme ``ps-md5-v1`` is intentionally versioned. If the payload format changes,
bump the prefix and document a one-time recalibration in CHANGELOG / MIGRATION.
Old PYTHONHASHSEED keys cannot be reconstructed, so v0.7.16 cannot migrate
them automatically.
"""

from __future__ import annotations

import hashlib

# Bump this prefix if the payload format changes.
PREFS_KEY_SCHEME = "ps-md5-v1"


def generate_preferences_key(base_key, channel_type):
    """Return a deterministic 32-bit preferences key.

    Args:
        base_key: Component id string (e.g. ``str(config[CONF_ID])``).
        channel_type: Integer channel type (pressure=0, ph=1, orp=2) or
            any value convertible with ``int()``.

    Returns:
        uint32 (0 .. 0xFFFFFFFF) suitable for ``set_preferences_key``.
    """
    payload = f"{PREFS_KEY_SCHEME}:{base_key}:{int(channel_type)}".encode("utf-8")
    digest = hashlib.md5(payload).digest()
    return int.from_bytes(digest[:4], byteorder="big")
