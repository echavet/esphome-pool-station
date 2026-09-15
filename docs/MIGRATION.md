# Migration Guide — pool_station

This guide helps migrate from older pool monitoring configurations to `pool_station`.

## Table of Contents

- [From j5_ha_bridge to pool_station](#from-j5_ha_bridge-to-pool_station)
- [From pool-firmata-wifi (lambda-heavy) to pool_station](#from-pool-firmata-wifi-to-pool_station)
- [Entity ID Stability](#entity-id-stability)
- [Calibration NVS key (v0.7.16)](#calibration-nvs-key-v0716)
- [Capturer draft / Save (v0.7.17)](#capturer-draft-save-v0717)
- [Capturer draft / Save review fixes (v0.7.18)](#capturer-draft-save-review-fixes-v0718)
- [Interference detection (v0.8.0)](#interference-detection-v080)
- [SENSOR-IDENTITY Checklist Before OTA](#sensor-identity-checklist-before-ota)

---

## From j5_ha_bridge to pool_station

If you were using the Johnny-Five Home Assistant bridge (j5_ha_bridge) for pool sensor calibration, here's how to migrate:

### j5_ha_bridge Calibration Options

| j5_ha_bridge Option | pool_station Equivalent | Notes |
|---------------------|------------------------|-------|
| `scale: [min, max]` | `calibration: type: linear` + 2 points | Use piecewise for N-point |
| `fsr.curve: [[x1,y1], ...]` | `calibration: points: [{x:, y:}]` | Same concept, YAML syntax |
| `threshold: {high, low}` | `filters: value_min/value_max` | Clamp values |
| `freq: 25` | `update_interval: 40ms` | Sampling rate |
| `median: true` | `filters: filter_samples: 5` | Median window |

### Example Migration

**j5_ha_bridge (old)**:
```javascript
// In Johnny-Five sensor configuration
{
  pin: "A2",
  freq: 25,
  scale: [0, 14],
  threshold: 0,
  fsr: {
    curve: [[0.5, 4.0], [1.5, 7.0], [2.5, 10.0]]
  }
}
```

**pool_station (new)**:
```yaml
channels:
  ph:
    source_id: ads_ph
    name: "Pool pH"
    update_interval: 40ms  # freq: 25 = 40ms
    
    calibration:
      type: piecewise
      points:
        - x: 0.5
          y: 4.0
        - x: 1.5
          y: 7.0
        - x: 2.5
          y: 10.0
    
    filters:
      value_min: 0.0
      value_max: 14.0
```

---

## From pool-firmata-wifi to pool_station

If you were using the original `pool-firmata-wifi` firmware with complex YAML lambdas, here's the migration path:

### Lambda-Based Calibration (Old)

```yaml
# OLD: Complex lambda in pool-firmata-wifi
sensor:
  - platform: ads1115
    multiplexer: 'A2_GND'
    name: "Pool pH"
    filters:
      - lambda: |-
          // Multi-point calibration with hardcoded values
          float v1 = 2.03, p1 = 4.01;
          float v2 = 1.50, p2 = 7.00;
          float v3 = 0.98, p3 = 10.00;
          
          if (x <= v1) {
            return p1 + (x - v1) * (p2 - p1) / (v2 - v1);
          } else if (x <= v2) {
            return p2 + (x - v2) * (p3 - p2) / (v3 - v2);
          } else {
            return p3;
          }
```

### pool_station Native Calibration (New)

```yaml
# NEW: Native N-point calibration in pool_station
pool_station:
  channels:
    ph:
      source_id: ads_ph
      name: "Pool pH"
      
      calibration:
        type: piecewise
        points:
          - x: 2.03
            y: 4.01
          - x: 1.50
            y: 7.00
          - x: 0.98
            y: 10.00
      
      # Capturer UI for runtime calibration editing
      capturer:
        point_count: 5   # Max HA slots (recommend 5–10 if using add/remove)
        capture_buttons: true
        point_numbers: true
        save_button: true
        point_count_number:
          name: "pH Point Count"   # Live engine count
```

### Benefits of Migration

| Aspect | pool-firmata-wifi | pool_station |
|--------|-------------------|--------------|
| Calibration editing | Requires YAML edit + flash | Live from Home Assistant |
| Persistence | Hardcoded in firmware | Flash storage (survives reboot) |
| Multiple algorithms | Manual lambda implementation | Built-in: linear, piecewise, polynomial, dfrobot_orp |
| Diagnostics | None | Noise detection, stuck detection, out-of-range |
| Temperature compensation | Manual lambda | Built-in Nernstian model |
| Entity organization | All in one namespace | Diagnostic entities auto-categorized |

### Step-by-Step Migration

1. **Document existing calibration values** from your lambdas
2. **Set up pool_station** with your ADS1115 sources
3. **Add calibration points** matching your lambda values
4. **Test in calibration mode** with known solutions
5. **Save calibration** to flash
6. **Remove old lambda filters** from your config

---

## Entity ID Stability

When migrating to pool_station, entity IDs are generated from your YAML configuration. To ensure stable entity IDs:

### Naming Best Practices

```yaml
# RECOMMENDED: Explicit names for stable entity_ids
pool_station:
  id: pool  # Component ID affects preferences key
  
  channels:
    ph:
      name: "Pool pH"           # → sensor.pool_ph
      raw_sensor:
        name: "Pool pH Raw"     # → sensor.pool_ph_raw
      calibrated_sensor:
        name: "Pool pH Cal"     # → sensor.pool_ph_cal
      cal_invalid:
        name: "pH Cal Invalid"  # → binary_sensor.ph_cal_invalid
```

### Entity ID Mapping

| Configuration | Generated Entity ID |
|--------------|---------------------|
| `name: "Pool pH"` | `sensor.pool_ph` |
| `name: "Pool pH Raw"` | `sensor.pool_ph_raw` |
| `name: "pH Cal Invalid"` | `binary_sensor.ph_cal_invalid` |
| `name: "Pool Calibration Mode"` | `switch.pool_calibration_mode` |

### Preserving Historical Data

If you have history/statistics on old entity IDs:

1. **Option A**: Name new entities to match old IDs
2. **Option B**: Use `entity_id:` override in Home Assistant after discovery
3. **Option C**: Accept new IDs and archive old data

---

## Calibration NVS key (v0.7.16)

Through v0.7.15, each channel's flash preferences key was
`hash((component_id, channel_type)) & 0xFFFFFFFF`. Python's `hash()` is
randomized per process (`PYTHONHASHSEED`), so a firmware recompile could
write and later fail to find the same NVS slot.

v0.7.16 uses a **deterministic MD5** (`ps-md5-v1:component_id:channel_type`).
The old key cannot be reconstructed, so there is **no automatic migration**.

After flashing 0.7.16:

1. Turn on Calibration Mode
2. Recapture (or re-enter) points for each channel
3. Press Save
4. Confirm `cal_invalid` is off and values match buffers / known references

If the MD5 payload scheme is ever bumped again, treat it as another one-time
recalibration — keep the scheme prefix versioned so a future dual-read
migration is possible.

Keep `pool_station.id` stable. Changing the component id still generates a
new key and orphans saved calibration.

---

## Capturer draft / Save (v0.7.17)

Through v0.7.16, Eric-style YAML with `draft_mode: false` applied Point Count /
Add / Remove / Capture to `live_points_` immediately. The Save button only
wrote that already-live set to flash. A Home Assistant page refresh does
**not** reload the ESP, so there was no undo.

v0.7.17 makes **draft + Save** the supported Capturer path for multi-point
channels:

| Action | Effect |
|--------|--------|
| Edit points / count / capture / algorithm | Draft only (`draft_pending` ON) |
| Live published reading | Last **committed / saved** calibration |
| **Save** | `commit_draft()` → live + flash + clear dirty |
| **Discard / Annuler** | Revert draft to live, clear dirty |

Enable in YAML (keep the existing Save button):

```yaml
capturer:
  point_count: 5
  save_button: true
  point_count_number:
    name: "pH Point Count"
  draft_mode: true
  discard_button:
    name: "pH Discard Changes"
  draft_pending:
    name: "pH Draft Pending"
```

`commit_button` is optional and does the same as Save when `draft_mode` is
on. Prefer a single Save button.

`draft_mode: false` remains the legacy immediate-apply path.

Lovelace note: dashboard wording such as "V croissant" means volts on the
raw X axis; that label is a dashboard concern, not this firmware change.

---

## Capturer draft / Save review fixes (v0.7.18)

v0.7.17 left three holes:

- The publish pipeline still gated on `get_type()` (draft). Choosing
  algorithm `none` without Save published raw values.
- Save committed an invalid draft to live + flash (no undo after reboot).
- Example ORP Capturer stayed on the legacy immediate-apply path.

v0.7.18:

- Published readings use `get_live_type()` + last committed points
- Save / Commit no-op if `is_draft_valid()` is false (`draft_pending` stays ON)
- `draft_invalid` is ON while Save would be refused (HA-visible; not `cal_invalid`)
- Enable draft on ORP mid/offset the same way as pH / pressure:

```yaml
capturer:
  save_button: true
  draft_mode: true
  discard_button:
    name: "ORP Discard Changes"
  draft_pending:
    name: "ORP Draft Pending"
  draft_invalid:
    name: "ORP Draft Invalid"
  mid_number: true
  offset_number: true
```

---

## Interference detection (v0.8.0)

Lot 8 is **opt-in**. Existing YAML without `interference:` is unchanged.

This is a **suspected interference** flag (pump/EMI coincidence), not a
chemistry diagnosis and not Atlas EZO.

Uses **pre-jump-guard** (compensated) samples. Default `jump_window: 90s`
covers a 60s ORP channel vs 2s pressure. Calibration Mode skips pushes and
resets the rolling windows on enter and leave.

```yaml
pool_station:
  interference:
    coincident_jump: true
    shared_noise: true
    tw_coupling: false
    hold_time: 30s
    jump_window: 90s
    tw_window: 180s
    orp_jump: 40.0
    pressure_jump: 0.15
    ph_sigma: 0.08
    orp_sigma: 10.0
    suspected:
      name: "Pool Interference"
    coincident_jump_flag:
      name: "Pool Pump ORP Interference"
    shared_noise_flag:
      name: "Pool Shared Noise"
```

Turn **Calibration Mode** ON while using buffers so Capturer tours do not
pollute σ windows.

---

## SENSOR-IDENTITY Checklist Before OTA

**CRITICAL**: Before OTA updating to pool_station, verify your sensor identity configuration.

### Pre-OTA Checklist

- [ ] **1. Document ALL Dallas addresses**
  ```bash
  # Check ESPHome logs for addresses
  grep -i "dallas" /config/esphome/.esphome/build/your-device/logs/*.log
  ```

- [ ] **2. Verify address-based configuration**
  ```yaml
  # CORRECT - uses address
  - platform: dallas_temp
    address: 0x28012345678901c8
    name: "Water"
  
  # WRONG - uses index (WILL SWAP!)
  - platform: dallas_temp
    index: 0
    name: "Water"
  ```

- [ ] **3. Physical label verification**
  - Each sensor cable/probe is physically labeled
  - Labels match documented addresses

- [ ] **4. Test swap scenario**
  1. Note which sensor shows "Water" temperature
  2. Physically swap two sensor cables
  3. Verify readings swapped (bad) or stayed correct (good)
  4. Swap cables back

- [ ] **5. Update configuration if needed**
  ```yaml
  # For each Dallas sensor, use explicit address:
  one_wire:
    - platform: gpio
      pin: GPIO17
      id: onewire_water
  
  sensor:
    - platform: dallas_temp
      one_wire_id: onewire_water
      address: 0xc802000000001c28  # YOUR actual address
      name: "Water Temperature"
      id: water_temp
  ```

- [ ] **6. Verify pool_station binding**
  ```yaml
  pool_station:
    water_temperature:
      sensor_id: water_temp  # References address-bound sensor
  ```

### Post-OTA Verification

After OTA update:

1. Check ESPHome logs for sensor registration
2. Verify temperature readings match physical locations
3. Confirm calibration data loaded from preferences
   (flashing **0.7.16+** from ≤0.7.15 requires a one-time recapture — see
   [Calibration NVS key](#calibration-nvs-key-v0716))
4. Test calibration mode switch functionality

### Troubleshooting

**Symptoms of address swap**:
- "Water" temperature shows air temperature
- Readings oscillate between sensors
- Temperature spikes when moving cables

**Resolution**:
1. Check logs for discovered addresses
2. Update YAML with correct address bindings
3. Flash firmware with corrected config
4. Verify physical labels match config

---

## See Also

- [SENSOR-IDENTITY.md](SENSOR-IDENTITY.md) — Full address binding documentation
- [README.md](../README.md) — Complete pool_station documentation
- [CHANGELOG.md](../CHANGELOG.md) — Version history and migration notes
