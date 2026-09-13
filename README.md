# ESPHome Pool Station

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![ESPHome](https://img.shields.io/badge/ESPHome-external_component-blue)](https://esphome.io/components/external_components.html)

> **Rich calibration and diagnostics platform for pool monitoring**

**pool_station** is an ESPHome external component designed to transform an ESP32 into a comprehensive pool monitoring station. This is **NOT** a simple DFRobot driver or vendor wrapper — it's a sophisticated calibration, diagnostics, and intelligent sampling platform.

## Vision

- **Rich N-point calibration** with multiple algorithms: linear, polynomial, exponential, logarithmic, power, piecewise, dfrobot_orp (SEN0165-like mid/offset)
- **Real-time diagnostics**: noise detection, jump detection, drift monitoring, diagnostic flags
- **Gated/campaign sampling**: e.g., filtration OFF for 60s then measure pH/ORP; compare chlorine ON/OFF
- **Water temperature compensation**: calibration and measurements compensated for Tw
- **Compose ESPHome sensors**: uses native `ads1115`/`dallas`/`gpio` — does NOT reimplement drivers
- **Stable entity IDs**: anti-swap protection by physical address (see [SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md))
- **Brand agnostic**: works with any analog pH/ORP probe, not locked to DFRobot/Atlas/etc.

Designed to replace complex YAML lambdas in [pool-firmata-wifi](https://github.com/echavet/pool-firmata-wifi) (ESP32-S3 + ADS1115: A0 pressure, A2 pH, A3 ORP).

## Current Status

**Lot 1** — ADS1115 Channel Binding:
- ✅ Real ADS1115-backed channels (pressure, pH, ORP)
- ✅ Raw voltage exposure as diagnostic sensors
- ✅ Calibration mode switch for high-frequency sampling
- ✅ Channels compose native ESPHome ads1115 sensors
- ✅ Pressure defaults: 1-2s interval, no heavy filters
- ⏳ Calibration: pass-through stub (real N-point in Lot 2)

See [Roadmap](#roadmap) for upcoming lots.

## Installation

Add this to your ESPHome configuration:

```yaml
external_components:
  - source: github://echavet/esphome-pool-station@main
    components: [pool_station]
    refresh: 1d
```

## Quick Start (Lot 1)

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
    multiplexer: 'A0_GND'
    gain: 4.096
    id: ads_pressure
    internal: true
    update_interval: 1s

  - platform: ads1115
    ads1115_id: ads
    multiplexer: 'A2_GND'
    gain: 4.096
    id: ads_ph
    internal: true
    update_interval: 1s

  - platform: ads1115
    ads1115_id: ads
    multiplexer: 'A3_GND'
    gain: 4.096
    id: ads_orp
    internal: true
    update_interval: 1s

# Pool Station with channels
pool_station:
  id: pool
  calibration_interval: 1s
  
  calibration_mode:
    name: "Pool Calibration Mode"

  channels:
    pressure:
      source_id: ads_pressure
      name: "Filter Pressure"
      update_interval: 2s
      raw_sensor:
        name: "Filter Pressure Raw"

    ph:
      source_id: ads_ph
      name: "Pool pH"
      update_interval: 60s
      raw_sensor:
        name: "Pool pH Raw"

    orp:
      source_id: ads_orp
      name: "Pool ORP"
      update_interval: 60s
      raw_sensor:
        name: "Pool ORP Raw"
```

For a complete example with temperature sensors, see [examples/pool-station-minimal.yaml](examples/pool-station-minimal.yaml).

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
  
  channels:                     # ADS1115-backed measurement channels
    pressure:
      source_id: ads_pressure   # Reference to ads1115 sensor
      name: "Filter Pressure"
      unit_of_measurement: "bar"
      accuracy_decimals: 2
      update_interval: 2s       # Normal mode interval
      raw_sensor:               # Diagnostic raw voltage sensor
        name: "Pressure Raw Volts"
    
    ph:
      source_id: ads_ph
      name: "Pool pH"
      # ...
    
    orp:
      source_id: ads_orp
      name: "Pool ORP"
      # ...
  
  water_temperature:            # For future Tw compensation (Lot 5)
    sensor_id: water_temp
```

### Calibration Mode

The **Calibration Mode Switch** controls sampling frequency:
- **OFF (default)**: Channels use their configured `update_interval`
- **ON**: All channels sample at `calibration_interval` (typically 1s)

Use calibration mode when:
- Performing pH calibration with buffer solutions (pH 4, 7, 10)
- Performing ORP calibration with reference solutions
- Checking pressure sensor response
- Capturing data for N-point calibration (Lot 2)

### Channel Sensors

Each channel creates two sensors:
1. **Main sensor**: Calibrated value (stub pass-through in Lot 1)
2. **Raw sensor**: Diagnostic voltage reading from ADS1115

Raw sensors are always exposed (entity_category: diagnostic) for troubleshooting and calibration verification.

## Roadmap

| Lot | Content | Status |
|-----|---------|--------|
| 0 | Skeleton, docs, sensor stub | ✅ Done |
| **1** | ADS1115 binding, raw values, calibration mode | ✅ **Current** |
| 2 | N-point calibration, Capturer UI, persistence | 📋 Planned |
| 3 | j5-like filters (median, max_jump, streak, clamp) | 📋 Planned |
| 4 | Diagnostics (noise, jumps, drift detection) | 📋 Planned |
| 5 | Water temperature compensation | 📋 Planned |
| 6 | Gates/campaigns (conditional sampling) | 📋 Planned |
| 7 | Home Assistant polish, services, migration guide | 📋 Planned |
| 8 | (Optional) Interference detection, EZO support | 🔮 Future |

## Lot 1 — What's Real vs Stub

### Real (working)
- ADS1115 source sensor composition (reads actual ADC values)
- Channel subscription to source sensors
- Raw voltage diagnostic sensors (always accurate)
- Calibration mode switch (controls sampling frequency)
- Update interval switching based on calibration mode
- Temperature sensors with address-bound configuration

### Stub (pass-through, coming in later lots)
- **pH calibration**: passes through voltage (Lot 2: N-point algorithms)
- **ORP calibration**: converts V→mV (Lot 2: proper calibration)
- **Pressure calibration**: passes through voltage (Lot 2: formula config)
- **Filters**: no filtering applied (Lot 3: median, jump rejection)
- **Diagnostics**: no noise/drift detection (Lot 4)
- **Tw compensation**: sensor bound but not used (Lot 5)

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
