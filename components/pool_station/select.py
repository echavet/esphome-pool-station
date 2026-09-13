"""
ESPHome pool_station select platform.
Lot 7: Runtime algorithm selection.
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import select
from esphome.const import (
    CONF_ID,
    CONF_ENTITY_CATEGORY,
    ENTITY_CATEGORY_CONFIG,
)

CODEOWNERS = ["@echavet"]
DEPENDENCIES = ["pool_station"]

pool_station_ns = cg.esphome_ns.namespace("pool_station")

CalibrationAlgorithmSelect = pool_station_ns.class_(
    "CalibrationAlgorithmSelect", select.Select, cg.Component
)
