# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.2.0] - 2026-09-13

### Added — Lot 2: N-Point Calibration & Capturer UI

#### Calibration Engine
- New `CalibrationEngine` class in separate module (`calibration_engine.h/cpp`)
- N-point calibration support (1 to 10 points per channel)
- Multiple calibration algorithms:
  - **linear**: 2-point linear interpolation/extrapolation
  - **piecewise**: Multi-point linear segments between adjacent points
  - **polynomial**: Least-squares polynomial regression (order 1-5)
  - **dfrobot_orp**: SEN0165-like mid/offset formula for ORP sensors
  - **exponential, logarithmic, power**: Stubs with TODO (not yet implemented)

#### Capturer UI (Home Assistant Integration)
- **Capture Buttons** (`CalibrationCaptureButton`): Press to copy current raw voltage into calibration point X
- **Point X Numbers** (`CalibrationPointXNumber`): Edit raw voltage value of each calibration point
- **Point Y Numbers** (`CalibrationPointYNumber`): Edit calibrated value of each calibration point
- **Save Button** (`CalibrationSaveButton`): Persist calibration to flash storage
- **DFRobot Mid Number** (`DFRobotMidNumber`): Adjust mid_mv parameter for dfrobot_orp algorithm
- **DFRobot Offset Number** (`DFRobotOffsetNumber`): Adjust offset_mv parameter for dfrobot_orp algorithm

#### Calibration Persistence
- Calibration data persists to flash via ESPHome preferences
- Automatically loads on boot
- YAML seed points provide initial calibration, overridable at runtime
- Save button commits runtime changes to flash

#### Validation & Feedback
- `CalibrationInvalidSensor` (`cal_invalid` binary_sensor): Indicates when calibration is insufficient
- Minimum point validation per algorithm type
- Graceful fallback to pass-through when calibration invalid
- Enhanced logging with tags `pool_station`, `pool_station.cal`, `pool_station.ui`

#### YAML Configuration
- New `calibration` block per channel:
  - `type`: Calibration algorithm selection
  - `order`: Polynomial order (1-5)
  - `precision`: Output decimal precision
  - `points`: Seed calibration points (x, y pairs)
  - `mid_mv`, `offset_mv`: DFRobot ORP specific parameters
- New `capturer` block per channel:
  - `point_count`: Number of calibration points (1-10)
  - `capture_buttons`: Enable/disable capture buttons
  - `point_numbers`: Enable/disable x/y number entities
  - `save_button`: Enable/disable save button
  - `mid_number`, `offset_number`: Enable DFRobot parameter numbers
- New `cal_invalid` binary_sensor per channel

### Changed
- `PoolStationChannelSensor` now integrates `CalibrationEngine`
- `apply_calibration_()` delegates to engine instead of inline stub code
- Updated Python codegen (`__init__.py`) with calibration and capturer schema
- AUTO_LOAD now includes `number`, `button`, `binary_sensor` platforms
- Example YAML demonstrates full Lot 2 calibration workflow
- README updated with calibration documentation and step-by-step guide

### Technical Notes
- Preferences key generated from component ID hash + channel type
- Polynomial regression uses Gaussian elimination with partial pivoting
- Piecewise calibration extrapolates beyond endpoint segments
- DFRobot formula: `ORP_mV = mid_mv - (V × 1000) - offset_mv`

---

## [0.1.0] - 2026-09-13

### Added — Lot 1: ADS1115 Channel Binding

#### Real ADS1115-Backed Channels
- **Pressure channel** on ADS1115 A0_GND with configurable gain (default 4.096)
- **pH channel** on ADS1115 A2_GND with configurable gain
- **ORP channel** on ADS1115 A3_GND with configurable gain
- Channels compose native ESPHome `ads1115` sensors — does NOT reimplement the ADC driver

#### Raw Voltage Diagnostic Sensors
- Each channel exposes a `raw_sensor` with actual ADC voltage
- Raw sensors are always available (not just in calibration mode)
- Entity category: diagnostic for Home Assistant organization

#### Calibration Mode Switch
- Global switch to enable high-frequency sampling
- When ON: all channels sample at `calibration_interval` (default 1s)
- When OFF: channels use their configured `update_interval`
- Useful for calibration with reference solutions

#### Channel Configuration
- `source_id`: Reference to native ESPHome ads1115 sensor
- `name`: Display name for calibrated sensor
- `unit_of_measurement`, `accuracy_decimals`, `icon`: Sensor metadata
- `update_interval`: Normal mode sampling interval
- `raw_sensor`: Optional diagnostic sensor configuration

#### Pressure Defaults
- Update interval: 1-2s (fast response for filter monitoring)
- No heavy filters applied (filters coming in Lot 3)

#### Architecture Improvements
- Clean channel/backend configuration schema
- `PoolStationChannelSensor` class wraps source sensors
- Subscription-based value propagation (no polling duplication)
- `CalibrationModeSwitch` class for mode control
- Backward compatible with Lot 0 role-based sensors

### Changed
- Refactored `__init__.py` with channels-based configuration
- Updated example YAML for Lot 1 usage pattern
- README updated with Lot 1 documentation and Quick Start

### Technical Notes
- Calibration is pass-through stub (actual N-point calibration in Lot 2)
- ORP converts voltage to millivolts as simple transform
- Pressure passes through voltage (formula configuration in Lot 2)
- pH passes through voltage (N-point calibration in Lot 2)

---

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
- `examples/pool-station-minimal.yaml` — Configuration example
- `CHANGELOG.md` — This file
- `LICENSE` — MIT License

#### Technical
- Log tag: `pool_station`
- Compatible with ESPHome external_components
- Follows MitsubishiCN105ESPHome component architecture
- No external dependencies (composes native ESPHome components)

---

## Roadmap

| Lot | Version | Content | Status |
|-----|---------|---------|--------|
| 0 | 0.0.1 | ✅ Skeleton, docs | Done |
| 1 | 0.1.0 | ✅ ADS1115 binding, raw values, calibration mode | Done |
| **2** | **0.2.0** | ✅ **N-point calibration, Capturer UI, persistence** | **Current** |
| 3 | 0.3.0 | Filters (j5-like) | Planned |
| 4 | 0.4.0 | Diagnostics | Planned |
| 5 | 0.5.0 | Temperature compensation | Planned |
| 6 | 0.6.0 | Gates/campaigns | Planned |
| 7 | 1.0.0 | HA polish, stable release | Planned |

## Future Lots (Lot 7 candidates)

The following features are documented for potential inclusion in Lot 7 or later:

- **Algorithm selection at runtime**: Select entity to change calibration type from HA
- **Add/remove point buttons**: Dynamic point management from HA UI
- **Draft/commit workflow**: Preview calibration changes before applying
