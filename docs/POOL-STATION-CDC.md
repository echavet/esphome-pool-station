# Pool Station — Cahier des Charges (CDC)

> Version: 0.8.1 (Lot 8 — détection d'interférences station, pas EZO)  
> Date: 2026-09-15  
> Auteur: @echavet

## 1. Vision

**pool_station** est un composant externe ESPHome conçu pour transformer un ESP32 en station de monitoring complète pour piscine. Il ne s'agit **pas** d'un simple driver DFRobot ou d'un wrapper vendeur — c'est une plateforme de calibration riche, de diagnostics avancés et d'échantillonnage intelligent.

### Objectifs stratégiques

1. **Remplacer les lambdas YAML complexes** de pool-firmata-wifi par un composant C++ natif
2. **Parité j5 et au-delà** — égaler puis dépasser les capacités de calibration Johnny-Five
3. **UX Capturer** — interface de calibration guidée avec draft/commit
4. **Diagnostics temps réel** — détection bruit, sauts, dérive
5. **Campagnes de mesure** — échantillonnage conditionné (filtration OFF, chlore ON/OFF)
6. **Compensation température** — calibration et mesures compensées en Tw
7. **IDs d'entités stables** — anti-swap par adresse physique (voir SENSOR-IDENTITY.md)
8. **Agnostique marque** — pas de dépendance DFRobot/Atlas/autre

## 2. Non-objectifs (hors périmètre)

| Item | Raison |
|------|--------|
| Réimplémenter ads1115/dallas/onewire | Compose les composants ESPHome existants |
| Support natif Zelia/Zodiac | Protocoles propriétaires fermés |
| Interface Lovelace v1 | Première version sans UI HA custom |
| Support EZO (I2C Atlas) | Hors scope (pas de driver Atlas dans ce composant) |

## 3. Exigences fonctionnelles

### F-CH : Canaux de mesure

| ID | Exigence | Lot |
|----|----------|-----|
| F-CH-01 | Valeurs brutes toujours exposées (`*_raw`) | 1 |
| F-CH-02 | Mode calibration actif publie ~1s (haute fréquence) | 2 |
| F-CH-03 | Pression : 1-2s, pas de filtres lourds | 1 |
| F-CH-04 | pH/ORP : publication après calibration | 2 |

### F-CAL : Calibration N-points

| ID | Exigence | Lot |
|----|----------|-----|
| F-CAL-01 | Points de calibration libres (1 à N points) | 2 |
| F-CAL-02 | Algorithmes : linear, polynomial, piecewise ; dfrobot_orp **ORP only** (exponential, logarithmic, power: **non implémentés**) | 2 |
| F-CAL-03 | Interface Capturer pour saisie guidée | 2 |
| F-CAL-04 | Persistance preferences (flash) | 2 |
| F-CAL-05 | Draft/Save workflow (Save=commit, Discard, draft_pending, draft_invalid) | 2 |
| F-CAL-06 | Indicateur `cal_invalid` si calibration périmée/absente | 2 |
| F-CAL-07 | dfrobot_orp : mode mid/offset SEN0165-like | 2 |

### F-FLT : Filtres

| ID | Exigence | Lot | Status |
|----|----------|-----|--------|
| F-FLT-01 | `filter_samples` : médiane/moyenne sur N échantillons | 3 | ✅ Done |
| F-FLT-02 | `max_jump` : rejet des sauts aberrants | 3 | ✅ Done |
| F-FLT-03 | `streak` : validation par séquence stable | 3 | ✅ Done |
| F-FLT-04 | `min/max` : clamp des valeurs hors plage | 3 | ✅ Done |
| F-FLT-05 | Publication raw et calibrated séparément | 3 | ✅ Done |

### F-DIAG : Diagnostics

| ID | Exigence | Lot | Status |
|----|----------|-----|--------|
| F-DIAG-01 | Détection bruit excessif (σ sur fenêtre) | 4 | ✅ Done |
| F-DIAG-02 | Détection sauts (|Δ| > seuil) | 4 | ✅ Done (via jump_magnitude) |
| F-DIAG-03 | Détection dérive (stuck detection) | 4 | ✅ Done |
| F-DIAG-04 | Flags exposés comme binary_sensors | 4 | ✅ Done |
| F-DIAG-05 | Stats exposés comme sensors (mean, σ, ptp) | 4 | ✅ Done |
| F-DIAG-06 | Logging structuré avec rate-limiting | 4 | ✅ Done |

### F-GATE : Échantillonnage conditionné (Gates/Campaigns)

| ID | Exigence | Lot | Status |
|----|----------|-----|--------|
| F-GATE-01 | Prepare : condition d'entrée (ex: filtration=OFF) | 6 | ✅ Done |
| F-GATE-02 | Delay : attente stabilisation (ex: 60s) | 6 | ✅ Done |
| F-GATE-03 | Burst : N échantillons rapprochés | 6 | ✅ Done |
| F-GATE-04 | Restore : retour état initial | 6 | ✅ Done |
| F-GATE-05 | Comparaison chlore ON/OFF (via conditions_tag) | 6 | ✅ Done |
| F-GATE-06 | Gates: bloquer échantillonnage si conditions non satisfaites | 6 | ✅ Done |
| F-GATE-07 | gate_blocked binary_sensor | 6 | ✅ Done |
| F-GATE-08 | Campaign state machine (idle→preparing→waiting→sampling→restoring→idle) | 6 | ✅ Done |
| F-GATE-09 | Safety timeout avec abort + restore | 6 | ✅ Done |
| F-GATE-10 | UI: start/abort buttons, campaign_running binary_sensor | 6 | ✅ Done |

### F-TEMP : Compensation température eau

| ID | Exigence | Lot | Status |
|----|----------|-----|--------|
| F-TEMP-01 | Binding capteur eau (`water_temperature: sensor_id`) | 5 | ✅ Done |
| F-TEMP-02 | Compensation pH selon Tw (modèle Nernstien documenté) | 5 | ✅ Done |
| F-TEMP-03 | Métadonnées cal incluent Tw de référence | 5 | ✅ Done |
| F-TEMP-04 | Switch runtime pour activer/désactiver compensation | 5 | ✅ Done |
| F-TEMP-05 | Compensation ORP optionnelle (désactivée par défaut) | 5 | ✅ Done |
| F-TEMP-06 | capteur Dallas avec adresse fixe (SENSOR-IDENTITY) | 5 | ✅ Done |

### F-ID : Identité capteurs (Anti-swap)

| ID | Exigence | Lot |
|----|----------|-----|
| F-ID-01 | Rôle lié à adresse physique, pas ordre scan | 0 |
| F-ID-02 | Table explicite dans SENSOR-IDENTITY.md | 0 |
| F-ID-03 | Configuration YAML par adresse | 1 |

### F-IO : Entrées/Sorties

| ID | Exigence | Lot |
|----|----------|-----|
| F-IO-01 | ADS1115 A0-A3 via composant ESPHome natif | 1 |
| F-IO-02 | Dallas DS18B20 via composant ESPHome natif | 1 |
| F-IO-03 | GPIO pour contrôle relais (filtration, chlore) | 6 |

### F-HA : Intégration Home Assistant

| ID | Exigence | Lot | Status |
|----|----------|-----|--------|
| F-HA-01 | Entités sensor standard | 0 | ✅ Done |
| F-HA-02 | Attributs diagnostics | 4 | ✅ Done |
| F-HA-03 | Services calibration (via UI) | 7 | ✅ Done |
| F-HA-04 | Notifications calibration périmée (cal_invalid) | 7 | ✅ Done |
| F-HA-05 | Runtime algorithm selection | 7 | ✅ Done |
| F-HA-06 | Draft/Save workflow (Save=commit, Discard, draft_pending, draft_invalid) | 7 | ✅ Done |
| F-HA-07 | Add/remove calibration points | 7 | ✅ Done |

### F-INT : Détection d'interférences (station)

Indicateurs de *suspicion*, pas un diagnostic chimique. Opt-in YAML `interference:`.

| ID | Exigence | Lot | Status |
|----|----------|-----|--------|
| F-INT-01 | Saut coïncident ORP ↔ pression ; `jump_window` ≥ intervalle du canal le plus lent (défaut 90s) | 8 | ✅ Done |
| F-INT-02 | Bruit partagé pH ↔ ORP (σ des deux canaux au-dessus seuil) | 8 | ✅ Done |
| F-INT-03 | Couplage pH ↔ Tw optionnel ; `tw_window` ≥ 2× intervalle pH (défaut 180s) | 8 | ✅ Done |
| F-INT-04 | Mode calibration : pas de push, reset des fenêtres à l'entrée et à la sortie | 8 | ✅ Done |
| F-INT-05 | Hold time pour visibilité HA + binary_sensors `device_class: problem` | 8 | ✅ Done |
| F-INT-06 | Pas de driver Atlas EZO / Zelia | 8 | ✅ Done (hors scope) |
| F-INT-07 | Échantillons pré-jump-guard (compensated) pour ne pas masquer l'EMI pompe | 8 | ✅ Done |

## 4. Exigences non-fonctionnelles

| ID | Exigence | Lot |
|----|----------|-----|
| NF-01 | C++ external_component, style CN105 | 0 |
| NF-02 | Log tag `pool_station` | 0 |
| NF-03 | Pas de dépendance externe (Arduino libs tierces) | 0 |
| NF-04 | Compatible ESP32 (ESP-IDF + Arduino) | 0 |
| NF-05 | Compatible ESP8266 (best effort) | 1 |

## 5. Matériel de référence

| Composant | Modèle | GPIO/I2C | Notes |
|-----------|--------|----------|-------|
| MCU | ESP32-S3 | — | pool-firmata-wifi |
| ADC | ADS1115 | I2C 0x48 | 4 canaux 16-bit |
| Pression | Capteur 0-5V | A0 | Via ADS1115 |
| pH | Sonde + ampli | A2 | Via ADS1115 |
| ORP | Sonde + ampli | A3 | Via ADS1115 |
| Eau Tw | DS18B20 `0xc802...` | GPIO17 | Immergé |
| Air pompe | DS18B20 `0x7001...` | GPIO13 | HA Air |
| Air local | DS18B20 `0x4401...` | GPIO13 | HA Air2 |

## 6. Matrice de parité j5

| Fonctionnalité j5 | pool_station | Lot |
|-------------------|--------------|-----|
| Linear calibration | ✅ | 2 |
| Multi-point | ✅ | 2 |
| Scale/offset | ✅ | 2 |
| Median filter | ✅ | 3 |
| freq sampling | ✅ | 1 |
| threshold events | ✅ | 4 |
| freeform calibration | ✅ (N-points) | 2 |

## 7. Roadmap des Lots

| Lot | Contenu | Status |
|-----|---------|--------|
| 0 | Squelette, docs, sensor stub | ✅ Done |
| 1 | ADS1115 binding, raw values, cal mode switch | ✅ Done |
| 2 | N-point calibration, Capturer, persistence | ✅ Done |
| 3 | Filters (j5-like median, jump, clamp) | ✅ Done |
| 4 | Diagnostics (noise σ/ptp, stuck, flags) | ✅ Done |
| 5 | Water temp compensation (Tw) | ✅ Done |
| 6 | Gates/campaigns | ✅ Done |
| 7 | HA polish, runtime algo select, draft/Save dirty UX | ✅ Done |
| **8** | **Interference detection (orp↔pressure, ph↔orp, optional pH↔Tw)** | ✅ **Current** |

## 8. Critères d'acceptation

### Lot 0 (ce lot)

- [ ] Structure `components/pool_station/` valide ESPHome
- [ ] Codegen Python avec CONFIG_SCHEMA
- [ ] C++ compile (header + cpp)
- [ ] Log tag `pool_station` visible
- [ ] Au moins un sensor stub enregistrable
- [ ] Docs: CDC, SENSOR-IDENTITY, README
- [ ] Exemple YAML fonctionnel (structure)
- [ ] **JAMAIS d'inversion de rôle capteur** (anti-swap)

### Lots suivants

- Tests unitaires C++ (Lot 2+)
- Validation calibration vs j5 (Lot 2)
- Benchmarks latence (Lot 3)
- Tests longue durée drift (Lot 4)

## 9. Références

- [ESPHome External Components](https://esphome.io/components/external_components.html)
- [MitsubishiCN105ESPHome](https://github.com/echavet/MitsubishiCN105ESPHome) — modèle de référence
- [Johnny-Five Sensor API](http://johnny-five.io/api/sensor/)
- [DFRobot SEN0165 ORP](https://wiki.dfrobot.com/Analog_ORP_Meter_SKU_SEN0165_)
