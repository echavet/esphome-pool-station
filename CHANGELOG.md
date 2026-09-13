# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.7.13] - 2026-09-13

### Fixed — Algorithm Select Options Are Per-Channel

#### CRIT: `dfrobot_orp` advertised on pH and pressure after v0.7.12
- **Symptoms**: Home Assistant pH / Pression algorithm selects include `dfrobot_orp`
- **Root cause**: v0.7.12 used one shared `ALGORITHM_SELECT_OPTIONS` for every channel
- **Fix**:
  - pressure / pH: `none`, `linear`, `polynomial`, `piecewise`
  - ORP: same list plus `dfrobot_orp` (engine also supports linear/polynomial/piecewise)
  - C++ `CalibrationAlgorithmSelect::setup()` branches on `channel_type_`
  - Codegen still uses non-empty `register_select` options + `register_component` order from 0.7.12

---

## [0.7.12] - 2026-09-13

### Fixed — Algorithm Select Empty Options in Home Assistant

#### CRIT: `select.*_algorithm` entities stuck at `unknown` with `options: []`
- **Symptoms**: `select.piscine_pool_station_ph_algorithm` and
  `select.piscine_pool_station_pression_algorithm` show state `unknown`
  and an empty options list in Home Assistant (live v0.7.11 / ESPHome 2026.4.3)
- **Root cause**: capturer codegen called `select.register_select(..., options=[])`.
  HA discovery uses that list at registration time. C++ `setup()` later called
  `traits.set_options({...})`, but the entity was already advertised empty and
  `publish_state()` of the current algorithm failed / stayed unknown.
  `register_component` was also missing, so `setup()` / `update_from_calibration()`
  may never run
- **Scan**: only one `register_select` in this component; no other empty-options selects
- **Fix**:
  - Shared `ALGORITHM_SELECT_OPTIONS` (`none`, `linear`, `polynomial`,
    `piecewise`, `dfrobot_orp`) passed into `select.register_select(...)`
  - `cg.register_component` after `set_parent` / `set_channel_type` so
    `setup()` can publish a value that exists in the option list
  - C++ `set_options` kept as belt-and-suspenders; `update_from_calibration`
    falls back to `none` if the engine type is not in the exposed list

---

## [0.7.11] - 2026-09-13

### Fixed — Channel `filters:` vs ESPHome sensor.register_sensor Collision

#### CRIT: Codegen Crash on Channels with pool_station `filters:`
- **Error**: `AttributeError: 'EStr' object has no attribute 'items'` in `sensor.setup_sensor_core_` → `build_filters()`
- **Root cause**: v0.7.9+ calls `sensor.register_sensor(ch_var, ch_conf)` so ESPHome 2026.x can set entity metadata. Channel YAML already uses `filters:` for pool_station guards (`filter_samples`, `max_jump`, `max_jump_streak`, `value_min`, `value_max`). ESPHome treats `CONF_FILTERS` as the standard sensor-filter registry and iterates that dict as a list of filter configs
- **YAML unchanged**: existing `filters: filter_samples/max_jump/...` semantics are preserved
- **Fix**:
  - Before `register_sensor`, pass a shallow copy of the channel config with colliding keys stripped (`filters`)
  - Apply channel guards via the existing `set_filter_*` path on the original config
  - Scan of other channel keys vs `setup_sensor_core_` / `setup_entity`: only `filters` collides
- register_sensor entity-metadata approach from v0.7.9/v0.7.10 preserved

---

## [0.7.10] - 2026-09-13

### Fixed — "string value is None" Validation Error for pH/ORP Channels

#### CRIT: device_class=None Triggers ESPHome 2026.4+ Validation Error
- **Error**: `string value is None.` appearing after each channel block during `esphome config` validation
- **Root cause**: `channel_schema()` passed `device_class=defaults.get("device_class")` to `sensor.sensor_schema()`. For pH and ORP channels, `CHANNEL_DEFAULTS` set `"device_class": None`, and passing `device_class=None` explicitly triggers ESPHome's string validation which rejects `None` values
- **Affected channels**: pH, ORP (pressure worked because it has `device_class=DEVICE_CLASS_PRESSURE`)
- **Fix**:
  - Modified `channel_schema()` to build kwargs dynamically and only include `device_class` when it has a valid non-None value
  - Removed `"device_class": None` entries from `CHANNEL_DEFAULTS` in `__init__.py` and `SENSOR_DEFAULTS` in `sensor.py`
  - Added comments documenting that pH/ORP have no standard Home Assistant device class
- register_sensor approach from v0.7.9 preserved

---

## [0.7.9] - 2026-09-13

### Fixed — PoolStationChannelSensor Entity Metadata Setters

#### CRIT: Channel Sensors Missing set_name/set_unit/set_icon/set_state_class Methods
- **Error**: `error: 'class esphome::pool_station::PoolStationChannelSensor' has no member named 'set_name'; did you mean 'get_name'?`
- **Also**: `set_unit_of_measurement`, `set_icon`, `set_device_class` errors, and `set_state_class` expecting enum not string
- **Root cause**: Python codegen called manual setters (`set_name()`, `set_unit_of_measurement()`, etc.) directly on `PoolStationChannelSensor`, but ESPHome 2026.x expects these to be configured via `sensor.register_sensor()` which uses the standard entity helper infrastructure
- **Affected entities**: All channel sensors (pressure, pH, ORP)
- **Fix**:
  - Changed `channel_schema()` to extend `sensor.sensor_schema()` instead of bare `cv.Schema()`
  - Schema now includes proper `unit_of_measurement`, `accuracy_decimals`, `icon`, `device_class`, `state_class` configuration at schema level
  - `to_code()` now calls `await sensor.register_sensor(ch_var, ch_conf)` which configures all entity metadata via ESPHome's standard codegen helpers
  - Removed manual setter calls that aren't available in ESPHome 2026.x sensor API
- TAG redefinition and SelectTraits fixes from 0.7.7/0.7.8 preserved

---

## [0.7.8] - 2026-09-13

### Fixed — TAG Redefinition Compilation Error

#### CRIT: TAG Redefinition in pool_station Namespace
- **Error**: `error: redefinition of 'const char* const esphome::pool_station::TAG'`
- **Root cause**: `pool_station.h` defined `TAG` at namespace level, and `.cpp` files that included it (directly or via other headers) also defined their own `TAG`, causing redefinition errors in the same translation unit
- **Affected files**:
  - `calibration_ui.cpp`: included `calibration_ui.h` → `pool_station.h`, then redefined `TAG`
  - `measurement_campaign.cpp`: included `pool_station.h` directly, then redefined `TAG`
- **Fix**: Removed `TAG` from `pool_station.h` header. Each `.cpp` file now defines its own `TAG` locally (standard ESPHome pattern):
  - `pool_station.cpp`: `TAG = "pool_station"`
  - `calibration_ui.cpp`: `TAG = "pool_station.ui"`
  - `measurement_gate.cpp`: `TAG = "pool_station.gate"`
  - `measurement_campaign.cpp`: `TAG = "pool_station.campaign"`
- Also removed unused `CAMPAIGN_TAG` from `measurement_campaign.h` and `GATE_TAG` from `measurement_gate.h`
- LOG_* macros now work correctly with file-local `TAG` definitions
- SelectTraits fix from 0.7.7 preserved

---

## [0.7.7] - 2026-09-13

### Fixed — SelectTraits::set_options + LOG_* TAG

#### CRIT-1: SelectTraits::set_options API Mismatch
- **Error**: `error: no matching function for call to set_options(const std::vector<std::string>&)`
- **Root cause**: ESPHome 2026.4 `SelectTraits::set_options()` only accepts `std::initializer_list<const char*>`, not `std::vector<std::string>`
- **Fix**: Removed `ALGORITHM_OPTIONS` static vector, pass string literals directly:
  ```cpp
  this->traits.set_options({"none", "linear", "polynomial", "piecewise", "dfrobot_orp"});
  ```

#### CRIT-2: LOG_* Macros Missing TAG Variable
- **Error**: `error: 'TAG' was not declared in this scope` in LOG_BINARY_SENSOR, LOG_SENSOR, etc.
- **Root cause**: ESPHome LOG_* macros require a `static const char *const TAG` in scope
- **Fix**: Added TAG definitions to all .cpp files that use these macros:
  - `calibration_ui.cpp`: `TAG = "pool_station.ui"`
  - `measurement_gate.cpp`: `TAG = "pool_station.gate"`
  - `measurement_campaign.cpp`: `TAG = "pool_station.campaign"`
  - `pool_station.cpp`: Already has TAG via `pool_station.h`

---

## [0.7.6] - 2026-09-13

### Fixed — C++ Compilation Errors + Campaign Schema

#### CRIT-1: Invalid Hex Literal in calibration_engine.h
- **Error**: `error: unable to find numeric literal operator 'operator""L10005'`
- **Root cause**: `0xCAL10005` used 'L' which is not a valid hex digit (0-9, A-F)
- **Fix**: Changed to `0xCA110005` (uses '1' instead of 'L' for visual similarity)
- Also fixed legacy magic `0xCAL10002` → `0xCA110002`

#### CRIT-2: CancelToken API Mismatch in pool_station.h
- **Error**: `error: 'CancelToken' is not a member of 'esphome::CallbackManager<void(float)>'`
- **Root cause**: ESPHome 2026.4 CallbackManager doesn't expose CancelToken publicly
- **Fix**: Removed `source_callback_` member - callback lifetime matches component lifetime, no cancellation needed

#### CRIT-3: Campaign Result Sensors Schema (from 0.7.5)
- **Error**: `KeyError: 'disabled_by_default'` in setup_campaigns
- **Root cause**: `campaign_result_sensor_schema()` used bare `cv.Schema({...})`
- **Fix**: Changed to `sensor.sensor_schema(CampaignResultSensor, ...).extend({...})`

---

## [0.7.5] - 2026-09-13

### Fixed — Campaign Result Sensors Schema Validation

(Superseded by 0.7.6 which includes all fixes)

---

## [0.7.4] - 2026-09-13

### Fixed — Robust Schema Validation for Dynamic Entities

#### CRIT: Dynamic Entity Configs Missing Required Keys (KeyError: 'mode', 'disabled_by_default')
- **Root cause**: Capturer UI entities created dynamically in loops were using bare dictionaries that didn't pass through ESPHome platform schemas
- ESPHome 2026.4.x `entity_helpers.py` requires multiple keys (`mode`, `disabled_by_default`, etc.) in entity configs
- Manual key addition (0.7.3) was insufficient — whack-a-mole approach
- **Robust fix**: All dynamic entity configs now pass through proper ESPHome platform schemas:
  - `button.button_schema(Class, icon=..., entity_category=...)` for buttons
  - `number.number_schema(Class, icon=..., entity_category=..., unit_of_measurement=...)` for numbers
  - Schemas automatically add ALL required defaults (`mode`, `disabled_by_default`, `internal`, etc.)
- Pre-create schemas once at function start, then validate each config through them
- Pattern now matches how ESPHome core components create dynamic entities

#### Affected entities (now schema-validated):
- Capture buttons (`pressure_capture_0`, etc.)
- Point X/Y numbers
- Save button
- DFRobot mid/offset numbers

---

## [0.7.3] - 2026-09-13

### Fixed — Missing Entity Schema Defaults (KeyError: 'disabled_by_default')

#### CRIT: Dynamic Entity Configs Missing Required Keys
- **Root cause**: Capturer UI entities created dynamically in loops (capture buttons, point X/Y numbers, save button, DFRobot mid/offset numbers) were using bare dictionaries instead of passing through ESPHome entity schemas
- ESPHome 2026.4.x `entity_helpers.py` requires `disabled_by_default` key in entity configs
- Dynamic configs lacked this key, causing: `KeyError: 'disabled_by_default'`
- **Fix**: Added `CONF_DISABLED_BY_DEFAULT: False` to all 6 dynamically created entity configs in `setup_capturer_ui()`
- Entities created via YAML schemas (`button.button_schema()`, etc.) already have defaults from schema validation

---

## [0.7.2] - 2026-09-13

### Fixed — Codegen Double Registration Bug

#### CRIT: Component ID Double Registration (ValueError)
- **Root cause**: ESPHome helper functions (`button.register_button()`, `number.register_number()`, `select.register_select()`, `binary_sensor.register_binary_sensor()`, `switch.register_switch()`, `switch.new_switch()`) internally call `cg.register_component()` when the C++ class inherits from `Component`
- The Python codegen was calling `await cg.register_component()` explicitly BEFORE these helpers, causing double registration
- This triggered `ValueError: Component ID xxx was not declared to inherit from Component, or was registered twice`
- Affected all Capturer UI buttons (capture, save, add/remove, commit/discard), numbers (point X/Y, mid/offset, point count), selects (algorithm), and binary sensors (draft_pending, diagnostic flags, gate_blocked, campaign_running, cal_invalid)
- **Fix**: Removed redundant `cg.register_component()` calls before all `register_*` helper calls
- Pattern now matches ESPHome core components (single registration via helpers)

---

## [0.7.1] - 2026-09-13

### Fixed — Code Review Issues (PR #9)

#### CRIT-01: Gate Condition Codegen
- Replaced `cg.StructInitializer` with explicit setter methods
- Added `add_binary_sensor_condition()`, `add_switch_condition()`, `add_sensor_threshold_condition()` to `MeasurementGate`
- Python codegen now generates correct C++ method calls

#### CRIT-02: Campaign Sampling Stale/NaN Values  
- Campaign sampling now uses `get_last_valid_value()` instead of raw `channel->state`
- NaN values are detected and skipped with a warning log
- Prevents publishing invalid campaign results

#### CRIT-03: Unimplemented Calibration Algorithms
- Updated README to clearly mark `exponential`, `logarithmic`, `power` as **NOT IMPLEMENTED**
- Added warning in Python codegen when these types are used
- Updated CDC to reflect implementation status
- Example YAML updated with clearer documentation

#### HIGH-01: Campaign Channel Validation
- Added channel existence check in `MeasurementCampaign::setup()`
- Logs warning for each missing channel
- Logs error if ALL channels are missing

#### HIGH-05: Invalid Dallas Addresses in Examples
- Fixed `0x70010000000000xx` and `0x44010000000000xx` placeholders
- Replaced with valid hex addresses (ending in `28` for DS18B20)
- Added clear comments indicating placeholders need replacement

### Changed
- CDC version updated to 0.7.1
- `PoolStationChannelSensor` now exposes `get_last_valid_value()` and `has_valid_value()` methods

---

## [0.7.0] - 2026-09-13

### Added — Lot 7: HA Polish + Migration Docs

#### Runtime Algorithm Selection (`CalibrationAlgorithmSelect`)
- New `algorithm_select:` config in `capturer:` block
- Select entity to change calibration type from Home Assistant
- Options: `none`, `linear`, `polynomial`, `piecewise`, `dfrobot_orp`
- Changing algorithm triggers automatic revalidation of points
- `cal_invalid` sensor updates based on new algorithm requirements

#### Add/Remove Calibration Points
- New `CalibrationAddPointButton`: Adds a calibration point with default values
- New `CalibrationRemovePointButton`: Removes the last calibration point
- New `CalibrationPointCountNumber`: Set exact point count (1-10)
- Points within Capturer limits (1-10 maximum)

#### Draft/Commit Calibration Workflow
- New `draft_mode:` config in `capturer:` block (default: false)
- When enabled, Capturer edits go to a draft set
- Main sensor publishing continues using live/committed set
- New `CalibrationCommitButton`: Apply draft to live and save to flash
- New `CalibrationDiscardButton`: Revert draft to live values
- New `DraftPendingSensor`: Binary sensor showing uncommitted draft changes
- `CalibrationEngine` enhanced with `enable_draft_mode()`, `commit_draft()`, `discard_draft()`

#### Migration Documentation (`docs/MIGRATION.md`)
- From j5_ha_bridge options → pool_station YAML
- From lambda-heavy pool-firmata-wifi → pool_station native
- Entity ID stability notes
- SENSOR-IDENTITY checklist before OTA

#### Unit Tests (`tests/test_calibration_filter.py`)
- Python tests for calibration algorithms (linear, piecewise, dfrobot_orp)
- Tests for filter functions (median, mean, clamp, jump guard)
- Tests for temperature compensation (Nernstian pH, linear ORP)
- Tests for draft/commit workflow logic

#### README Polish
- Full feature matrix for Lots 0-7
- Version pinning guidance (`@main` vs `@tag`)
- Lot 7 feature documentation
- Updated roadmap with Lot 8 future scope

### Changed
- `CalibrationEngine`:
  - Separate `live_points_` and `draft_points_` vectors
  - `set_type()` vs `set_type_runtime()` for validation callback
  - `is_valid()` checks live points, `is_draft_valid()` checks draft
  - `calibrate()` always uses live points
- `__init__.py`:
  - Added `select` to AUTO_LOAD
  - New capturer schema options for Lot 7 components
  - `CONF_DRAFT_MODE`, `CONF_ALGORITHM_SELECT`, etc.
- `pool_station.h`:
  - Forward declarations for new UI classes
  - `set_draft_mode_enabled()` on PoolStationChannelSensor

### Technical Notes
- Draft mode is **opt-in** for backward compatibility
- Legacy behavior (direct edits) preserved when `draft_mode: false`
- All new UI components only created if declared in YAML
- Validation callback notifies on algorithm/point changes
- Draft changes don't affect published sensor values until commit

---

## [0.6.0] - 2026-09-13

### Added — Lot 6: Gates & Measurement Campaigns

#### Measurement Gate Module (`measurement_gate.h/cpp`)
- New `GateCondition` struct supporting three condition types:
  - `GATE_COND_BINARY_SENSOR` — Check binary_sensor state
  - `GATE_COND_SWITCH` — Check switch state
  - `GATE_COND_SENSOR_THRESHOLD` — Sensor value comparison (>, >=, <, <=, ==)
- New `GateConfig` struct for per-channel gate configuration
- New `MeasurementGate` class orchestrating condition evaluation
- Pure functions in `gate_functions` namespace (unit-testable)
- `GateBlockedBinarySensor` class exposes gate status to Home Assistant

#### Per-Channel Gate Configuration
- `gate:` block under each channel
- `conditions:` list of gate conditions (AND logic)
- Each condition supports:
  - `binary_sensor_id:` with `state: true/false`
  - `switch_id:` with `state: true/false`
  - `sensor_id:` with `operator:` (>, >=, <, <=, ==) and `threshold:`
- `gate_blocked:` optional binary_sensor entity
- `sample_when:` shorthand for simple single-condition gates
- Calibration mode bypasses gates for testing

#### Measurement Campaign Module (`measurement_campaign.h/cpp`)
- New `CampaignAction` struct for switch state changes
- New `CampaignSample` struct for captured measurements
- New `CampaignResult` struct for campaign outcomes
- New `CampaignConfig` struct for campaign configuration
- New `MeasurementCampaign` class implementing state machine

#### Campaign State Machine
- **IDLE**: Not running, ready to start
- **PREPARING**: Executing prepare actions (switch on/off)
- **WAITING**: Waiting for delay before sampling
- **SAMPLING**: Taking burst samples on configured channels
- **RESTORING**: Restoring actuators to prior state
- State transitions logged at INFO level with clear markers

#### Campaign Configuration
- `campaigns:` list at pool_station level
- `name:` campaign identifier
- `prepare:` list of switch state changes
- `delay:` wait time before sampling (e.g., 60s)
- `sample_channels:` list of channels to sample (pressure, ph, orp)
- `burst_samples:` number of samples per channel
- `burst_delay:` delay between burst samples
- `restore:` restore actuators to prior state
- `timeout:` maximum campaign duration (safety abort)
- `conditions_tag:` optional A/B tag for results

#### Campaign UI Entities
- `CampaignStartButton` — Triggers campaign start
- `CampaignAbortButton` — Aborts running campaign
- `CampaignRunningSensor` — Binary sensor showing campaign state
- `CampaignResultSensor` — Sensor capturing last campaign value per channel

#### Safety Features
- Configurable timeout with automatic abort + restore
- Refuses to start if campaign already running
- Actuators restored even on abort/timeout
- Optional safety gate that must be clear to start (placeholder)

### Changed
- `PoolStationComponent` now tracks registered campaigns
- `PoolStationChannelSensor` now integrates `MeasurementGate`
- Gate check added to `on_source_value_()` pipeline
- Pipeline: gate_check → raw → median → calibrate → temp_comp → guards → publish
- Python codegen (`__init__.py`) extended with gate and campaign schemas
- Example YAML demonstrates full Lot 6 configuration

### Technical Notes
- Gates use fail-closed semantics (no state = condition not met)
- Gates are bypassed in calibration mode for testing
- Campaign state transitions logged with channel name context
- Campaign results include all burst samples for later analysis
- Switches NOT hardcoded — reference by id from YAML
- No breaking changes to Lots 2-5

---

## [0.5.0] - 2026-09-13

### Added — Lot 5: Water Temperature Reference + Compensation

#### Temperature Compensation Module (`temperature_compensation.h/cpp`)
- New `TemperatureCompensationConfig` struct for per-channel configuration
- New `TemperatureCompensator` class wrapping compensation logic
- Pure functions in `temp_comp_functions` namespace (unit-testable):
  - `compute_slope_ratio()` — Nernst slope ratio calculation
  - `compensate_ph()` — pH compensation using Nernstian model
  - `compensate_orp()` — ORP compensation using linear model

#### pH Temperature Compensation (Nernstian Model)
- **Formula**: `pH_comp = neutral_ph + (pH_meas - neutral_ph) × slope_ratio`
- **slope_ratio**: `(T_ref + 273.15) / (T_measured + 273.15)`
- **reference_temperature**: Standard calibration temp (default 25°C)
- **neutral_ph**: Isopotential point where T has no effect (default 7.0)
- Documented equation in code, README, and example YAML

#### ORP Temperature Compensation (Optional)
- **Formula**: `ORP_comp = ORP_meas - orp_coefficient × (T_meas - T_ref)`
- **orp_coefficient**: Linear coefficient in mV/°C (default 0 = disabled)
- Disabled by default — ORP compensation varies by electrode

#### Per-Channel Configuration
- `temperature_compensation:` block under each channel
- `enabled`: Static enable/disable
- `reference_temperature`: Reference temp for compensation
- `neutral_ph`: Isopotential point (pH only)
- `orp_coefficient`: Linear coefficient (ORP only)
- `enable_switch:` — Optional runtime switch entity for HA control

#### Runtime Enable/Disable Switch
- New `TempCompensationSwitch` class
- Per-channel switch to toggle compensation from Home Assistant
- Initial state synced with YAML `enabled` setting

#### Calibration Temperature Metadata
- Water temperature (Tw) captured at calibration save time
- Persisted to flash with calibration data (new prefs magic 0xCAL10005)
- Backward compatible with Lot 2 format (0xCAL10002)
- `calibration_temp_sensor:` — Diagnostic sensor showing Tw at last cal
- Logged at save time and on boot

#### Water Temperature Binding
- Top-level `water_temperature: sensor_id` made functional (was stub)
- Reads from bound Dallas DS18B20 sensor
- Required for temperature compensation to work

### Changed
- Pipeline order: raw → median → calibrate → **temp_comp** → clamp → jump → publish
- `PoolStationChannelSensor` integrates `TemperatureCompensator`
- `CalibrationEngine` stores/loads calibration temperature
- `CalibrationSaveButton` captures Tw at save time
- `dump_config()` outputs temperature compensation settings
- Example YAML demonstrates full Lot 5 configuration with fixed Dallas address
- README documents Nernstian formula and all configuration options

### Technical Notes
- Compensation applied after calibration, before guards
- At neutral_ph (7.0), compensation has zero effect (isopotential)
- Nernst constants derived from R=8.314 J/(mol·K), F=96485 C/mol
- ORP compensation is simple linear model — no standard formula exists
- Dallas sensors MUST use fixed ROM address per SENSOR-IDENTITY.md

---

## [0.4.0] - 2026-09-13

### Added — Lot 4: Diagnostics (noise / drift / flags)

#### Diagnostics Module (`channel_diagnostics.h/cpp`)
- New `DiagnosticsConfig` struct for configurable thresholds
- New `DiagnosticsStats` struct for computed statistics (mean, σ, ptp)
- New `DiagnosticsFlags` struct for boolean indicators (noisy, stuck, out_of_range)
- New `DiagnosticsWindow` class for sliding window statistics
- New `StuckDetector` class for detecting sensor stuck conditions
- New `ChannelDiagnostics` class coordinating all diagnostics per channel
- Pure functions in `diagnostic_functions` namespace (unit-testable)

#### Diagnostic Configuration
- `noise_window`: Sliding window size for statistics (2-50 samples)
- `noise_warn_ptp`: Peak-to-peak threshold for noisy flag (0 = off)
- `noise_warn_sigma`: Standard deviation threshold for noisy flag (0 = off)
- `stuck_timeout`: Duration without change to trigger stuck flag
- `stuck_threshold`: Minimum change to consider "not stuck"
- `range_min` / `range_max`: Out-of-range detection bounds
- `log_rate_limit`: Rate limiting for diagnostic log messages

#### Opt-In Diagnostic Sensors
- `mean_sensor`: Mean value over sliding window
- `sigma_sensor`: Standard deviation (noise indicator)
- `ptp_sensor`: Peak-to-peak (max - min) over window
- Sensors only created if declared in YAML (avoids entity explosion)

#### Opt-In Diagnostic Flags (binary_sensors)
- `noisy`: True when ptp or σ exceeds configured thresholds
- `stuck`: True when no meaningful change for stuck_timeout
- `out_of_range`: True when value outside range_min/max
- Flags only created if declared in YAML

#### Structured Logging
- Rate-limited warnings with tag `pool_station` on abnormal events
- Suppressed in calibration mode to avoid log spam
- Logs include channel name, flag type, and relevant values

#### Integration Stubs (Lot 6 preparation)
- Placeholder flags: `cal_invalid`, `gate_blocked`, `campaign_running`
- Ready for integration with future Gates/Campaigns lot

### Changed
- `PoolStationChannelSensor` now integrates `ChannelDiagnostics`
- Pipeline extended: ... → jump guard → diagnostics → publish
- `dump_config()` outputs diagnostics configuration
- Python codegen (`__init__.py`) extended with `diagnostics:` schema
- Example YAML demonstrates full Lot 4 diagnostics configuration

### Technical Notes
- Diagnostics process on final guarded value (post-filters)
- Standard deviation uses Welford's online algorithm
- Jump magnitude and streak tracked (reuses Lot 3 JumpGuard state pattern)
- Calibration mode: diagnostics run but logs are suppressed
- Pressure-friendly: can enable diagnostics without heavy filters

---

## [0.3.0] - 2026-09-13

### Added — Lot 3: Filters & Companion Publication

#### Per-Channel Filters (`channel_filter.h/cpp`)
- New `ChannelFilterConfig` struct for filter settings
- New `SlidingWindow` class for N-sample median/mean filtering
- New `JumpGuard` class for outlier rejection with plateau detection
- Pure functions in `filter_functions` namespace (unit-testable)
- `compute_median()`, `compute_mean()`, `clamp_value()`, `is_jump_exceeded()`

#### Filter Configuration
- `filter_samples`: Median window size (0 or 1 = off, up to 20)
- `max_jump`: Maximum allowed change between readings (0 = disabled)
- `max_jump_streak`: Accept new plateau after N consecutive similar jumps
- `value_min` / `value_max`: Clamp calibrated values to valid range

#### Companion Entities
- `calibrated_sensor`: Diagnostic sensor showing calibrated value BEFORE guards
- Main channel entity now publishes fully guarded value
- Pipeline: raw → median → raw_sensor → calibrate → calibrated_sensor → clamp → jump → publish

#### Calibration Mode Filter Bypass
- When Calibration Mode is ON, median window and jump guard are bypassed
- Ensures Capturer UI shows immediate ~1s raw response during calibration
- Jump guard baseline resets when toggling calibration mode

### Changed
- `PoolStationChannelSensor` now includes filter state (SlidingWindow, JumpGuard)
- `on_source_value_()` implements full signal processing pipeline
- `dump_config()` outputs filter configuration
- Python codegen (`__init__.py`) extended with `filters:` and `calibrated_sensor:` schemas

### Technical Notes
- Filters default to OFF (`filter_samples: 0`, `max_jump: 0.0`)
- Pressure channels intentionally keep filters OFF for fast response
- pH/ORP examples demonstrate mild median (3-5 samples) + jump guards
- Median uses `std::nth_element` for O(n) performance
- Jump guard tracks pending values for plateau detection

---

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
| 2 | 0.2.0 | ✅ N-point calibration, Capturer UI, persistence | Done |
| 3 | 0.3.0 | ✅ Filters (j5-like median, jump, clamp) | Done |
| 4 | 0.4.0 | ✅ Diagnostics (noise σ/ptp, flags) | Done |
| 5 | 0.5.0 | ✅ Temperature compensation (Tw) | Done |
| 6 | 0.6.0 | ✅ Gates/campaigns | Done |
| **7** | **0.7.13** | ✅ **HA polish, runtime algo select, draft/commit** | **Current** |
| 8 | — | (Optional) Interference detection, EZO | Future |

## Future Lots (Lot 8 candidates)

The following features are documented as potential future work:

- **Interference/correlation detection**: Cross-channel chemistry analysis
- **EZO (I2C Atlas Scientific) support**: Native driver for Atlas sensors
