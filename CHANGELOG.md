# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.1] - 2026-09-13

### Added — Lot 0: Skeleton

#### Component Structure
- `components/pool_station/__init__.py` — Main Python codegen with CONFIG_SCHEMA
- `components/pool_station/sensor.py` — Sensor platform for pool_station sensors
- `components/pool_station/pool_station.h` — C++ header with component and sensor classes
- `components/pool_station/pool_station.cpp` — C++ implementation with stub functionality

#### Features (Stubs)
- `PoolStationComponent` class — Main orchestrator component
- `PoolStationSensor` class — Role-based sensor with calibration hooks
- Calibration mode configuration (stub — actual impl in Lot 2)
- Water temperature sensor binding (stub — actual impl in Lot 5)
- Sensor role enumeration: water_temperature, air_pump, air_local, ph_raw, ph_calibrated, orp_raw, orp_calibrated, pressure_raw, pressure_calibrated

#### Documentation
- `README.md` — Vision, installation, roadmap, quick start
- `docs/POOL-STATION-CDC.md` — Full requirements specification (CDC)
- `docs/SENSOR-IDENTITY.md` — Anti-swap address binding table
- `examples/pool-station-minimal.yaml` — Future-facing configuration example
- `CHANGELOG.md` — This file
- `LICENSE` — MIT License

#### Technical
- Log tag: `pool_station`
- Compatible with ESPHome external_components
- Follows MitsubishiCN105ESPHome component architecture
- No external dependencies (composes native ESPHome components)

### Notes
- This is a skeleton release — sensors register but don't publish actual calibrated values
- ADS1115/Dallas binding requires Lot 1
- N-point calibration requires Lot 2
- See roadmap in README.md for planned lots

---

## Roadmap

| Lot | Version | Content |
|-----|---------|---------|
| 0 | 0.0.1 | ✅ Skeleton, docs |
| 1 | 0.1.0 | ADS1115 binding, raw values |
| 2 | 0.2.0 | N-point calibration, Capturer |
| 3 | 0.3.0 | Filters (j5-like) |
| 4 | 0.4.0 | Diagnostics |
| 5 | 0.5.0 | Temperature compensation |
| 6 | 0.6.0 | Gates/campaigns |
| 7 | 1.0.0 | HA polish, stable release |
