# Investigation: Régression Wi-Fi / API v0.10.0–v0.10.3

**Date**: 2026-09-20  
**Firmware concerné**: pool_station v0.10.0–v0.10.3 (Lot A/B: runtime ADS gain + filter params)  
**Appareil**: ESP32-S3 (`pool-station`)  
**ESPHome**: 2026.4.3

## Symptômes Rapportés

- ESP intermittent hors ligne, entités HA indisponibles
- Handshake API échoue souvent même quand TCP se connecte brièvement
- Perte de ping / flapping réseau
- RSSI fort (~-34 dBm) → pas un problème de signal faible
- Le dual-SSID Wi-Fi est **explicitement exclu** comme cause racine (autres ESP fonctionnent)

---

## Candidats de Cause Racine (Classés par Probabilité)

### 🔴 1. NVS Write Storms — HAUTE PROBABILITÉ

**Fichiers concernés**: 
- `pool_station.cpp` lignes 728-770 (`save_runtime_preferences()`)
- `pool_station.cpp` lignes 920-930 (`persist_filters_if_()`, `persist_interval_if_()`)

**Analyse**:

Chaque modification d'un paramètre Lot A/B depuis Home Assistant déclenche une écriture NVS synchrone et bloquante :

```cpp
// pool_station.cpp:769
this->runtime_prefs_.save(&data);  // NVS FLASH WRITE — BLOQUANT 20-100ms
```

Les méthodes `apply_*_runtime(value, persist=true)` appellent `save_runtime_preferences()` :

```cpp
// pool_station.cpp:673-677
if (persist) {
  this->save_runtime_preferences();  // ← Écriture NVS à chaque changement HA
}
```

**Impact**:
- L'écriture NVS sur ESP32 prend **20-100ms** et **bloque complètement le CPU**
- Durant ce temps, la stack Wi-Fi ne peut pas traiter les paquets → timeouts API
- Avec 3 canaux × 6 widgets Lot B (filter_samples, max_jump, max_jump_streak, value_min, value_max, update_interval), un ajustement rapide peut déclencher **plusieurs écritures consécutives**
- Le problème est amplifié si HA fait des appels de synchronisation au démarrage

**Code v0.8.0 vs v0.10.x**:
- v0.8.0: Pas de `runtime_prefs_key_`, pas d'écriture NVS à chaque changement de paramètre
- v0.10.x: Chaque `apply_gain_runtime()`, `apply_filter_samples_runtime()`, etc. avec `persist=true` écrit en NVS

**Preuves**:
```cpp
// pool_station.cpp:978-984
void PoolStationChannelSensor::apply_filter_samples_runtime(uint8_t n, bool persist) {
  this->filter_config_.filter_samples = ads_runtime::clamp_filter_samples(n);
  this->sync_median_window_();
  this->persist_filters_if_(persist);  // ← NVS WRITE si persist=true
  // ...
}
```

---

### 🔴 2. Logging DEBUG Intensif — HAUTE PROBABILITÉ

**Fichiers concernés**: 
- `pool_station.cpp` (130 appels ESP_LOG*)
- `examples/pool-station-minimal.yaml` lignes 60-65

**Analyse**:

L'exemple YAML active explicitement le niveau DEBUG :

```yaml
logger:
  level: DEBUG
  initial_level: INFO
  logs:
    pool_station: DEBUG
```

Avec ce niveau, chaque échantillon de capteur génère plusieurs lignes de log :

```cpp
// pool_station.cpp:1413
ESP_LOGV(TAG, "Channel %s: raw=%.4f, filtered=%.4f, cal=%.3f, comp=%.3f, guarded=%.3f (type=%s)",
         this->get_channel_type_name(), value, filtered_raw, calibrated, compensated, guarded,
         this->calibration_.get_type_name());
```

**Impact**:
- Le logging série sur ESP32 est **synchrone et bloquant**
- Avec 3 canaux à 1Hz, ~10-20 lignes de log par seconde
- Les logs INFO de Lot A/B (gain changed, filter applied, etc.) s'ajoutent

---

### 🟠 3. Heap Fragmentation via Median Filter — PROBABILITÉ MOYENNE

**Fichiers concernés**:
- `channel_filter.cpp` lignes 38-52 (`SlidingWindow::median()`)
- `channel_diagnostics.cpp` ligne 51

**Analyse**:

Chaque appel à `median()` ou `sigma()` crée un `std::vector<float>` temporaire :

```cpp
// channel_filter.cpp:38-52
float SlidingWindow::median() const {
  // ...
  std::vector<float> valid;           // ← Allocation heap
  valid.reserve(this->count_);        // ← Peut fragmenter
  for (size_t i = 0; i < this->count_; i++) {
    // push_back...
  }
  return filter_functions::compute_median(std::move(valid));
}
```

**Impact**:
- Avec 3 canaux utilisant median (filter_samples > 1), ~3 allocations/secondes
- Fragmentation progressive du heap → instabilité à long terme
- L'ESP32-S3 a ~320KB de RAM, mais la fragmentation peut épuiser la mémoire disponible avant d'atteindre la limite

---

### 🟠 4. État API Publishing Storm — PROBABILITÉ MOYENNE

**Fichiers concernés**:
- `pool_station.cpp` — 64 appels `publish_state`
- Lot A ajoute: `saturated_sensor_`, `fsr_percent_sensor_`, `gain_mismatch_sensor_`

**Analyse**:

Chaque canal avec `ads:` configuré ajoute plusieurs entités qui publient à chaque lecture :

```cpp
// pool_station.cpp:826-833
if (this->saturated_sensor_ != nullptr) {
  this->saturated_sensor_->publish_state(this->sat_published_);
}
if (this->fsr_percent_sensor_ != nullptr) {
  this->fsr_percent_sensor_->publish_state(this->get_fsr_percent(raw));
}
```

**Impact**:
- 3 canaux × ~4 entités supplémentaires = 12+ publications par cycle
- Chaque publication génère du trafic API vers HA
- Le buffer d'envoi peut saturer

---

### 🟡 5. I2C Bus Contention avec ADS1115 — PROBABILITÉ FAIBLE

**Fichiers concernés**:
- `pool_station.cpp` lignes 649-655 (`apply_ads_gain_to_source_()`)

**Analyse**:

```cpp
void PoolStationChannelSensor::apply_ads_gain_to_source_() {
#ifdef USE_ADS1115
  if (this->ads_ != nullptr && ads_runtime::is_managed_gain(this->gain_shadow_)) {
    this->ads_->set_gain(static_cast<ads1115::ADS1115Gain>(this->gain_shadow_));
  }
#endif
}
```

L'appel à `set_gain()` fait une écriture I2C. Si le bus I2C est occupé ou si un périphérique ne répond pas, cela peut bloquer.

---

## Tests Diagnostiques Recommandés

### Test 1: Désactiver le Lot A/B Overlay (le plus rapide)

Dans le YAML, commenter temporairement toutes les sections `ads:` et `filters.runtime:` :

```yaml
ph:
  source_id: ads_ph
  # ads:                    # ← Commenter
  #   gain: 6.144
  #   ...
  filters:
    filter_samples: 5
    # runtime:              # ← Commenter
    #   persist: true
    #   ...
```

**Résultat attendu**: Si les problèmes disparaissent, cela confirme que les écritures NVS sont la cause.

### Test 2: Réduire le Niveau de Log

```yaml
logger:
  level: WARN              # ← Changer de DEBUG à WARN
  logs:
    pool_station: WARN     # ← Réduire le verbosité
```

### Test 3: Désactiver les Entités Lot A Secondaires

Ne garder que `gain_select` et retirer `saturated`, `fsr_percent`, `gain_mismatch` :

```yaml
ads:
  gain: 6.144
  gain_select:
    name: "pH ADS Gain"
  # saturated: ...         # ← Commenter
  # fsr_percent: ...       # ← Commenter
  # gain_mismatch: ...     # ← Commenter
```

### Test 4: Augmenter l'Update Interval

```yaml
ph:
  update_interval: 120s    # ← Augmenter de 60s à 120s
```

---

## Corrections Recommandées

### Fix 1: Différer les Écritures NVS (PRIORITÉ HAUTE)

Implémenter un mécanisme de "dirty flag + delayed write" au lieu d'écrire immédiatement :

```cpp
// Dans pool_station.h
uint32_t nvs_dirty_since_ms_{0};
bool nvs_dirty_{false};

// Dans apply_*_runtime()
void PoolStationChannelSensor::apply_filter_samples_runtime(uint8_t n, bool persist) {
  this->filter_config_.filter_samples = ads_runtime::clamp_filter_samples(n);
  this->sync_median_window_();
  if (persist && this->filters_persist_) {
    this->mark_nvs_dirty_();  // ← Marquer dirty au lieu d'écrire
  }
  // ...
}

// Dans loop()
void PoolStationChannelSensor::loop() {
  // ...
  this->flush_nvs_if_idle_();  // Écrire après 5s d'inactivité
}
```

### Fix 2: Pré-allouer les Buffers Median (PRIORITÉ MOYENNE)

```cpp
// Dans channel_filter.h
class SlidingWindow {
  // ...
  mutable std::vector<float> sort_buffer_;  // Réutiliser entre appels
};

float SlidingWindow::median() const {
  this->sort_buffer_.clear();
  this->sort_buffer_.reserve(this->size_);  // Ne réalloue pas si size <= capacity
  // ...
}
```

### Fix 3: Rate-Limit les Publications Lot A (PRIORITÉ BASSE)

Ne publier `fsr_percent` et `saturated` que si la valeur a changé de façon significative :

```cpp
if (this->fsr_percent_sensor_ != nullptr) {
  float new_pct = this->get_fsr_percent(raw);
  if (std::abs(new_pct - this->last_fsr_pct_) > 0.5f) {
    this->fsr_percent_sensor_->publish_state(new_pct);
    this->last_fsr_pct_ = new_pct;
  }
}
```

---

## Résumé Exécutif

| # | Candidat | Probabilité | Impact Wi-Fi | Test |
|---|----------|-------------|--------------|------|
| 1 | NVS Write Storms | HAUTE | **Critique** — bloque CPU 20-100ms | Commenter `ads:` + `filters.runtime:` |
| 2 | DEBUG Logging | HAUTE | Élevé — bloque série | `level: WARN` |
| 3 | Heap Fragmentation | MOYENNE | Graduel — instabilité | Monitorer heap libre |
| 4 | API Publish Storm | MOYENNE | Modéré — saturation buffer | Retirer entités secondaires |
| 5 | I2C Contention | FAIBLE | Ponctuel | Pas de test simple |

**Recommandation immédiate**: Tester en commentant `ads:` et `filters.runtime:` dans le YAML. Si cela résout le problème, le fix #1 (delayed NVS writes) devrait être implémenté.

---

## Références Code

- `components/pool_station/pool_station.cpp:728-770` — `save_runtime_preferences()`
- `components/pool_station/pool_station.cpp:673-677` — NVS write on gain change
- `components/pool_station/pool_station.cpp:978-984` — NVS write on filter change
- `components/pool_station/channel_filter.cpp:38-52` — Heap allocation in median()
- `components/pool_station/ads_runtime.h:12` — `CHANNEL_RUNTIME_PREFS_MAGIC`
