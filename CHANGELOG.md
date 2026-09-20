## [0.10.1] - 2026-09-20

### Fixed — `ads:` overlay rejected native `platform: ads1115` sources

Lot A validate used `cv.use_id(sensor.Sensor)`, so `source_id` was typed as
`Sensor` and `inherits_from(ADS1115Sensor)` returned False. The helper then
returned without checking the YAML platform — every compose-model `ads:`
block failed on ESPHome 2026.4.3 (Eric OTA unblock).

- Fall through from `inherits_from` (False or exception) to type-name /
  `CORE.config` `sensor:` `platform: ads1115` lookup
- `FINAL_VALIDATE_SCHEMA` walks the full config (do not rely on the ID
  declared type being `ADS1115Sensor`)
- Still rejects dallas / wifi_signal / other non-ADS sources with `ads:`

---

## [0.10.0] - 2026-09-20

### Added — Lot B: runtime Lot 3 filters + channel interval

HA-tunable overlay on existing Lot 3 setters (`set_filter_samples`,
`set_max_jump`, …). Does **not** own the ADS, change clamp→reject, or
move filters into the Capturer draft. Lot C remains NO-GO.

Per channel, opt-in:

- `filters.runtime:` numbers for `filter_samples` (0–20), `max_jump`,
  `max_jump_streak` (1–10), `value_min` / `value_max` (still **clamp**)
- `filters.runtime.reset_yaml_button` restores YAML seeds (RAM + NVS)
- `update_interval_number` (1–3600 s) for the **channel** throttle only
  (cal-mode ~1 s stays prior; ADS `update_interval` is not changed)
- `apply_*_runtime` resize the median window (`set_size` clears) and
  reset `JumpGuard` — codegen `set_filter_*` stay config-only
- Same sidecar NVS `ps-md5-rt-v1` / magic `0xA0511115` as Lot A.
  Unmanaged sentinels (`0xFF` / `NAN` / interval `0`) keep YAML so a
  Lot A-only blob does not wipe filters.
  **Calibration key `ps-md5-v1` and magic `0xCA110005` are unchanged**
- YAML seeds; NVS wins after the first HA apply / reboot
- Filters are **not** locked in Calibration Mode (median/jump already
  bypass). Changing a number does **not** dirty Capturer draft / Save
- Lot 8 still samples **pre-`max_jump`** (`compensated`)

Without `filters.runtime` / `update_interval_number`: existing YAML is
unchanged (zero new entities). `filters:` seeds still apply as Lot 3.

### Fixed — Lot B review (same PR, no version bump)

- Save cal / `apply_gain_runtime` no longer stamp unmanaged filter or
  interval fields (design §4.2: Save does not write filters)
- Sidecar `alignas(4)` so Lot B float fields cannot fault on ESP32-C3/C6
  (32-byte Lot A layout unchanged)
- `HAS_VMIN` / `HAS_VMAX` require a finite value (NAN does not wipe YAML)
- Inverted `value_min` > `value_max` apply is refused
- `reset_yaml` restores a YAML `update_interval` below 1 s (HA still 1–3600)
- Median `set_size` / JumpGuard reset skipped when the size or jump is unchanged
- `SlidingWindow::is_full()` is false when size is 0
- `filters.runtime.value_min/max` require the YAML clamp seed
- Non-finite HA number values are ignored
- Lot B numbers default to `mode: box`

See [MIGRATION.md](docs/MIGRATION.md#runtime-filters-lot-b-v0100) and
[DESIGN-RUNTIME-ADS-AND-FILTERS.md](docs/DESIGN-RUNTIME-ADS-AND-FILTERS.md).

---

## [0.9.0] - 2026-09-20

### Added — Lot A: runtime ADS1115 gain + saturation

Opt-in overlay on the existing compose model (`source_id` → native
`platform: ads1115`). Does **not** rewrite the ADC driver, expose mux, or
own scheduling (Lot C remains NO-GO). Lot B dynamic filters are not in
this release (see 0.10.0).

Per channel with `ads:`:

- HA `select` for PGA (`6.144`, `4.096`, `2.048`, `1.024`, `0.512`, `0.256`)
- Typed overlay: `static_cast<ADS1115Sensor*>` after config validation
  (non-ADS `source_id` + `ads:` is a YAML error, not a silent cast)
- Shadow gain (upstream has setters, no getters)
- `binary_sensor` saturation: ON at ≥ 98 % of current FSR, OFF at ≤ 95 %,
  `hold_time` 15 s (uses **pre-median** `last_raw_value_`)
- Optional `% FSR` diagnostic and `gain_mismatch` (current ≠ gain at last
  successful calibration Save; `0xFF` / never saved → OFF)
- Soft-lock: gain select / reset YAML refused while Calibration Mode is ON
- Save / Commit refused if the current raw is saturated (rail is not a
  measurement). Capture logs a warning, no hard-block (MVP)
- Sidecar NVS `ps-md5-rt-v1` / magic `0xA0511115` — **calibration key
  `ps-md5-v1` and magic `0xCA110005` are unchanged**
- YAML `ads.gain` is the seed; NVS wins after the first HA apply / reboot
- Changing gain does **not** rescale calibration X/Y (cal is in volts)

Without `ads:`: existing YAML is unchanged (zero new entities).

### Fixed — Lot A review (same PR, no version bump)

- Overlay enable requires a successful ADS bind (`USE_ADS1115` + non-null `ads_`)
- Sidecar packed struct: 2-byte pad before floats, 32-byte / 4-aligned
  (no production NVS yet)
- Auto-save NVS on every persist apply (not only when gain changes)
- Saturation hysteresis keeps the latch on NaN raw / invalid FSR
- Hold expiry uses unsigned wrap-safe elapsed
- `apply_gain_runtime(persist)` also refuses in Calibration Mode
- Validator prefers `inherits_from(ADS1115Sensor)` when importable
- `static_assert` gain codes vs `ads1115::ADS1115Gain`
- Save / Commit log uses the channel `sat_on` (not hardcoded 98 %)

See [MIGRATION.md](docs/MIGRATION.md#runtime-ads-gain-lot-a-v090) and
[DESIGN-RUNTIME-ADS-AND-FILTERS.md](docs/DESIGN-RUNTIME-ADS-AND-FILTERS.md).

---

## [0.8.1] - 2026-09-15

### Fixed — Restore Capturer draft UX entities after v0.8.0

After the Lot 8 / v0.8.0 firmware rebuild (commit `6e6b4da`), Home Assistant
pool-io lost calibration Save/Discard widgets even though C++ still had
draft_mode:

- `number.*_points_actifs` (`point_count_number`)
- `binary_sensor.*_modifications_en_attente` (`draft_pending`)
- `button.*` Annuler modifications (`discard_button`)
- calibration point slots 4–5 (only 1–3 remained; schema default was 3)

**Cause**: those widgets were YAML-opt-in only. `setup_capturer_ui` created
them solely when the keys were present. `point_count` defaulted to 3 HA
slots. `point_count_number` also registered *before* `set_parent`, and
`draft_pending` / `draft_invalid` were not `register_component`'d so
`loop()` never ran on ESPHome 2026.8 (`register_binary_sensor` does not
register Component).

**Restore** (no UX redesign):

- `draft_mode: true` always codegens `point_count_number`, `discard_button`,
  and `draft_pending` (auto-created if the YAML keys are omitted; declared
  `name:` still wins — keep French names for stable HA entity IDs)
- `point_count` default is **5** (max preallocated HA slots)
- Parent/channel setters run before `number.register_number` for the count
- `register_component` restored for Discard / draft_pending / draft_invalid
  (Lot 7 original path)

YAML keys to keep in `pool-station.yaml` for stable entity IDs: `point_count`,
`point_count_number.name`, `discard_button.name`, `draft_pending.name`,
`draft_mode`, `save_button`, plus optional `draft_invalid.name`.

---

## [0.8.0] - 2026-09-15

### Added — Lot 8 interference detection

Station-level **suspected interference** flags (not a chemistry oracle, not Atlas EZO):

- **Coincident jump** ORP ↔ pressure: both jumped within `jump_window` (last-jump latch so a pump start then ORP spike still pairs).
- **Shared noise** pH ↔ ORP: both rolling σ at or above threshold (own window, Lot 4 optional).
- **Tw coupling** (opt-in): pH and water temperature move the same way over `tw_window`.
- Calibration mode: no `push`, `reset()` on enter **and** leave (Capturer buffers must not look like EMI).
- Samples are **pre-jump-guard** (compensated), so Lot 3 `max_jump` does not hide pump spikes.
- `hold_time` keeps HA `device_class: problem` visible after a one-shot event.
- YAML `pool_station.interference` is opt-in.

### Fixed — review follow-ups (not yet released)

- Default `jump_window` **90s** (covers ORP `update_interval: 60s` vs pressure 2s). A 2s window never pairs those channels.
- Default `tw_window` **180s** so two pH samples at 60s exist in the delta window.
- `time_delta_ms` / `later_ms` are millis wrap-safe (`dt < 2^31`).
- `dump_config` warns if coincident_jump / shared_noise miss a required channel.

---

## [0.7.18] - 2026-09-15

### Fixed — Draft/Save review follow-ups

- Published pipeline gated on `get_type()` (draft). Selecting algorithm
  `none` without Save published raw volts / ORP `V*1000` instead of the
  last committed calibration. Gate now uses `get_live_type()`.
- **Save / Commit refuse an invalid draft** (`is_draft_valid()`). Live +
  flash stay on the last good set; `draft_pending` remains ON.
- **`draft_invalid` binary sensor** is ON while the draft cannot be Saved
  (HA-visible; independent of live `cal_invalid`).
- `get_type_name()` / `get_minimum_points()` report the **live** algorithm
  (same as `calibrate()` / `is_valid()`).
- Example ORP Capturer now uses `draft_mode` + Discard + `draft_pending`
  + `draft_invalid`.

---

## [0.7.17] - 2026-09-14

### Fixed — Coherent Capturer Save / dirty-state UX

With `draft_mode: false` (legacy), Point Count / Add / Remove / Capture mutate
`live_points_` immediately. A HA page refresh does **not** reload the ESP, so
there is no undo. `CalibrationSaveButton` only flushed already-live RAM to flash.

Lot 7 already had draft/commit/discard, but:
- `draft_mode` defaulted to false
- Save did not `commit_draft()` (a separate Commit button was required)
- Runtime algorithm / DFRobot mid-offset edits applied to published readings
- Prefs load after codegen `enable_draft_mode()` left draft on YAML seeds

### Changed
- **`draft_mode: true` is the supported path** for multi-point Capturer channels
- Point / count / capture / X/Y / algorithm / DFRobot edits go to **draft only**
- `calibrate()` / `is_valid()` keep the **last committed** type + `live_points_`
- **Save** = `commit_draft()` (draft → live + `save_to_preferences()` + clear dirty)
- **Discard / Annuler** = revert draft to live, clear dirty
- `draft_pending` is `ON` while the draft is dirty (dashboard can emphasize Save)
- Prefs load resyncs draft from live so browser refresh is **not** the undo
- `is_draft_valid()` remains available for UI feedback
- Legacy `draft_mode: false` Save behavior is unchanged
- Optional `commit_button` still works; prefer a single **Save** (do not require Commit+Save)

### Migration
Enable `draft_mode: true` + `draft_pending` + `discard_button`. Keep `save_button`.
See [MIGRATION.md](docs/MIGRATION.md#capturer-draft-save-v0717).

---

## [0.7.16] - 2026-09-13

### Fixed — Lot-2 review-fix ports (clean re-implementation on v0.7.15)

Selective ports of remaining Lot-2 review intents onto current `main`.
Does **not** apply the old Lot-2 WIP tree. Capturer sync from 0.7.14/0.7.15
and the v0.7.15 number `register_component` fix are preserved.

#### Stable NVS preferences key (operational)
- **Problem**: `generate_preferences_key` used `hash((base_key, channel_type)) & 0xFFFFFFFF`.
  Python's `hash()` is randomized per process (`PYTHONHASHSEED`), so a recompile
  could orphan flash-saved calibration.
- **Fix**: deterministic MD5 (`ps-md5-v1`) in `prefs_key.py`. Same component id +
  channel type always yields the same uint32 key.
- **Migration**: old PYTHONHASHSEED keys cannot be reconstructed. After flashing
  0.7.16, previously persisted calibration will not load — **one-time recapture
  and Save** per channel. If the MD5 payload scheme is ever bumped, document
  another one-time recal the same way.

#### HA slot-stable calibration points
- Stopped in-place `std::sort` / compact on `live_points_` / draft slots
  (`add_point`, `set_point`, `set_seed_points`, prefs load).
- Piecewise (and other math) sorts a **working copy** of valid points only.
- YAML / Capturer HA entity indices stay aligned after capture, edit, and reboot.

#### `precision` applied to calibrated output
- YAML `calibration.precision` was stored (`precision_decimals_`) but never
  applied. `calibrate()` now rounds the result with `apply_precision_`.

#### Honest `is_implemented()` for stub algorithms
- `exponential` / `logarithmic` / `power` remain stubs (select still lists only
  implemented algos). `is_implemented()` is false for those types, so
  `is_valid()` / `cal_invalid` reflect reality.

#### Nested `calibration_mode` parent bind
- Nested `__init__.py` path now calls `set_parent` on the calibration mode
  switch (standalone `switch.py` already did). Toggling the nested switch
  actually enables calibration-mode sampling.

### Changed
- Linear calibration uses least-squares on all valid points (N==2 is still the
  two-point line; N>2 is a better fit than first/last).
- Root `.gitignore` now ignores `build/` and `.esphome/`.

---

## [0.7.15] - 2026-09-13

### Fixed — Capturer Number Codegen Double-Registers Component

#### CRIT: `esphome config` ValueError after v0.7.14
- **Error**: `ValueError: Component ID pressure_point0_x was not declared to inherit from Component, or was registered twice.` at `setup_capturer_ui` → `cg.register_component(num_x_var, num_x_conf)`
- **Root cause**: v0.7.14 called `cg.register_component` on capturer point X/Y (and `point_count_number`). ESPHome 2026.4 `number.register_number` already registers the Component
- **Fix**: drop the redundant `cg.register_component` on number entities. Keep parent/index setters before `register_number` so `setup()` still runs. Same cleanup on `number.py` helpers
- **Preserved**: C++ `notify_calibration_updated()` / `publish_captured_point_x()` so Capture still writes volts onto the HA X number
- Algorithm select still uses `register_component` + `register_select` (needed since v0.7.12; `register_select` does not double-register)

---

## [0.7.14] - 2026-09-13

### Fixed — Add/Remove Calibration Point Has No HA UI Effect

#### HIGH: `pression_add_point` / `pression_remove_point` change the engine but HA numbers stay stale
- **Symptoms**: ESP log shows `Added new calibration point for channel 0 (total: 4)` but Home Assistant still has 3 `number.*_pressure_cal_point_*` entities; values often stay `unknown`; no `point_count` entity
- **Root cause 1**: `PoolStationChannelSensor::notify_calibration_updated()` was a stub (log only). Add/Remove/Save/capture/algo/commit/discard never refreshed registered `CalibrationPointX/YNumber`, `CalibrationAlgorithmSelect`, `CalibrationPointCountNumber`, `cal_invalid`, or `draft_pending`
- **Root cause 2**: `capturer.point_count` is the **max preallocated HA slots** codegen'd at compile time. Deploy YAML with `point_count: 3` cannot show a 4th point. `point_count_number` (live engine count) was not registered on the parent and is optional in schema
- **Also confirmed**: `pressure_capture_point_1` logs `Captured raw=0.4600V into point 0` but `number.*_pressure_cal_point_1_x` stays `unknown` while the live volt sensor shows ~0.45 V
- **Fix**:
  - `notify_calibration_updated()` calls `PoolStationComponent::refresh_calibration_ui()` which republishes all registered widgets for that channel
  - Codegen registers `algorithm_select`, `point_count_number`, and `draft_pending` on the parent (point X/Y were already registered)
  - Capturer point X/Y numbers now `register_component` (same 0.7.12 gap as algorithm select) so `setup()` / `publish_state` reach HA
  - `update_from_calibration()` publishes X (or Y) whenever that axis is finite — capture writes X first; requiring both axes valid left HA at `unknown`
  - Capture path: after notify, `publish_captured_point_x()` writes the captured volts onto that slot's X number so Étalonnage updates immediately
  - Channel `setup()` refreshes UI after `load_from_preferences()` so numbers are not stuck `unknown` when they setup before prefs load
  - Save also notifies so HA stays in sync
- **Docs**: `point_count` = max HA slots (recommend 5–10 with add/remove); `point_count_number` = live engine count. Example YAML uses `point_count: 5` plus `point_count_number`
- Based on main after PR #22 (v0.7.13 per-channel algorithm select options)

---

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
| **7** | **0.7.18** | ✅ HA polish, runtime algo select, draft/Save dirty UX, Capturer UI sync, Lot-2 review-fix ports | Done |
| **8** | **0.8.1** | ✅ Interference detection + restore Capturer draft UX entities | Done |
| **A** | **0.9.0** | ✅ **Runtime ADS gain + saturation (compose overlay)** | **Current** |
| **B** | — | Dynamic Lot 3 filters + channel interval | Later |
| **C** | — | ADS ownership / scheduler | NO-GO |

## Out of scope

The following remain **out of this component**:

- **EZO (I2C Atlas Scientific)**: native Atlas driver
- **Zelia/Zodiac protocols**: proprietary closed protocols
