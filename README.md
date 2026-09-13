# ESPHome Pool Station

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESPHome](https://img.shields.io/badge/ESPHome-external_component-blue)](https://esphome.io/components/external_components.html)

> **Rich calibration and diagnostics platform for pool monitoring**

**pool_station** is an ESPHome external component designed to transform an ESP32 into a comprehensive pool monitoring station. This is **NOT** a simple DFRobot driver or vendor wrapper — it's a sophisticated calibration, diagnostics, and intelligent sampling platform.

## Features

- **Rich N-point calibration** with multiple algorithms: linear, polynomial, piecewise, dfrobot_orp
- **Capturer UI**: Calibration workflow directly from Home Assistant
- **Draft/Commit workflow**: Preview changes before applying (Lot 7)
- **Runtime algorithm selection**: Change calibration type without reflashing (Lot 7)
- **Calibration persistence**: Survives reboots via ESPHome preferences
- **Real-time diagnostics**: noise detection, stuck detection, out-of-range flags
- **Water temperature compensation**: Nernstian model for pH, linear model for ORP
- **Compose ESPHome sensors**: uses native `ads1115`/`dallas`/`gpio` — does NOT reimplement drivers
- **Stable entity IDs**: anti-swap protection by physical address (see [SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md))
- **Brand agnostic**: works with any analog pH/ORP probe

Designed to replace complex YAML lambdas in [pool-firmata-wifi](https://github.com/echavet/pool-firmata-wifi).

## Feature Matrix

| Feature | Lot | Status | Description |
|---------|-----|--------|-------------|
| **Skeleton & Docs** | 0 | ✅ Done | Component structure, CDC, SENSOR-IDENTITY |
| **ADS1115 Binding** | 1 | ✅ Done | Source sensors, raw values, calibration mode switch |
| **N-Point Calibration** | 2 | ✅ Done | Multiple algorithms, Capturer UI, persistence |
| **Filters** | 3 | ✅ Done | Median, jump guard, clamp, calibration bypass |
| **Diagnostics** | 4 | ✅ Done | Noise/stuck/range flags, stats sensors, logging |
| **Temperature Compensation** | 5 | ✅ Done | Nernstian pH model, optional ORP linear model |
| **Gates/Campaigns** | 6 | ✅ Done | Conditional sampling, measurement campaigns |
| **HA Polish** | 7 | ✅ Done | Runtime algo select, add/remove points, draft/commit |
| **Interference Detection** | 8 | 🔮 Future | Cross-channel correlation (optional) |

## Current Status — Lot 7

**Lot 7** — HA Polish + Migration Docs:
- ✅ **Runtime algorithm select**: Change calibration type from HA without reflash
- ✅ **Add/remove calibration points**: Dynamic point management from UI
- ✅ **Point count number**: Live engine count + optional resize (1-10)
- ✅ **Capturer UI sync**: Add/Remove/Save/capture/algo refresh HA numbers (v0.7.15)
- ✅ **Stable NVS keys + slot-stable points**: MD5 prefs key, HA indices not reshuffled (v0.7.16)
- ✅ **Draft/Commit workflow**: Edit draft, preview, then commit or discard
- ✅ **Draft pending sensor**: Shows when uncommitted changes exist
- ✅ **Migration documentation**: From j5_ha_bridge and pool-firmata-wifi
- ✅ **Unit tests**: Python tests for calibration and filter algorithms

**Previous Lots**:
- **Lot 6**: Gates and measurement campaigns
- **Lot 5**: Water temperature compensation (Nernstian pH, linear ORP)
- **Lot 4**: Diagnostics (noise σ/ptp, stuck, out_of_range flags)
- **Lot 3**: Filters (median window, jump guard, clamp)
- **Lot 2**: N-point calibration, Capturer UI, persistence
- **Lot 1**: ADS1115 binding, raw sensors, calibration mode
- **Lot 0**: Component skeleton, documentation

See [CHANGELOG.md](CHANGELOG.md) for detailed version history.

## Installation

Add this to your ESPHome configuration:

```yaml
external_components:
  - source: github://echavet/esphome-pool-station@main
    components: [pool_station]
    refresh: 1d
```

### Version Pinning

For production stability, pin to a specific tag instead of `@main`:

```yaml
external_components:
  # Pin to a release tag for stability
  - source: github://echavet/esphome-pool-station@v0.7.0
    components: [pool_station]
    refresh: 0s  # No refresh for tagged versions
```

| Reference | Use Case | Stability |
|-----------|----------|-----------|
| `@main` | Development, testing latest features | May break |
| `@v0.7.0` | Production, stable release | Stable |
| `@commit-sha` | Specific commit for debugging | Fixed |

## Quick Start (Lot 2)

```yaml
# I2C bus for ADS1115
i2c:
  sda: GPIO5
  scl: GPIO4

# ADS1115 ADC
ads1115:
  - address: 0x48
    id: ads

# Source sensors (native ESPHome ads1115)
sensor:
  - platform: ads1115
    ads1115_id: ads
    multiplexer: 'A2_GND'
    gain: 4.096
    id: ads_ph
    internal: true
    update_interval: 1s

# Pool Station with N-point calibration
pool_station:
  id: pool
  calibration_interval: 1s
  
  calibration_mode:
    name: "Pool Calibration Mode"

  channels:
    ph:
      source_id: ads_ph
      name: "Pool pH"
      update_interval: 60s
      
      raw_sensor:
        name: "Pool pH Raw"
      
      # Piecewise 3-point calibration (pH 4/7/10)
      calibration:
        type: piecewise
        points:
          - x: 2.03   # Volts at pH 4
            y: 4.01
          - x: 1.50   # Volts at pH 7
            y: 7.00
          - x: 0.98   # Volts at pH 10
            y: 10.00
      
      # Capturer UI for Home Assistant
      capturer:
        point_count: 5          # Max HA slots; add/remove cannot exceed this
        capture_buttons: true
        point_numbers: true
        save_button: true
        point_count_number:
          name: "pH Point Count"
      
      cal_invalid:
        name: "pH Cal Invalid"
```

For a complete example with all channels, see [examples/pool-station-minimal.yaml](examples/pool-station-minimal.yaml).

## Calibration from Home Assistant

### Step-by-Step pH Calibration

1. **Turn ON Calibration Mode**
   - Toggle the "Pool Calibration Mode" switch ON
   - All channels now sample at `calibration_interval` (1s) for responsive readings

2. **Capture Point 1 (pH 4 buffer)**
   - Immerse probe in pH 4.01 buffer solution
   - Wait for stable reading on "Pool pH Raw" sensor
   - Press **"Ph Capture Point 1"** button
   - Set **"Ph Cal Point 1 Y"** number to `4.01`

3. **Capture Point 2 (pH 7 buffer)**
   - Rinse probe, immerse in pH 7.00 buffer
   - Wait for stability
   - Press **"Ph Capture Point 2"** button
   - Set **"Ph Cal Point 2 Y"** to `7.00`

4. **Capture Point 3 (pH 10 buffer)** (optional for piecewise)
   - Rinse probe, immerse in pH 10.00 buffer
   - Press **"Ph Capture Point 3"** button
   - Set **"Ph Cal Point 3 Y"** to `10.00`

5. **Save Calibration**
   - Press **"Ph Save Calibration"** button
   - Calibration is now persisted to flash and survives reboots

6. **Turn OFF Calibration Mode**
   - Return to normal `update_interval` sampling

### DFRobot ORP Calibration (SEN0165-like)

For ORP sensors using the DFRobot algorithm:

1. Set **"Orp DFRobot Mid mV"** to your reference midpoint (typically 2500)
2. Immerse probe in known ORP solution (e.g., 225mV)
3. Note the raw voltage reading
4. Adjust **"Orp DFRobot Offset mV"** until calibrated value matches expected
5. Press **"Orp Save Calibration"**

Formula: `ORP_mV = mid_mv - (raw_voltage × 1000) - offset_mv`

## Calibration Algorithms

| Algorithm | Min Points | Use Case | Parameters | Status |
|-----------|------------|----------|------------|--------|
| `linear` | 2 | Simple linear sensors (pressure) | least-squares on all valid points | ✅ Implemented |
| `piecewise` | 2 | pH sensors, non-linear but monotonic | — | ✅ Implemented |
| `polynomial` | order+1 | Complex curves | `order` (1-5), `precision` | ✅ Implemented |
| `dfrobot_orp` | 0 | DFRobot SEN0165 ORP modules | `mid_mv`, `offset_mv` | ✅ Implemented |
| `exponential` | 2 | — | — | ⚠️ **NOT IMPLEMENTED** (pass-through) |
| `logarithmic` | 2 | — | — | ⚠️ **NOT IMPLEMENTED** (pass-through) |
| `power` | 2 | — | — | ⚠️ **NOT IMPLEMENTED** (pass-through) |

> **Note**: The `exponential`, `logarithmic`, and `power` algorithms are reserved for future implementation. They fall back to pass-through and `is_valid()` is **false** so `cal_invalid` stays honest. Do not use them in production configurations.

## Hardware Reference

| Component | Model | Connection | Notes |
|-----------|-------|------------|-------|
| MCU | DIYables ESP32-S3 UNO | — | I2C: SDA=GPIO5, SCL=GPIO4 |
| ADC | ADS1115 | I2C 0x48 | 4 channels, 16-bit, gain 4.096 |
| Pressure | 0-5V sensor | A0 | Via isolation module |
| pH | Probe + amplifier | A2 | Via isolation module |
| ORP | Probe + amplifier | A3 | Via isolation module |
| Water Tw | DS18B20 `0xc802...` | GPIO17 | Immersed probe |
| Air pump | DS18B20 `0x7001...` | GPIO13 | Near pump |
| Air local | DS18B20 `0x4401...` | GPIO13 | Ambient |

> **IMPORTANT**: Temperature sensors **must** be bound by physical address, not scan order.  
> See [docs/SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md) for anti-swap configuration.

## Configuration Reference

### Pool Station Block

```yaml
pool_station:
  id: pool
  update_interval: 5s           # Main component polling (default: 1s)
  calibration_interval: 1s      # Interval when calibration mode is ON
  
  calibration_mode:             # Optional: calibration mode switch
    name: "Calibration Mode"
  
  channels:
    pressure:
      source_id: ads_pressure
      name: "Filter Pressure"
      # ... (see below)
    ph:
      source_id: ads_ph
      # ...
    orp:
      source_id: ads_orp
      # ...
  
  water_temperature:            # For future Tw compensation (Lot 5)
    sensor_id: water_temp
```

### Channel Configuration

```yaml
channels:
  ph:
    source_id: ads_ph           # Reference to native ADS1115 sensor
    name: "Pool pH"
    unit_of_measurement: "pH"
    accuracy_decimals: 2
    icon: "mdi:ph"
    update_interval: 60s        # Normal mode sampling interval
    
    raw_sensor:                 # Diagnostic raw voltage sensor
      name: "Pool pH Raw"
    
    calibration:                # Calibration configuration
      type: piecewise           # Algorithm: linear, polynomial, piecewise, dfrobot_orp
      order: 2                  # For polynomial only (1-5)
      precision: 2              # Output decimals
      points:                   # Seed calibration points (x=volts, y=calibrated)
        - x: 2.03
          y: 4.01
        - x: 1.50
          y: 7.00
      # DFRobot specific (only for dfrobot_orp type)
      mid_mv: 2500.0
      offset_mv: 0.0
    
    capturer:                   # Capturer UI configuration
      point_count: 5            # Max preallocated HA slots (1-10). Recommend 5–10
                                # if using add/remove. Cannot create entities past this.
      capture_buttons: true     # Create capture buttons per point slot
      point_numbers: true       # Create x/y number entities per slot
      save_button: true         # Create save to flash button
      mid_number: false         # DFRobot mid_mv number entity
      offset_number: false      # DFRobot offset_mv number entity
      point_count_number:       # Live CalibrationEngine count (not the slot max)
        name: "pH Point Count"
    
    cal_invalid:                # Calibration invalid indicator
      name: "pH Cal Invalid"
```

### Filter Configuration (Lot 3)

```yaml
channels:
  ph:
    # ... source, calibration, etc.
    
    # Calibrated sensor (pre-guard diagnostic value)
    calibrated_sensor:
      name: "Pool pH Calibrated"
    
    # Filter configuration
    filters:
      filter_samples: 5         # Median window size (0 or 1 = off)
      max_jump: 0.5             # Max allowed change (0 = off)
      max_jump_streak: 3        # Accept new plateau after N consistent jumps
      value_min: 0.0            # Min clamp value (omit = no min)
      value_max: 14.0           # Max clamp value (omit = no max)
```

### Signal Processing Pipeline

```
raw → [median window] → raw_sensor → calibrate → calibrated_sensor
    → [clamp] → [jump guard] → main entity
```

| Stage | Description | Config |
|-------|-------------|--------|
| **raw** | Original ADC voltage | — |
| **median window** | N-sample median filter | `filter_samples` (0/1 = off) |
| **raw_sensor** | Diagnostic: filtered raw voltage | `raw_sensor:` block |
| **calibrate** | Apply calibration algorithm | `calibration:` block |
| **calibrated_sensor** | Diagnostic: calibrated value pre-guard | `calibrated_sensor:` block |
| **clamp** | Min/max value bounds | `value_min`, `value_max` |
| **jump guard** | Reject absurd jumps | `max_jump`, `max_jump_streak` |
| **main entity** | Final guarded value | Channel name |

> **Calibration Mode**: When ON, filters are bypassed for ~1s raw response during calibration capture.

### Temperature Compensation Configuration (Lot 5)

```yaml
channels:
  ph:
    # ... source, calibration, filters, etc.
    
    temperature_compensation:
      enabled: true                 # Enable compensation (default: false)
      reference_temperature: 25.0   # Standard calibration temp in °C
      neutral_ph: 7.0               # Isopotential point (pH only)
      orp_coefficient: 0.0          # Linear coeff in mV/°C (ORP only, 0=off)
      
      # Optional runtime switch to enable/disable
      enable_switch:
        name: "pH Temp Compensation"
    
    # Shows water temp at last calibration save
    calibration_temp_sensor:
      name: "pH Calibration Tw"
```

### Diagnostics Configuration (Lot 4)

```yaml
channels:
  ph:
    # ... source, calibration, filters, etc.
    
    diagnostics:
      # Noise detection thresholds
      noise_window: 10         # Sliding window size (2-50)
      noise_warn_ptp: 0.3      # Peak-to-peak threshold (0 = off)
      noise_warn_sigma: 0.1    # Stddev threshold (0 = off)
      
      # Stuck detection
      stuck_timeout: 300s      # Duration without change (0 = off)
      stuck_threshold: 0.01    # Minimum change to consider "not stuck"
      
      # Out-of-range detection
      range_min: 0.0           # Min valid value (omit = no check)
      range_max: 14.0          # Max valid value (omit = no check)
      
      # Logging
      log_rate_limit: 60s      # Rate limit for warning logs
      
      # Optional sensors (only created if declared)
      mean_sensor:
        name: "Pool pH Mean"
      sigma_sensor:
        name: "Pool pH Noise (σ)"
      ptp_sensor:
        name: "Pool pH Peak-to-Peak"
      
      # Optional flags (only created if declared)
      noisy:
        name: "Pool pH Noisy"
      stuck:
        name: "Pool pH Stuck"
      out_of_range:
        name: "Pool pH Out Of Range"
```

### Diagnostics Recommendations

| Channel | noise_warn_ptp | noise_warn_sigma | stuck_timeout | Notes |
|---------|---------------|------------------|---------------|-------|
| **Pressure** | — | — | — | Diagnostics optional, fast response needed |
| **pH** | 0.2-0.5 | 0.05-0.15 | 3-10 min | Moderate noise detection |
| **ORP** | 20-50 mV | 5-15 mV | 5-15 min | ORP changes slowly |

### Filter Recommendations

| Channel | filter_samples | max_jump | Rationale |
|---------|---------------|----------|-----------|
| **Pressure** | 0 (off) | 0 (off) | Fast response needed |
| **pH** | 3-5 | 0.3-0.5 | Smooth noise, reject spikes |
| **ORP** | 3-5 | 30-50 mV | Filter electrical interference |

### Calibration Mode

The **Calibration Mode Switch** controls sampling frequency:
- **OFF (default)**: Channels use their configured `update_interval`
- **ON**: All channels sample at `calibration_interval` (typically 1s)

Use calibration mode when:
- Performing pH calibration with buffer solutions (pH 4, 7, 10)
- Performing ORP calibration with reference solutions
- Checking pressure sensor response
- Capturing data for N-point calibration

## Roadmap

| Lot | Version | Content | Status |
|-----|---------|---------|--------|
| 0 | 0.0.1 | Skeleton, docs, sensor stub | ✅ Done |
| 1 | 0.1.0 | ADS1115 binding, raw values, calibration mode | ✅ Done |
| 2 | 0.2.0 | N-point calibration, Capturer UI, persistence | ✅ Done |
| 3 | 0.3.0 | j5-like filters (median, max_jump, streak, clamp) | ✅ Done |
| 4 | 0.4.0 | Diagnostics (noise σ/ptp, stuck, out_of_range flags) | ✅ Done |
| 5 | 0.5.0 | Water temperature compensation (Tw) | ✅ Done |
| 6 | 0.6.0 | Gates/campaigns (conditional sampling) | ✅ Done |
| **7** | **0.7.16** | **HA polish, runtime algo select, draft/commit, Lot-2 review-fix ports** | ✅ **Current** |
| 8 | — | (Optional) Interference detection, EZO support | 🔮 Future |

### Lot 8 — Future / Out of Scope

The following features are documented as potential future work but are **not planned** for implementation:

- **Interference/correlation detection**: Cross-channel chemistry analysis
- **EZO (I2C Atlas Scientific) support**: Native Atlas driver integration
- **Zelia/Zodiac protocols**: Proprietary closed protocols

## Lot 7 — What's New

### Runtime Algorithm Selection

Change calibration algorithm from Home Assistant without reflashing:

```yaml
capturer:
  algorithm_select:
    name: "pH Algorithm"
```

Creates a select entity. Options are **per-channel**:
- **pressure / pH**: `none`, `linear`, `polynomial`, `piecewise`
- **ORP**: the same list plus `dfrobot_orp`

**Use case**: Switch from `linear` to `piecewise` after adding more calibration points.

### Add/Remove Calibration Points

Dynamic point management from the UI:

```yaml
capturer:
  point_count: 5                 # Max preallocated HA slots (recommend 5–10)
  add_point_button:
    name: "pH Add Point"
  remove_point_button:
    name: "pH Remove Point"
  point_count_number:
    name: "pH Point Count"       # Live engine count
```

- **point_count**: Compile-time **max HA slots** for `cal_point_*` numbers/buttons (1-10). Add/remove **cannot** create new Home Assistant entities beyond this. Recommend 5–10 when using add/remove.
- **add_point_button**: Adds a new calibration point in the engine (x=0, y=0). Existing slots refresh; a 4th point only appears in HA if `point_count >= 4`.
- **remove_point_button**: Removes the last engine point and refreshes remaining slots (unused slots publish `unknown`/NaN).
- **point_count_number**: Live `CalibrationEngine` count (and optional resize 1-10). This is **not** the same as `point_count` slots.

### Draft/Commit Workflow

Preview calibration changes before applying them:

```yaml
capturer:
  draft_mode: true
  commit_button:
    name: "pH Commit Calibration"
  discard_button:
    name: "pH Discard Changes"
  draft_pending:
    name: "pH Draft Pending"
```

**Workflow**:
1. Enable `draft_mode: true` in YAML
2. Edit points using Capturer UI (changes go to draft)
3. Main sensor continues using live/committed calibration
4. Press **Commit** to apply draft → live (and save to flash)
5. Or press **Discard** to revert draft to live

**draft_pending** binary sensor shows `ON` when uncommitted changes exist.

### Backward Compatibility

- `draft_mode: false` (default): Legacy behavior, edits apply immediately
- All new UI components are **opt-in** (only created if declared in YAML)
- Existing configurations work without modification

---

## Lot 5 — What's New

### Water Temperature Binding

```yaml
pool_station:
  # Bind water temperature sensor for compensation
  water_temperature:
    sensor_id: water_temp  # Reference to Dallas DS18B20 sensor
```

> **IMPORTANT**: The water temperature sensor **must** use a fixed ROM address, not index-based binding. See [SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md).

### pH Temperature Compensation (Nernstian Model)

The Nernst equation's electrode slope is temperature-dependent. This module compensates pH readings to a reference temperature using the documented formula:

```
pH_compensated = neutral_ph + (pH_measured - neutral_ph) × slope_ratio
slope_ratio = (T_ref + 273.15) / (T_measured + 273.15)
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `reference_temperature` | 25.0°C | Standard calibration temperature |
| `neutral_ph` | 7.0 | Isopotential point (T has no effect at this pH) |

**Example**: At 30°C water with pH measured = 7.50:
- slope_ratio = 298.15 / 303.15 = 0.9835
- pH_compensated = 7.0 + (7.50 - 7.0) × 0.9835 = **7.492**

### ORP Temperature Compensation (Optional)

ORP compensation uses a simple linear model, **disabled by default**:

```
ORP_compensated = ORP_measured - orp_coefficient × (T_measured - T_ref)
```

| Parameter | Default | Description |
|-----------|---------|-------------|
| `orp_coefficient` | 0.0 mV/°C | Linear temperature coefficient (0 = disabled) |

### Temperature Compensation Configuration

```yaml
channels:
  ph:
    temperature_compensation:
      enabled: true                    # Enable by default
      reference_temperature: 25.0      # Standard calibration temp
      neutral_ph: 7.0                  # Isopotential point
      
      # Runtime switch to enable/disable from Home Assistant
      enable_switch:
        name: "Pool pH Temp Compensation"
    
    # Shows water temperature at last calibration save
    calibration_temp_sensor:
      name: "Pool pH Calibration Tw"
```

### Calibration Temperature Metadata

When pressing the Save Calibration button, the current water temperature is automatically stored with the calibration data:

- Persisted to flash with calibration points
- Displayed via `calibration_temp_sensor` entity
- Logged at calibration save time
- Useful for tracking calibration conditions

### Pipeline Order

```
raw → [median] → raw_sensor → calibrate → TEMP_COMP → calibrated_sensor
    → [clamp] → [jump guard] → main entity
```

Temperature compensation is applied **after** calibration but **before** guards, ensuring compensation works on properly calibrated values.

---

## Lot 4 — What's New

### Diagnostic Statistics
- **noise_window**: Sliding window size for computing statistics (2-50 samples)
- **mean**: Mean value over the sliding window
- **sigma**: Standard deviation (σ) — noise indicator
- **ptp**: Peak-to-peak (max - min) over the window

### Diagnostic Flags (binary_sensors)
- **noisy**: True when ptp or σ exceeds configured thresholds
- **stuck**: True when no meaningful change for `stuck_timeout` duration
- **out_of_range**: True when value is outside `range_min`/`range_max`

### Opt-In Entities
- Diagnostic sensors and flags are **only created if declared** in YAML
- Avoids Home Assistant entity explosion for simple setups
- Pressure channels can enable diagnostics without heavy filters

### Structured Logging
- Rate-limited diagnostic warnings with tag `pool_station`
- Suppressed in calibration mode to avoid log spam
- Configurable via `log_rate_limit` (default 60s)

### Lovelace-Friendly Entity Names
Example entities for pH channel:
- `sensor.pool_ph_mean` — Mean pH over window
- `sensor.pool_ph_noise` — Stddev (σ) of pH readings  
- `sensor.pool_ph_peak_to_peak` — Max - Min over window
- `binary_sensor.pool_ph_noisy` — ON when noise exceeds thresholds
- `binary_sensor.pool_ph_stuck` — ON when no change for stuck_timeout
- `binary_sensor.pool_ph_out_of_range` — ON when outside valid range

## Lot 3 — What's New

### Per-Channel Filters
- **filter_samples**: Median sliding window (0 or 1 = off)
- **max_jump**: Reject absurd jumps between readings
- **max_jump_streak**: Accept new plateau after N consistent jumps
- **value_min / value_max**: Clamp calibrated values to range

### Companion Entities
- **raw_sensor**: Diagnostic raw voltage (already in Lot 1)
- **calibrated_sensor**: Diagnostic calibrated value BEFORE guards (Lot 3)
- **Main entity**: Final guarded value with all filters applied

### Calibration Mode Bypass
- When Calibration Mode is ON, filters are bypassed for ~1s raw response
- Ensures Capturer UI shows immediate readings during calibration
- Jump guard baseline resets on calibration mode toggle

### Pressure Defaults
- All filters OFF by default (`filter_samples: 0`, `max_jump: 0`)
- Pressure sensors need fast, unfiltered response for monitoring

## Lot 2 — What's New

### Calibration Algorithms
- **linear**: 2-point linear interpolation with extrapolation
- **piecewise**: N-point linear segments between adjacent points
- **polynomial**: Least-squares polynomial regression (order 1-5)
- **dfrobot_orp**: SEN0165-like mid/offset formula for ORP

### Capturer UI (Home Assistant)
- **Capture Buttons**: Copy current raw voltage into calibration point X
- **Point X/Y Numbers**: Edit raw voltage (X) and calibrated value (Y) per point
- **Save Button**: Persist calibration to flash (survives reboot)
- **DFRobot Numbers**: mid_mv and offset_mv adjustment for ORP

### Persistence
- Calibration automatically loads from flash on boot
- Runtime changes via Capturer UI are temporary until Save is pressed
- YAML seed points are used as initial values, can be overridden at runtime

### Validation
- `cal_invalid` binary sensor indicates insufficient points for algorithm
- Graceful fallback to pass-through when calibration invalid
- Logging with tag `pool_station` for troubleshooting

## Documentation

- [CDC (Cahier des Charges)](docs/POOL-STATION-CDC.md) — Full requirements specification
- [SENSOR-IDENTITY](docs/SENSOR-IDENTITY.md) — Anti-swap address binding table
- [CHANGELOG](CHANGELOG.md) — Version history
- [Examples](examples/) — YAML configuration examples

## Anti-Swap Protection

**NEVER** use index-based sensor configuration for multi-sensor buses. Sensor roles must be bound to physical ROM addresses.

❌ **Wrong** (will swap):
```yaml
- platform: dallas_temp
  index: 0  # DANGEROUS
  name: "Water"
```

✅ **Correct** (stable):
```yaml
- platform: dallas_temp
  address: 0x28012345678901c8  # Unique ROM address
  name: "Water"
```

See [docs/SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md) for complete guidance.

## Related Projects

- [MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome) — Component architecture reference
- [pool-firmata-wifi](https://github.com/echavet/pool-firmata-wifi) — Original firmware this replaces
- [Johnny-Five](http://johnny-five.io/) — Calibration parity target

## Contributing

Contributions welcome! Please:
1. Follow ESPHome coding conventions
2. Update documentation for new features
3. Test on actual hardware when possible
4. Never break the anti-swap guarantees

## License

MIT License — see [LICENSE](LICENSE)

---

*pool_station* — Because your pool deserves better than YAML lambdas.
