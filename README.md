# ESPHome Pool Station

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESPHome](https://img.shields.io/badge/ESPHome-external_component-blue)](https://esphome.io/components/external_components.html)

> **Rich calibration and diagnostics platform for pool monitoring**

**pool_station** is an ESPHome external component designed to transform an ESP32 into a comprehensive pool monitoring station. This is **NOT** a simple DFRobot driver or vendor wrapper — it's a sophisticated calibration, diagnostics, and intelligent sampling platform.

## Vision

- **Rich N-point calibration** with multiple algorithms: linear, polynomial, piecewise, dfrobot_orp (SEN0165-like mid/offset)
- **Capturer UI**: Calibration workflow directly from Home Assistant with capture buttons and number entities
- **Calibration persistence**: Survives reboots via ESPHome preferences (flash storage)
- **Real-time diagnostics**: noise detection, jump detection, drift monitoring (coming in Lot 4)
- **Gated/campaign sampling**: e.g., filtration OFF for 60s then measure pH/ORP (coming in Lot 6)
- **Water temperature compensation**: calibration and measurements compensated for Tw (coming in Lot 5)
- **Compose ESPHome sensors**: uses native `ads1115`/`dallas`/`gpio` — does NOT reimplement drivers
- **Stable entity IDs**: anti-swap protection by physical address (see [SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md))
- **Brand agnostic**: works with any analog pH/ORP probe, not locked to DFRobot/Atlas/etc.

Designed to replace complex YAML lambdas in [pool-firmata-wifi](https://github.com/echavet/pool-firmata-wifi) (ESP32-S3 + ADS1115: A0 pressure, A2 pH, A3 ORP).

## Current Status

**Lot 3** — Filters & Companion Publication:
- ✅ Per-channel filters (median window, jump guard, min/max clamp)
- ✅ `calibrated_sensor` companion entity (pre-guard diagnostic)
- ✅ Pressure defaults: filters OFF (fast response)
- ✅ Calibration mode bypasses filters (~1s raw response)
- ✅ Pipeline: raw → median → calibrate → clamp → jump guard → publish

**Lot 2** — N-Point Calibration & Capturer UI:
- ✅ N-point calibration (1 to 10 points per channel)
- ✅ Multiple algorithms: linear, polynomial, piecewise, dfrobot_orp
- ✅ Capturer UI: capture buttons, x/y number entities, save button
- ✅ DFRobot ORP: mid_mv/offset_mv number entities for SEN0165-like calibration
- ✅ Calibration persistence to flash (survives reboots)
- ✅ `cal_invalid` binary sensor when calibration is insufficient
- ✅ YAML seed points with runtime override

See [Roadmap](#roadmap) for upcoming lots.

## Installation

Add this to your ESPHome configuration:

```yaml
external_components:
  - source: github://echavet/esphome-pool-station@main
    components: [pool_station]
    refresh: 1d
```

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
        point_count: 3
        capture_buttons: true
        point_numbers: true
        save_button: true
      
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

| Algorithm | Min Points | Use Case | Parameters |
|-----------|------------|----------|------------|
| `linear` | 2 | Simple linear sensors (pressure) | — |
| `piecewise` | 2 | pH sensors, non-linear but monotonic | — |
| `polynomial` | order+1 | Complex curves | `order` (1-5), `precision` |
| `dfrobot_orp` | 0 | DFRobot SEN0165 ORP modules | `mid_mv`, `offset_mv` |
| `exponential` | 2 | TODO: not yet implemented | — |
| `logarithmic` | 2 | TODO: not yet implemented | — |
| `power` | 2 | TODO: not yet implemented | — |

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
      point_count: 3            # Number of calibration points (1-10)
      capture_buttons: true     # Create capture buttons per point
      point_numbers: true       # Create x/y number entities per point
      save_button: true         # Create save to flash button
      mid_number: false         # DFRobot mid_mv number entity
      offset_number: false      # DFRobot offset_mv number entity
    
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

| Lot | Content | Status |
|-----|---------|--------|
| 0 | Skeleton, docs, sensor stub | ✅ Done |
| 1 | ADS1115 binding, raw values, calibration mode | ✅ Done |
| 2 | N-point calibration, Capturer UI, persistence | ✅ Done |
| **3** | **j5-like filters (median, max_jump, streak, clamp)** | ✅ **Current** |
| 4 | Diagnostics (noise σ/ptp, jumps, drift detection) | 📋 Planned |
| 5 | Water temperature compensation (Tw) | 📋 Planned |
| 6 | Gates/campaigns (conditional sampling) | 📋 Planned |
| 7 | HA polish, runtime algo select, services | 📋 Planned |
| 8 | (Optional) Interference detection, EZO support | 🔮 Future |

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
