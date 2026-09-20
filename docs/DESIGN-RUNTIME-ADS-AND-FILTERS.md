# Design — ADS1115 / filtres dynamiques au runtime

> **Statut** : Lot A **implémenté** en v0.9.0 (usage : [MIGRATION](MIGRATION.md#runtime-ads-gain-lot-a-v090)). Lot B **implémenté** en v0.10.0 (usage : [MIGRATION](MIGRATION.md#runtime-filters-lot-b-v0100)). Lot C NO-GO.  
> **Décideur** : Eric Chavet.  
> **Cible** : ESPHome **2026.x** (`ads1115` lu sur `dev` / docs 2026, API identique à 2026.8).  
> **Repo** : `echavet/esphome-pool-station` @ Lot 8 / v0.8.1.  
> **Contexte live** : rail pH ≈ 4.094 V @ gain 4.096 → OTA `gain: 6.144` (voir [PH-J5-VS-POOL-STATION-ADS.md](PH-J5-VS-POOL-STATION-ADS.md) si fusionné, PR #28).

Ce document doit permettre un **go / no-go par lot** sans nouvelle passe d’archéologie.

---

## 0. Décision (lire en premier)

### Réponse à la question d’Eric

> *Pourquoi le composant ne gérerait-il pas les paramètres ADS dynamiquement par source, au lieu de seulement `platform: ads1115` en YAML ?*

**Il devrait les gérer — en overlay runtime, pas en réécrivant le driver.**

ESPHome 2026.x applique déjà **mux + gain + résolution + SPS à chaque conversion**. Les setters C++ publics existent. Ce qui manque, c’est : entités HA, persistance NVS, lecture du gain courant (pas de getter), diagnostic de saturation, verrou anti-cal-pourrie.

> *Et configurer les filtres dynamiquement ?*

**Oui, Lot B.** Les setters Lot 3 existent déjà (`set_filter_samples`, `set_max_jump`, …) mais ne sont appelés qu’au codegen. Ce n’est pas un rewrite : c’est exposer + persister + redimensionner la fenêtre.

### Recommandation

| Lot | Contenu | Verdict | Effort | Risque |
|-----|---------|---------|--------|--------|
| **A** | Gain PGA runtime + saturation ≥ 98 % FSR + mismatch gain≠cal | **GO** | Moyen : overlay typé `ADS1115Sensor*`, 1 select, 2 binary, 1 sensor, prefs sidecar, lock cal-mode | Moyen : couplage header ESPHome, 1er échantillon YAML, VDD vs FSR |
| **B** | Filtres Lot 3 + `update_interval` canal HA-tunables | **GO** (après A) | Moyen : 5–6 numbers + resize fenêtre + même blob NVS | Bas–moyen : explosion d’entités, cold-start médiane |
| **C** | Ownership ADS (scheduler mux, raw counts, continuous_mode) | **NO-GO** sauf blocage de A | Haut : fork du driver, casse le compose | Haut |

**Garder le modèle compose** (`source_id` + `platform: ads1115`). Lot C seulement si `set_gain()` ne suffit pas en pratique (régression ESPHome, besoin de comptes bruts, ou multiplexage cassé).

### Alternatives (une ligne)

| Idée | Verdict |
|------|---------|
| Status quo YAML + reflash pour le gain | Rejeté — c’est déjà arrivé (OTA 4.096 → 6.144) |
| Lambda HA `id(ads_ph).set_gain(...)` | Stopgap possible **aujourd’hui**, pas une archi |
| Invalider / rescaler la cal si le gain change | Rejeté — la cal est en **volts** |
| Mettre gain/filtres dans `CalibrationPrefsData` | Rejeté — bump magic, recals forcés |
| Filtres dans le draft/Save Capturer | Rejeté — ce n’est pas de la calibration |
| `continuous_mode: true` pour aller plus vite | Rejeté — 1 ADS partagé pH/ORP/pression |
| Select mux (A0/A2/A3) dans HA | Rejeté — anti-swap |
| Réécrire Firmata / j5 | Hors scope |

---

## 0b. English recommendation (short)

Keep composing native `platform: ads1115` sensors. ESPHome 2026.x already programs PGA / mux / SPS **per conversion** and converts raw→volts with the **current** gain, so chemistry calibration in volts does **not** rescale when gain changes. pool_station should add an **opt-in overlay**: typed `ADS1115Sensor*`, HA select for gain, saturation `binary_sensor` at ~98 % FSR, sidecar NVS (do **not** bump cal magic). Soft-lock gain while Calibration Mode is ON; do not dirty the Capturer draft. Lot B exposes existing Lot 3 filter setters as HA numbers. Lot C (own the ADC) is unnecessary unless Lot A is blocked. Do not HA-tune the multiplexer. Shared ADS must stay single-shot.

---

## 1. Archéologie — ce qui existe aujourd’hui

### 1.1 Binding Lot 1 (compose, pas ownership)

```text
YAML  sensor.platform: ads1115  →  ADS1115Sensor (volts)
         ↑ source_id
pool_station channel            →  sensor::Sensor* only
```

- Codegen : `set_source_sensor(source)` — type **générique** `sensor::Sensor`.
- Aucun `#include` ads1115, `DEPENDENCIES: []`.
- Gain / mux / SPS vivent **uniquement** dans le YAML source (`examples/pool-station-minimal.yaml` : `gain: 4.096`, `continuous_mode: false`, mux `A0_GND` / `A2_GND` / `A3_GND`).
- Le canal **n’a aucun moyen** d’appeler `set_gain()` sans pointeur typé.

### 1.2 Pipeline canal (`on_source_value_`)

```text
source volts
  → throttle (configured_interval_ ; cal-mode → calibration_interval_ 1 s)
  → gate (bypass cal-mode)
  → médiane filter_samples (bypass cal-mode)
  → raw_sensor
  → calibrate(live)          # draft ignoré
  → temp_comp
  → calibrated_sensor        # pré-garde
  → clamp value_min/max
  → jump guard (bypass + reset cal-mode)
  → publish main
  → diagnostics (valeur gardée)
  → Lot 8 push (valeur compensée, pré-jump-guard)
```

Points durs pour ce design :

- `set_filter_*` existent mais ne sont **pas** runtime-safe pour la médiane : `SlidingWindow::set_size()` **clear** le buffer.
- `configured_interval_` est déjà un int mutable — HA-tunable = trivial.
- Le **source** ADS continue de sampler à **son** `update_interval` (souvent 1 s) même si le canal est à 60 s. Cal-mode n’accélère que le throttle canal, pas le PollingComponent ADS (déjà à 1 s dans l’exemple).
- Lot 8 lit **avant** `max_jump` : rendre `max_jump` dynamique ne cache **pas** les spikes pompe.

### 1.3 Calibration / NVS

- Cal en **volts** (`CalibrationPoint.x`).
- Clé : `ps-md5-v1:{component_id}:{channel_type}` (`prefs_key.py`).
- Struct `CalibrationPrefsData`, magic `0xCA110005` (Lot 5 Tw) / legacy `0xCA110002`.
- **Aucun** champ gain, SPS, filtre.
- Draft/Save (Lot 7) : edits Capturer → draft ; live + flash au Save valide.
- v0.7.16 : bump de schéma de clé = **recal one-shot**. Ne pas recommencer.

### 1.4 j5_ha_bridge (Firmata) — ce qui était configurable

Schema addon (`config.yaml`) **par capteur** :

| Clé j5 | Sens | pool_station aujourd’hui |
|--------|------|--------------------------|
| `filter_samples` | Médiane glissante N (YAML only) | `filters.filter_samples` (YAML only) |
| `max_jump` | Rejet saut, unité **calibrée** | idem, unité calibrée |
| `max_jump_streak` | Défaut j5 **5** | Défaut **3** |
| `value_min` / `value_max` | **Rejet** (last-good) | **Clamp** (modifie la valeur) |
| `freq` | Intervalle J5 (ms) | `update_interval` canal + source |
| `publish_raw` / `publish_calibrated` | Companions | `raw_sensor` / `calibrated_sensor` |
| PGA / gain ADS | N/A (ADC Uno 10-bit 5 V) | YAML `gain:` figé |

j5 n’exposait **pas** non plus ces clés en entités HA number. La demande runtime est **nouvelle**, pas une parité j5 stricte.

Piège à ne pas « corriger » en Lot B : passer du clamp au rejet j5 changerait la sémantique des installs existantes (`value_max: 14` clampe, ça ne garde pas last-good).

---

## 2. Capacités ESPHome 2026.x `ads1115` (faits)

Sources lues :  
`esphome/components/ads1115/{ads1115.h,ads1115.cpp}`  
`esphome/components/ads1115/sensor/{ads1115_sensor.h,ads1115_sensor.cpp,__init__.py}`  
[docs sensor/ads1115](https://esphome.io/components/sensor/ads1115/).

### 2.1 Hub vs sensor

| Objet | Rôle | Runtime aujourd’hui |
|-------|------|---------------------|
| `ADS1115Component` (hub I²C) | `continuous_mode_`, `request_measurement(mux, gain, res, sps)` | `set_continuous_mode()` au setup **seulement**. Pas d’entité HA. |
| `ADS1115Sensor` | Stocke mux/gain/res/sps ; `sample()` les passe au hub | Setters **publics** : `set_gain`, `set_multiplexer`, `set_resolution`, `set_samplerate`. **Pas de getters** (`gain_` etc. sont `protected`). |

Conversion (extrait sémantique) :

```text
millivolts = signed_raw * FSR_mV / 32768     # 16 bits
return millivolts / 1000                     # volts
```

Le volt publié **suit le gain courant**. Changer 4.096 → 6.144 **ne multiplie pas** les X de calibration.

### 2.2 Ce que YAML fixe vs ce qu’on peut changer après boot

| Paramètre | YAML | Setter runtime | Getter | HA natif | Notes |
|-----------|------|----------------|--------|----------|-------|
| **gain / PGA** | requis | `set_gain(ADS1115Gain)` | **non** | non | Appliqué à la **prochaine** conversion |
| **sample_rate** | défaut `860` | `set_samplerate` | **non** | non | 8 SPS = **delay 126 ms** bloquant / conversion |
| **multiplexer** | requis | `set_multiplexer` | **non** | non | Ne **jamais** exposer (anti-swap A0/A2/A3) |
| **resolution** | défaut 16 bits | `set_resolution` | **non** | non | ADS1015 only ; ignorer |
| **continuous_mode** | hub, défaut `false` | setup only | non | non | **Rester false** si 1 ADS / 3 voies |
| **update_interval** source | PollingComponent, ex. 1 s | `PollingComponent::set_update_interval` | oui (get) | non | Distinct du throttle canal |

### 2.3 Multiplexage — un ADC, trois voies

Le chip n’a **qu’un** convertisseur. ESPHome sérialise :

```text
ads_pressure.update() → request_measurement(A0, gain_P, …)  [bloque 2–126 ms]
ads_ph.update()       → request_measurement(A2, gain_H, …)
ads_orp.update()      → request_measurement(A3, gain_O, …)
```

Chaque appel **réécrit** le registre CONFIG (mux+gain+SPS). Les gains **peuvent différer par voie** sans ownership. C’est déjà le modèle (pression 4.096, pH 6.144 après OTA).

Contraintes :

1. **Single-shot** (`continuous_mode: false`) : correct pour ce partage. En continu, un seul mux « gagne » ; ESPHome réécrit si `prev_config_` change, mais le mode n’apporte rien ici.
2. **SPS bas** : 3 × 126 ms ≈ 378 ms de `delay()` dans la loop si les 3 pollent ensemble. Interdit en dessous de 128 SPS sur un hub partagé (warning codegen).
3. **Pas de file d’attente** à ajouter : PollingComponent + I²C sync suffisent à 860 SPS / 1 s (≈ 6 ms/s).

### 2.4 PGA, LSB, saturation

| Gain YAML | Enum | FSR (V) | LSB 16-bit | 98 % FSR |
|-----------|------|---------|------------|----------|
| `6.144` | `ADS1115_GAIN_6P144` | 6.144 | 187.5 µV | 6.021 V |
| `4.096` | `ADS1115_GAIN_4P096` | 4.096 | 125 µV | 4.014 V |
| `2.048` | `ADS1115_GAIN_2P048` | 2.048 | 62.5 µV | 2.007 V |
| `1.024` | `ADS1115_GAIN_1P024` | 1.024 | 31.25 µV | 1.004 V |
| `0.512` / `0.256` | … | 0.512 / 0.256 | 15.6 / 7.8 µV | rarement utile ici |

Live Eric : **4.094 / 4.096 = 99.95 % FSR** — un seuil 98 % aurait crié.

Datasheet (rappel docs ESPHome) : **AIN ≤ VDD + 0.3 V**.  
- ADS en **5 V** : un isolateur 0–5 V est lisible en gain **6.144** (seul PGA ≥ 5 V).  
- ADS en **3.3 V** : AIN max ≈ 3.6 V ; gain 6.144 **n’autorise pas** 5 V sur la broche. Le diagnostic saturation ≠ protection hardware.

Changer le gain **change la résolution**, pas l’échelle chimique. Un LSB plus gros (6.144) est le prix du headroom. Pour pH (mV de sonde via module), 187 µV reste largement assez.

### 2.5 Thin wrapper vs ownership

| Besoin | Suffit `set_gain` + overlay | Exige Lot C |
|--------|------------------------------|-------------|
| Changer PGA sans reflash | Oui | Non |
| Volt toujours juste après changement | Oui (formule FSR/32768) | Non |
| Saturation 98 % FSR | Oui **si on track le gain** | Raw counts plus jolis, pas nécessaires |
| Lire le gain actuel | Overlay **shadow** (pas de getter) | Ou patch upstream ESPHome |
| SPS runtime | Oui (`set_samplerate`) | Non |
| Mux runtime | Possible mais **interdit** produit | Non |
| `continuous_mode` runtime | Non (hub, setup) | Oui, inutile |
| Scheduler / priorité voies | Non | Oui, inutile à 1 Hz |
| Compte ADC 16-bit exposé | Non (ESPHome ne publie que des volts) | Oui |

**Conclusion §2** : Lot A = wrapper mince autour de setters **déjà publics**. Pas de fork.

---

## 3. Lot A — Gain + saturation (GO)

### 3.1 Objectif

Depuis HA, par canal ADS :

1. Choisir le PGA (`6.144` … `0.256`).
2. Voir **% FSR** et un flag **saturé** (`device_class: problem`).
3. Persister le gain (OTA / reboot).
4. Ne pas casser une cal en volts ; empêcher de **capturer au rail**.

### 3.2 Overlay (C++)

Ne pas ajouter `DEPENDENCIES = ["ads1115"]` au composant (casse un `source_id` non-ADS futur). Activer seulement si le bloc `ads:` est présent :

```cpp
// pool_station.h — conditionné
#ifdef USE_ADS1115
#include "esphome/components/ads1115/sensor/ads1115_sensor.h"
#endif

void set_ads1115_sensor(ads1115::ADS1115Sensor *ads);
void apply_gain_runtime(ads1115::ADS1115Gain gain);  // set_gain + shadow + persist + sat recalc
float get_fsr_volts() const;
float get_fsr_percent(float raw_v) const;
bool is_adc_saturated(float raw_v) const;
ads1115::ADS1115Gain get_gain_shadow() const;
```

Codegen : si `ads:` présent, `source_id` **doit** être un `ADS1115Sensor` (`cv.use_id` typé ou validateur). Sinon erreur de config, pas un `static_cast` silencieux.

`USE_ADS1115` est déjà défini par ESPHome quand le hub est dans le YAML (install Eric : toujours).

Shadow obligatoire : sans getter upstream, le canal est **source de vérité** après `setup()` :

1. Seed = YAML `ads.gain` **ou**, à défaut, YAML du sensor source (si on le recopie en codegen) **ou** 4.096 documenté.
2. NVS overlay gagne si magic valide.
3. `apply_gain_runtime` → `ads_->set_gain(...)` **avant** le prochain `update()` source.

Course au boot : le premier `ADS1115Sensor::update()` peut partir avec le gain YAML source **avant** `channel.setup()`. Mitigation :

- `setup_priority` canal déjà `DATA - 1` (après le source). Inverser n’aide pas : le source a pu poller.
- Acceptable : **1 échantillon** au gain YAML. Documenter.
- Option plus propre (A.1) : `ads_ph.internal: true` + `update_interval: never` / très long, et le canal appelle `ads_->sample()` lui-même. **Plus invasif** — pas Lot A MVP. Le throttle actuel **écoute** le callback ; ne pas changer ça dans A.

### 3.3 Saturation / santé — UX HA

**Détection** (volts, pas counts) :

```text
ON  si |raw| ≥ sat_on  * FSR     # défaut sat_on  = 0.98
OFF si |raw| ≤ sat_off * FSR     # défaut sat_off = 0.95
hold_time 15 s (comme Lot 8, pour que HA voie le one-shot)
```

Utiliser **`last_raw_value_`** (pré-médiane). Une médiane de rails reste un rail ; un spike unique au rail doit quand même alerter.

Entités (opt-in, comme Lot 4) :

| YAML | Type | `entity_category` | Rôle |
|------|------|-------------------|------|
| `ads.gain_select` | `select` | `config` | Options `"6.144"`, `"4.096"`, `"2.048"`, `"1.024"`, `"0.512"`, `"0.256"` |
| `ads.saturated` | `binary_sensor` | `diagnostic` | `device_class: problem` |
| `ads.fsr_percent` | `sensor` | `diagnostic` | `100 * |raw| / FSR`, unité `%` |
| `ads.gain_mismatch` | `binary_sensor` | `diagnostic` | gain courant ≠ gain au dernier **Save** cal |

Lovelace recommandé :

- Badge / carte conditionnelle : `saturated` ON → « ADC saturé — élargir le gain ou vérifier l’isolateur ; **ne pas Capturer / Save** ».
- Automatisation HA : notify si `saturated` 30 s (sel / électrolyseur **après** le rail, cf. PR #28).
- Ne **pas** auto-changer le gain. L’humain choisit 6.144 (pression 0–5 V : seul PGA tenable).

`out_of_range` Lot 4 est en **unité chimique** (pH 0–14). Ce n’est **pas** un substitut : 8.709 pH passait dans 0–14 pendant que l’ADC était à 99.95 % FSR.

### 3.4 SPS / continuous — Lot A

| Paramètre | Lot A |
|-----------|--------|
| `sample_rate` select | **Optionnel A.1**, pas MVP. Si oui : interdire 8/16/32 SPS quand ≥2 sensors partagent le hub (validation codegen). Défaut rester 860. |
| `continuous_mode` | **Non exposé.** Documenter : laisser `false`. |
| `resolution` | Non. |
| mux | Non. |

### 3.5 Lock calibration

Voir §6. Lot A **soft-lock** le select gain tant que Calibration Mode = ON (write refusée + log). Déverrouillage à la sortie du mode, même sans Save.

### 3.6 Tests Lot A

- Table FSR / 98 % (purs, comme `tests/test_calibration_filter.py`).
- Hystérésis 98/95.
- Clé NVS runtime ≠ clé cal (`test_prefs_key.py`).
- Validateur : `ads:` + `source_id` non-ADS → erreur.

---

## 4. Lot B — Filtres dynamiques (GO après A)

### 4.1 Quoi exposer

| Paramètre | HA | Pourquoi | Interaction |
|-----------|----|----------|-------------|
| `filter_samples` | **number** 0–20, step 1 | j5 + Lot 3 ; tuner le lissage sans reflash | `set_size()` **clear** → trou médiane jusqu’à N samples. En cal-mode la médiane est déjà bypass. |
| `max_jump` | **number** ≥ 0, step selon canal (pH 0.05, ORP 1, bar 0.01) | j5 | 0 = off. Lot 8 **non impacté** (pré-garde). Reset `JumpGuard` au changement. |
| `max_jump_streak` | **number** 1–10 | j5 | Inutile si `max_jump == 0`. |
| `value_min` / `value_max` | **number** optionnels | j5 / Lot 3 | **Rester clamp**, pas rejet. `nan` = off : UI « supprimer borne » = number à une sentinelle ou switch `clamp_enable`. Plus simple : omettre les numbers si pas dans YAML ; si présents, `mode: box`. |
| `update_interval` **canal** | **number** (s) 1–3600 | cal-mode 1 s reste prioritaire | N’impose pas le polling ADS. Gates : un intervalle plus long retarde juste le prochain sample autorisé. |
| `calibration_interval` | **non** | YAML station | Un seul switch cal-mode suffit. |
| `noise_window` / seuils Lot 4 | **non** Lot B | Autre lot | — |
| seuils Lot 8 | **non** Lot B | YAML station | — |

**Select** : aucun nécessaire (pas d’enum).  
**Switch** : optionnel `filters_enable` (tout off) — **non** MVP ; `filter_samples: 0` + `max_jump: 0` suffisent.

Bouton `filters.reset_yaml` : recharge les seeds YAML (RAM + NVS). Utile après un tunning foireux.

### 4.2 Draft / Save / gates / Lot 8

| Surface | Règle Lot B |
|---------|-------------|
| Capturer draft | **Ne pas dirty**. Les filtres ne sont pas des points de cal. |
| Save cal | N’écrit **pas** les filtres. Inversement, changer un filtre n’appelle pas `commit_draft`. |
| Cal-mode | Médiane + jump **déjà bypass**. Changer les numbers en cal-mode est OK (prend effet à la sortie). **Ne pas** lock les filtres. |
| Gates | Aucune sémantique partagée. |
| Lot 8 | Samples toujours `compensated`. `max_jump` dynamique ne masque pas l’EMI. Médiane plus large **lisse** le raw → cal → compensated : un `filter_samples` énorme **peut** atténuer un spike vu par Lot 8. Documenter ; pas de hold spécial. |
| Diagnostics Lot 4 | Tourne sur la valeur **gardée**. Un `max_jump` plus serré baisse ptp/σ publiés (comportement actuel). |

### 4.3 Runtime apply (C++)

```cpp
void apply_filter_samples_runtime(uint8_t n);  // config + set_size (clear)
void apply_max_jump_runtime(float j);          // config + JumpGuard + reset()
void apply_max_jump_streak_runtime(uint8_t s);
void apply_value_min_runtime(float v);         // NAN = disable
void apply_value_max_runtime(float v);
void apply_update_interval_runtime(uint32_t ms);
void reset_filters_to_yaml();
```

Aujourd’hui `set_filter_samples` ne touche **pas** `raw_window_` (seulement `setup()` le fait). C’est le trou à fermer en Lot B : d’où des méthodes `apply_*` distinctes des setters codegen.

### 4.4 `update_interval` source ADS

Hors MVP. Si on veut économiser l’I²C à 60 s : en quittant cal-mode, `ads_->set_update_interval(channel_interval)` ; en entrant, forcer 1 s. Risque : cal-mode « mou » si on oublie de accélérer le source. L’exemple actuel (source 1 s, canal 60 s) est déjà le bon compromis.

---

## 5. Persistance

### 5.1 Deux stores, deux clés

| Store | Clé | Magic | Contenu |
|-------|-----|-------|---------|
| Cal (existant) | `ps-md5-v1:{id}:{ch}` | `0xCA110005` | type, points, DFRobot, Tw |
| **Runtime (nouveau)** | `ps-md5-rt-v1:{id}:{ch}` | `0xA0511115` (« ADS» + version nibble) | gain, filtres, intervalle, `gain_at_last_cal_save` |

Helper : `generate_runtime_preferences_key()` dans `prefs_key.py`. **Ne pas** réutiliser la clé cal (même taille de blob = collision silencieuse).

### 5.2 Struct (taille fixe, packed)

```cpp
struct ChannelRuntimePrefsData {
  uint32_t magic;              // 0xA0511115
  uint8_t  version;            // 1
  uint8_t  ads_gain;           // ADS1115Gain ou 0xFF = unmanaged
  uint8_t  ads_sps;            // 0xFF = unmanaged (A.1 / C)
  uint8_t  filter_samples;
  uint8_t  max_jump_streak;
  uint8_t  flags;              // bit0 has_vmin, bit1 has_vmax, bit2 has_gain, …
  uint8_t  pad_align[2];       // floats 4-byte aligned (ESP32-C3); sizeof=32
  float    max_jump;
  float    value_min;
  float    value_max;
  uint32_t update_interval_ms;
  uint8_t  gain_at_last_cal_save;  // 0xFF = never saved since overlay
  uint8_t  reserved[3];
};
```

Lot A n’écrit que gain + flags + `gain_at_last_cal_save`. Lot B remplit le reste. Même magic/version : champs 0xFF / NAN = « pas géré ».

**Ne pas** étendre `CalibrationPrefsData` : ça force un dual-read et un récit v0.7.16.

### 5.3 YAML vs NVS vs OTA

| Événement | Comportement |
|-----------|----------------|
| Premier boot overlay (pas de NVS rt) | Seeds YAML (`ads.gain`, `filters.*`, `update_interval`) |
| Reboot / OTA (NVS intacte) | NVS **gagne** (comme la cal) |
| Changement YAML gain + flash, NVS déjà là | NVS **gagne** — piège. Bouton `ads.reset_yaml` + doc MIGRATION |
| `pool_station.id` changé | Nouvelle clé → retour YAML (comme la cal) |
| Bump `ps-md5-rt-v1` → v2 | One-shot retour YAML ; **cal intacte** |
| Effacement flash | YAML |

Persistance : **auto-save NVS à chaque apply runtime** (gain ou filtre). Pas de second « Save » Capturer. Les tunables opérationnels doivent survivre à un reset sans rituel.

### 5.4 `gain_at_last_cal_save`

Écrit dans le blob runtime au **Save cal réussi** (même endroit que Tw). Compare au shadow courant → `gain_mismatch`.

- Première install overlay : `0xFF` → mismatch OFF (pas de faux positif).
- Premier Save après overlay : mémorise le gain.
- Changer le gain plus tard : mismatch ON jusqu’au prochain Save (l’humain décide si un recapture est utile).

---

## 6. Sécurité avec la calibration

### 6.1 Ce qui est vrai

- `calibrate(V)` est homogène en volts. **Gain ≠ facteur d’échelle** sur X/Y.
- Ce qui casse la cal : **capturer saturé** (X = FSR, pas le vrai volt) — PR #28, point 3.406 V @ 7.4.
- Un LSB plus gros change le bruit de quantification, pas la droite.

### 6.2 Politique (Lot A+B)

| Action | Effet |
|--------|--------|
| Changer le gain hors cal-mode | Appliquer + persist. **Ne pas** `discard_draft` / dirty. `gain_mismatch` si ≠ save. |
| Changer le gain **en** cal-mode | **Refus** (select inchangé). Log warning. |
| Capturer / Save si `saturated` | Soft-block **Save** si raw actuel saturé (log + HA). Capture button : warning, pas de hard-block MVP (l’humain peut avoir un tampon hors rail sur une autre voie). A.1 : hard-block Capture aussi. |
| Invalider `cal_invalid` | **Non** — la live cal volts reste valide mathématiquement. |
| Rescaler les X (`X *= 6.144/4.096`) | **Interdit** — faux. |
| Filtres en cal-mode | Déjà bypass ; pas de lock. |
| Versionner les prefs **cal** avec le gain | **Non** (voir §5). |

### 6.3 Message produit (FR)

> Le gain change la **pleine échelle** et le LSB, pas tes points (volts). Si « ADC saturé » est ON, tes X Capturer sont des **plafonds**, pas des mesures. Élargis le PGA ou corrige le câble, **puis** recapture.

---

## 7. API sketch

Noms HA = `name:` YAML (FR possible, IDs stables — leçon v0.8.1). Exemples français ci-dessous ; l’anglais reste le défaut codegen si `name:` omis.

### 7.1 YAML Lot A

```yaml
# Source : toujours natif. Le gain YAML est le SEED (et le 1er sample).
sensor:
  - platform: ads1115
    ads1115_id: ads
    multiplexer: 'A2_GND'
    gain: 6.144          # seed + 1er poll
    sample_rate: 860
    id: ads_ph
    internal: true
    update_interval: 1s

ads1115:
  - address: 0x48
    id: ads
    continuous_mode: false   # obligatoire si 3 voies

pool_station:
  channels:
    ph:
      source_id: ads_ph
      ads:
        gain: 6.144                 # seed overlay ; NVS gagne ensuite
        saturation_on: 0.98
        saturation_off: 0.95
        hold_time: 15s
        gain_select:
          name: "pH Gain ADC"
        saturated:
          name: "pH ADC saturé"
        fsr_percent:
          name: "pH ADC % FSR"
        gain_mismatch:
          name: "pH Gain ≠ étalonnage"
        reset_yaml_button:
          name: "pH Gain YAML"
```

Sans bloc `ads:` : **zéro** nouvelle entité, `source_id` inchangé. Installs actuelles non cassées.

### 7.2 YAML Lot B

```yaml
      filters:
        filter_samples: 5
        max_jump: 0.5
        max_jump_streak: 3
        value_min: 0.0
        value_max: 14.0
        runtime:                    # opt-in ; évite l’explosion d’entités
          persist: true             # défaut true si runtime: présent
          filter_samples:
            name: "pH Échantillons médiane"
          max_jump:
            name: "pH Saut max"
          max_jump_streak:
            name: "pH Streak saut"
          value_min:
            name: "pH Clamp min"
          value_max:
            name: "pH Clamp max"
          reset_yaml_button:
            name: "pH Filtres YAML"
      update_interval: 60s
      update_interval_number:       # opt-in, hors filters: (évite collision CONF_FILTERS)
        name: "pH Intervalle (s)"
```

`filters:` reste le dict Lot 3 (déjà strippé avant `register_sensor` — `sensor_register_compat.py`). Ajouter `runtime` **dans** ce dict : le strip actuel enlève tout `filters` pour ESPHome core — OK. Ne **pas** inventer un second `filters:` ESPHome.

### 7.3 Entity IDs (si names ci-dessus)

| Entité | Exemple |
|--------|---------|
| `select.ph_gain_adc` | Gain |
| `binary_sensor.ph_adc_sature` | Saturation |
| `sensor.ph_adc_fsr` | % FSR |
| `binary_sensor.ph_gain_etalonnage` | Mismatch |
| `number.ph_echantillons_mediane` | 0–20 |
| `number.ph_saut_max` | pH |
| `number.ph_intervalle_s` | secondes |

### 7.4 Surfaces C++ (résumé)

```cpp
// Overlay ADS (Lot A)
void set_ads1115_sensor(ads1115::ADS1115Sensor *);
void apply_gain_runtime(ads1115::ADS1115Gain);
void reset_ads_to_yaml();
void set_gain_select(AdsGainSelect *);
void set_saturated_sensor(AdsSaturatedBinarySensor *);
void set_fsr_percent_sensor(sensor::Sensor *);
void set_gain_mismatch_sensor(AdsGainMismatchSensor *);

// Filtres (Lot B) — distincts des setters codegen
void apply_filter_samples_runtime(uint8_t);
void apply_max_jump_runtime(float);
void apply_max_jump_streak_runtime(uint8_t);
void apply_value_min_runtime(float);
void apply_value_max_runtime(float);
void apply_update_interval_runtime(uint32_t);
void reset_filters_to_yaml();

// Prefs
void set_runtime_preferences_key(uint32_t);
void load_runtime_preferences();   // setup(), après seed YAML
void save_runtime_preferences();   // chaque apply
void remember_gain_at_cal_save();  // appelé par CalibrationSaveButton
```

Python : `CONF_ADS = "ads"`, `CONF_FILTER_RUNTIME = "runtime"`, `generate_runtime_preferences_key`.

### 7.5 Collision `filters:` (rappel v0.7.11)

`filters.runtime` est **sous** le dict pool_station. `sensor_register_compat` continue de stripper la clé top-level `filters`. Aucun nouveau choc avec `build_filters()` si on ne passe pas `runtime` à `register_sensor`.

---

## 8. Lot C — ownership ADS (NO-GO par défaut)

### 8.1 Ce que ça voudrait dire

- pool_station appelle `ADS1115Component::request_measurement` (il est **public**).
- Ou fork un `sample()` qui retourne raw counts + volts.
- Scheduler : une file unique, burst cal-mode, SPS par voie.
- `continuous_mode` dynamique.

### 8.2 Pourquoi ce n’est pas nécessaire

Les setters par voie couvrent gain/SPS. Le hub sérialise déjà le mux. Les volts sont justes. La saturation 98 % FSR a suffi sur le live 4.094 V.

### 8.3 Triggers pour rouvrir C

1. ESPHome retire / casse `set_gain` (régression 2026.x+).
2. Besoin produit de **comptes bruts** (debug isolateur, pas juste %).
3. Plus de 4 voies / 2 chips et contention I²C mesurée.
4. Upstream refuse d’ajouter des getters et le shadow se révèle faux (autre composant appelle `set_gain` derrière notre dos — improbable).

### 8.4 Si C un jour

Toujours **composer** le hub (`ads1115_id`), ne pas réimplémenter I²C. Thin `PoolStationAdsClient` : `request_measurement` + publication `raw_counts` diagnostic. Toujours pas de driver from-scratch. Toujours pas Firmata.

---

## 9. Lots, effort, risques, ordre

```text
Lot A  ──►  (prod Eric : plus de reflash pour le rail)
  │
  └── Lot B  ──►  tuner médiane / jump / intervalle
         │
         └── Lot C seulement si A bloqué
```

### Lot A — détail

**Touche** : `__init__.py` (schéma `ads:`), `pool_station.h/.cpp`, `select.py` / `binary_sensor.py` (classes), `prefs_key.py`, `calibration_ui.cpp` (Save → `remember_gain`, Save refus si saturé), tests, MIGRATION, exemple YAML **commenté**.

**Ne touche pas** : `CalibrationPrefsData`, pipeline cal, Lot 8, gates, `source_id` obligatoire.

**Risques** :

| Risque | Mitigation |
|--------|------------|
| Header / namespace `esphome::ads1115` (2026 C++17) | Include officiel ; CI `esphome config` |
| 1er sample au gain YAML | Documenté ; A.1 sample-owned plus tard |
| NVS gagne sur un YAML volontairement changé | `reset_yaml` + MIGRATION |
| VDD 3.3 V + isolateur 5 V | Doc hardware ; le flag sat n’est pas une protec AIN |
| Select vide (bug v0.7.12) | Options **non vides** dès `register_select` |
| Double `register_component` (v0.7.2/15) | Helpers ESPHome only |

### Lot B — détail

**Touche** : mêmes fichiers + `channel_filter` apply/resize, numbers, blob rt champs filtres.

**Risques** : médiane vide après resize (publier raw jusqu’à `is_full()`, déjà le cas au boot) ; utilisateur met `max_jump: 0` sans le vouloir (step + min 0 documenté) ; trop d’entités (opt-in `runtime:`).

### Critères d’acceptation (pour un futur PR code)

**A**

- [ ] Sans `ads:` : YAML actuel compile, 0 nouvelle entité.
- [ ] Select gain → prochain volt cohérent avec nouveau FSR (multimètre / rail connu).
- [ ] Reboot : gain NVS, pas le YAML source si NVS présent.
- [ ] `saturated` ON à ≥ 98 % FSR, OFF sous 95 %, hold visible HA.
- [ ] Cal-mode ON : select gain ignoré.
- [ ] Save cal mémorise gain ; mismatch si on change après.
- [ ] Save refusé si raw saturé.
- [ ] Clé cal inchangée (golden `0x867CA26C` toujours vert).

**B**

- [ ] Numbers changent médiane / jump / clamp / intervalle sans reflash.
- [ ] Resize médiane ne crash pas ; fenêtre re-remplit.
- [ ] Draft Capturer **non** dirty.
- [ ] Lot 8 coincident_jump toujours vu si `max_jump` serré.
- [ ] Cal-mode : toujours ~1 s, filtres bypass.
- [ ] `reset_yaml` restaure seeds.

---

## 10. Non-goals

- Réécrire Firmata / j5_ha_bridge / `MQTTSensor.js`.
- Casser `source_id` (toute source `sensor::Sensor` volts reste valide **sans** `ads:`).
- Driver ADS maison, bit-bang I²C, comparateur ALERT.
- HA-tuner le **mux** (anti-swap pression/pH/ORP).
- `continuous_mode` runtime.
- Rescale auto des points X.
- Bump `ps-md5-v1` cal / magic `0xCA110005`.
- Fusionner filtres dans le draft Capturer.
- Changer clamp → rejet j5.
- Exposer seuils Lot 4 / Lot 8 (autre design).
- Lovelace custom / carte officielle (YAML examples suffisent).
- Atlas EZO / Zelia.

---

## 11. Idées rejetées (pourquoi)

1. **« Le composant doit posséder l’ADS parce que le YAML est statique »** — le YAML est statique, l’objet C++ ne l’est pas. Ownership ≠ dynamisme.
2. **Lambda only** — ça marche (`id(ads_ph).set_gain(ADS1115_GAIN_6P144)`) mais c’est le style pool-firmata que pool_station remplace : pas de NVS, pas de sat, pas de lock.
3. **Invalider la cal à tout changement de gain** — faux positifs ; volts inchangés. Le mismatch **informe**, `cal_invalid` reste « points insuffisants ».
4. **Auto-gain** (si sat → 6.144) — dangereux près d’un 5 V hardware / VDD 3.3 ; l’humain doit voir le voltmètre (procédure PR #28).
5. **Prefs cal versionnées par gain** — orphan NVS, recal forcé, déjà douloureux en v0.7.16.
6. **Un gain unique hub-wide** — les voies n’ont pas le même FSR utile (pression 0–5 V vs pH ~2–4 V). ESPHome est déjà per-sensor.
7. **Publier les counts en Lot A** — demande `request_measurement` raw ; C éventuellement.
8. **Médiane sur le source ESPHome** (`filters: - median`) **en plus** de Lot 3 — double lissage, cal-mode ne bypass que Lot 3. Rester une seule médiane (canal).

---

## 12. Go / no-go (case à cocher Eric)

Coche et merge ce doc comme décision. Un futur PR code ne part **que** des lots GO.

- [ ] **Lot A GO** — overlay gain + sat + mismatch + NVS rt + lock cal-mode + Save bloqué au rail.
- [ ] **Lot A no-go** — rester YAML ; live rester en `gain: 6.144` flashé.
- [ ] **Lot A.1** (option) — select SPS + hard-block Capture si sat.
- [ ] **Lot B GO** — numbers filtres + intervalle canal, après A.
- [ ] **Lot B no-go** — filtres restent YAML.
- [ ] **Lot C no-go** (recommandé) — pas d’ownership ADS.
- [ ] **Lot C GO** (dérogation) — motif : _______________________

Décision attendue : **A GO, B GO, C NO-GO**.

---

## 13. Références

| Sujet | Où |
|-------|----|
| Pipeline / filtres | `pool_station.cpp` `on_source_value_` ; `channel_filter.*` |
| Binding source | `__init__.py` `to_code` ~1610 ; `set_source_sensor` |
| Prefs cal | `calibration_engine.h` `CalibrationPrefsData` ; `prefs_key.py` |
| Draft/Save | `calibration_ui.cpp` `CalibrationSaveButton` ; MIGRATION v0.7.17+ |
| Lot 8 pré-garde | `on_channel_sample(..., compensated)` |
| Collision `filters:` | `sensor_register_compat.py` ; CHANGELOG v0.7.11 |
| ESPHome ADS | `ads1115.cpp` `request_measurement` ; `ADS1115Sensor::set_gain` |
| j5 filtres | `j5_ha_bridge` `config.yaml` + `MQTTSensor.filterConfiguration` |
| Rail live 4.094 V | PR #28 / `docs/PH-J5-VS-POOL-STATION-ADS.md` |
| Compose / non-goals CDC | `docs/POOL-STATION-CDC.md` F-IO-01, « Réimplémenter ads1115 » |

---

*Design runtime ADS + filtres — pool_station. Pas de code de production dans ce document.*
