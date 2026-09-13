# Sensor Identity — Anti-Swap Table

> **CRITICAL**: This document defines the binding between physical sensor addresses and their roles.  
> **NEVER use AUTO-discovery for multi-sensor buses** — roles must be explicitly assigned by address.

## The Problem

When multiple DS18B20 sensors share a 1-Wire bus (e.g., GPIO13), their discovery order can change:
- After power cycle
- After firmware update
- After physical rewiring

If roles are assigned by scan order (sensor 0 = water, sensor 1 = air), **swapped readings will corrupt your data** without any visible error.

## The Solution

Bind roles to **physical ROM addresses**, not scan order. Each DS18B20 has a unique 64-bit address that never changes.

## Production Reference Table

| Role | Address (ROM) | GPIO | HA Entity | Physical Location |
|------|---------------|------|-----------|-------------------|
| `water_temperature` | `0xc802...` | 17 | `sensor.pool_water_temp` | Immersed probe |
| `air_pump` | `0x7001...` | 13 | `sensor.pool_air` | Near pump enclosure |
| `air_local` | `0x4401...` | 13 | `sensor.pool_air2` | Local ambient |

> **Note**: Addresses shown as `0xc802...` are abbreviated. Use full 16-character hex in YAML.

## How to Find Your Addresses

### Method 1: ESPHome Debug Logs

Enable debug logging for the `dallas` component:

```yaml
logger:
  level: DEBUG
  logs:
    dallas.component: DEBUG
```

On boot, you'll see:

```
[D][dallas.component:xxx]: Found sensors:
[D][dallas.component:xxx]:   0x28012345678901c8 (DS18B20)
[D][dallas.component:xxx]:   0x28aabbccddeeff70 (DS18B20)
[D][dallas.component:xxx]:   0x2811223344556644 (DS18B20)
```

### Method 2: Scan Once, Label Physically

1. Connect ONE sensor at a time
2. Note its address in logs
3. Label the physical sensor/cable
4. Repeat for each sensor

## YAML Configuration Pattern

**WRONG** — Index-based (will swap):

```yaml
# DON'T DO THIS
dallas:
  - pin: GPIO13
sensor:
  - platform: dallas
    index: 0
    name: "Water Temp"  # DANGEROUS: index can change!
```

**CORRECT** — Address-based (stable):

```yaml
dallas:
  - pin: GPIO13
    id: dallas_bus

sensor:
  # Explicit address binding
  - platform: dallas
    dallas_id: dallas_bus
    address: 0x28012345678901c8  # Full 64-bit ROM
    name: "Pool Water Temperature"
    id: pool_water_temp

  - platform: dallas
    dallas_id: dallas_bus
    address: 0x28aabbccddeeff70
    name: "Pool Air Pump"
    id: pool_air_pump

  - platform: dallas
    dallas_id: dallas_bus
    address: 0x2811223344556644
    name: "Pool Air Local"
    id: pool_air_local
```

## pool_station Integration

When binding sensors to pool_station, reference by ID (which is address-bound):

```yaml
pool_station:
  id: pool
  water_temperature:
    sensor_id: pool_water_temp  # References the address-bound sensor

sensor:
  - platform: pool_station
    pool_station_id: pool
    role: water_temperature
    name: "Water Temperature (Calibrated)"
```

## Verification Checklist

Before deploying, verify:

- [ ] Each DS18B20 address is documented
- [ ] Physical labels match configuration
- [ ] No index-based references in YAML
- [ ] Test: unplug/replug sensors, verify no swap
- [ ] Test: reboot, verify no swap

## Address Format Reference

DS18B20 addresses are 64 bits (8 bytes):

```
Byte:    0    1    2    3    4    5    6    7
         |    |-------- Serial --------|    |
         |                                  |
    Family code (0x28)               CRC-8 checksum
```

ESPHome expects the full address as `0x` prefixed hex:
- Valid: `0x28012345678901c8`
- Invalid: `28:01:23:45:67:89:01:c8` (colon format not accepted)

## Troubleshooting

### "Address not found" error

1. Check wiring (4.7kΩ pull-up required)
2. Verify GPIO number
3. Confirm address format (0x prefix, 16 hex chars)

### Readings swapped after reboot

You're using index-based configuration. Switch to address-based immediately.

### Multiple buses

If you have sensors on different GPIOs, use separate `dallas:` entries with distinct IDs:

```yaml
dallas:
  - pin: GPIO17
    id: dallas_water
  - pin: GPIO13
    id: dallas_air

sensor:
  - platform: dallas
    dallas_id: dallas_water
    address: 0x28012345678901c8
    name: "Water"

  - platform: dallas
    dallas_id: dallas_air
    address: 0x28aabbccddeeff70
    name: "Air Pump"
```
