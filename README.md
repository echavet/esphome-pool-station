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

**Lot 0** — Skeleton implementation:
- ✅ Component structure valid for ESPHome
- ✅ Python codegen with CONFIG_SCHEMA
- ✅ C++ component with `pool_station` log tag
- ✅ Stub sensor registration
- ✅ Documentation: CDC, SENSOR-IDENTITY, examples

See [Roadmap](#roadmap) for upcoming lots.

## Installation

Add this to your ESPHome configuration:

```yaml
external_components:
  - source: github://echavet/esphome-pool-station
    components: [pool_station]
    refresh: 1d
```

## Quick Start

```yaml
# Minimal configuration (Lot 0 — skeleton only)
external_components:
  - source: github://echavet/esphome-pool-station
    components: [pool_station]

pool_station:
  id: pool
  update_interval: 1s
  calibration:
    mode: none  # Lot 2+: linear, polynomial, etc.
```

For a complete example with hardware bindings, see [examples/pool-station-minimal.yaml](examples/pool-station-minimal.yaml).

## Hardware Reference

| Component | Model | Connection | Notes |
|-----------|-------|------------|-------|
| MCU | ESP32-S3 | — | Tested on pool-firmata-wifi |
| ADC | ADS1115 | I2C 0x48 | 4 channels, 16-bit |
| Pressure | 0-5V sensor | A0 | Via ADS1115 |
| pH | Probe + amplifier | A2 | Via ADS1115 |
| ORP | Probe + amplifier | A3 | Via ADS1115 |
| Water Tw | DS18B20 `0xc802...` | GPIO17 | Immersed probe |
| Air pump | DS18B20 `0x7001...` | GPIO13 | Near pump |
| Air local | DS18B20 `0x4401...` | GPIO13 | Ambient |

> **IMPORTANT**: Temperature sensors **must** be bound by physical address, not scan order.  
> See [docs/SENSOR-IDENTITY.md](docs/SENSOR-IDENTITY.md) for anti-swap configuration.

## Roadmap

| Lot | Content | Status |
|-----|---------|--------|
| **0** | Skeleton, docs, sensor stub | ✅ **Current** |
| 1 | ADS1115 binding, raw values, calibration mode toggle | 📋 Planned |
| 2 | N-point calibration, Capturer UI, persistence | 📋 Planned |
| 3 | j5-like filters (median, max_jump, streak, clamp) | 📋 Planned |
| 4 | Diagnostics (noise, jumps, drift detection) | 📋 Planned |
| 5 | Water temperature compensation | 📋 Planned |
| 6 | Gates/campaigns (conditional sampling) | 📋 Planned |
| 7 | Home Assistant polish, services, migration guide | 📋 Planned |
| 8 | (Optional) Interference detection, EZO support | 🔮 Future |

## Documentation

- [CDC (Cahier des Charges)](docs/POOL-STATION-CDC.md) — Full requirements specification
- [SENSOR-IDENTITY](docs/SENSOR-IDENTITY.md) — Anti-swap address binding table
- [Examples](examples/) — YAML configuration examples

## Calibration Algorithms (Lot 2+)

| Mode | Description | Use Case |
|------|-------------|----------|
| `none` | Pass-through, no calibration | Raw values only |
| `linear` | y = mx + b | Simple 2-point calibration |
| `polynomial` | Polynomial fit | Multi-point curves |
| `exponential` | y = a·e^(bx) | Exponential response |
| `logarithmic` | y = a·ln(x) + b | Log response |
| `power` | y = a·x^b | Power curves |
| `piecewise` | Linear segments | Complex non-linear |
| `dfrobot_orp` | Mid/offset (SEN0165) | DFRobot ORP compatibility |

## Sensor Roles

| Role | Description | Unit |
|------|-------------|------|
| `water_temperature` | Water probe | °C |
| `air_pump` | Air near pump | °C |
| `air_local` | Local ambient | °C |
| `ph_raw` | pH raw voltage | mV |
| `ph_calibrated` | pH calibrated | pH |
| `orp_raw` | ORP raw voltage | mV |
| `orp_calibrated` | ORP calibrated | mV |
| `pressure_raw` | Pressure raw | mV |
| `pressure_calibrated` | Pressure calibrated | bar |

## Anti-Swap Protection

**NEVER** use index-based sensor configuration for multi-sensor buses. Sensor roles must be bound to physical ROM addresses.

❌ **Wrong** (will swap):
```yaml
- platform: dallas
  index: 0  # DANGEROUS
  name: "Water"
```

✅ **Correct** (stable):
```yaml
- platform: dallas
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
