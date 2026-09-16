# pH DFRobot : j5_ha_bridge (Arduino) vs pool_station (ESP32-S3 + ADS1115)

Document de **comparaison uniquement**, vérifié dans le code (pas des hypothèses
produit). Objectif : expliquer pourquoi la même sonde DFRobot qui lisait juste
sur Arduino + ConfigurableFirmata + [j5_ha_bridge](https://github.com/echavet/j5_ha_bridge)
peut afficher **~8.2** sur pool_station alors qu’un pH-mètre de référence dit
**7.4**.

Sources lues pour cette note :

| Pile | Dépôt | Fichiers principaux |
|------|--------|---------------------|
| Ancienne | `echavet/j5_ha_bridge` (public, `config.yaml` version `2026.7.29.02`) | `MQTTSensor.js`, `CustomIO.js`, `config.yaml`, `README.md` |
| Nouvelle | ce dépôt (`esphome-pool-station`) | `calibration_engine.cpp`, `pool_station.cpp` (`on_source_value_`), `channel_filter.cpp`, `temperature_compensation.h`, `examples/pool-station-minimal.yaml` |

Aucun bug firmware pool_station n’a été trouvé qui **ajoute +0.8 pH** à une
courbe correctement calée en volts. Le décalage 8.2 vs 7.4 est cohérent avec
une **courbe d’exemple / une échelle X migrée** appliquée à une tension ADS
d’environ **1.29 V**.

---

## 0. Verdict en une page (pour Eric)

**Regarde maintenant dans HA, sans reflasher :**

1. `sensor.pool_ph_raw` (volts ADS, post-médiane) — note la valeur stable.
2. Les **3 points live** Capturer (X en volts, Y en pH) et l’algorithme live
   (`piecewise` vs `polynomial`). `cal_invalid` doit être OFF.
3. `sensor.pool_ph_calibrated` (après calib + compensation T, **avant** garde)
   vs l’entité principale `sensor.pool_ph` (après clamp / jump).
4. Si tu as un **proxy HA** par-dessus pool_station, compare le proxy au
   natif. Le proxy n’est pas dans ce firmware.

| Si raw ADS vaut… | et les points live sont… | pH publié attendu | Interprétation |
|------------------|--------------------------|-------------------|----------------|
| ≈ **1.29 V** | exemple YAML `2.03→4.01`, `1.50→7.00`, `0.98→10.00` | **≈ 8.2** | Courbe d’exemple + tension trop basse vs Arduino. Recaler sur tampons. |
| ≈ **1.43 V** | même exemple YAML | ≈ 7.4 | La courbe d’exemple « tombe juste » à cette tension. Si le mètre dit 7.4 et que tu lis 8.2, tu n’es **pas** dans ce cas. |
| ≈ **2.43 V** | exemple YAML (polarité SEN0161) | ≈ **1.7** | Polarité inversée vs ta vieille courbe Arduino. |
| ≈ **2.43 V** | tes anciens points **convertis** `1.60→4`, `2.43→7.4`, `3.22→10.01` | ≈ 7.4 | Migration volts 5 V / 1023, isolation ≈ 1:1. |
| ≈ **1.29 V** | mêmes points convertis (polarité Eric) | ≈ **2.6** | Isolation / module a **divisé** la tension ; ancienne courbe ne s’applique plus. |
| n’importe quoi ≈ 1–3 V | X encore à **328 / 498 / 658** | pH ≈ **−4** | Comptes ADC collés tels quels dans Capturer. Très visible, pas 8.2. |

**À faire ensuite (ordre) :**

1. Recalibrer aux **tampons 4 / 7 / 10** sur l’ADS (mode Calibration ON,
   capturer le **volt** stable, Y = valeur tampon, Save). Ne jamais réutiliser
   328/498/658 comme X.
2. Noter le volt **circuit ouvert** (sonde débranchée du module) et le volt
   dans le bassin — pour voir si 1.29 V est un vrai signal ou un biais
   isolation / masse / électrolyseur.
3. Aligner les filtres si tu veux le même durcissement qu’avant (voir §6) :
   `filter_samples: 5`, `max_jump: 0.4`, `max_jump_streak: 5`. Attention :
   `value_min` / `value_max` **clampent** ici, ils ne **rejettent** pas comme
   j5.
4. Sel / électrolyseur : plausible pour du bruit ou ±0.1–0.3 pH, **peu
   probable seul** pour un palier stable +0.8.

---

## 1. Ancienne pile j5 — chemin réel (code)

Config de travail citée (et reprise telle quelle dans
`j5_ha_bridge/README.md` § « Exemple — pH polynomial ») :

```text
Sonde PH  pin A5   freq: 12000
calibration_type: polynomial
calibration_order: 2          # tu l’as dit ; le README public montre order: 3
calibration_precision: 20
points (x = comptes ADC):  328→4 ,  498→7.4 ,  658→10.01
filter_samples: 5
max_jump: 0.4
max_jump_streak: 5
value_min: 4.5
value_max: 9
publish_raw + publish_calibrated
Sonde Redox A4  dfrobot_orp
Pression A0     polynomial
```

Le README public de j5 met `calibration_order: 3` sur le même jeu de points.
Avec **3 points**, ordre 2 est un fit exact ; ordre 3 est sous-déterminé
(la lib `regression` produit quand même une courbe qui passe par les points).
L’écart entre les deux vers ADC 550 est ~0.03 pH — **sans rapport avec +0.8**.

### 1.1 Firmata → valeur « raw »

```
Arduino ADC 10 bits, Vref typ. 5 V
    → ConfigurableFirmata analog report (0…1023, RESOLUTION.ADC = 1023)
    → Johnny-Five Sensor (CustomIO étend firmata.Board)
    → médiane interne J5 sur l’intervalle `freq`
    → événement `change` si |ΔADC| ≥ `threshold` (défaut J5 = 1 compte)
    → MQTTSensor.handleChange(j5Value)          # j5Value = comptes ADC
```

Preuves :

- `CustomIO.js` `logBoardFirmwareIdentity()` : `RESOLUTION.ADC === 1023` →
  « 10 bits (0-1023), Vref carte typ. 5 V ».
- `MQTTSensor.js` discovery raw : `unit_of_measurement: 'ADC'`, champ
  `attributes.raw_value`.
- README j5 : « `x_point` = valeur **brute ADC** (pas la tension en volts) ».
- `configureSensor()` passe `pin` / `freq` / `threshold` à `new MQTTSensor({
  ...sensorConfig })` donc Johnny-Five lit bien `A5` en analogique.

**Domaine raw = comptes 0–1023, pas des volts.**
`freq: 12000` = **12 secondes** entre deux traitements J5 (millisecondes,
pas du Hz). Ce n’est pas `update_interval: 40ms`.

Johnny-Five n’expose **pas** de `scale: [min,max]` ni de `fsr.curve` dans
cet add-on. La section historique
[MIGRATION.md](MIGRATION.md#from-j5_ha_bridge-to-pool_station) qui parlait
de ça était **fausse** (corrigée pour pointer ici).

### 1.2 Calibration polynomiale

`MQTTSensor.applyCalibration(adc)` (`MQTTSensor.js`) :

1. Si `calibration_type === 'dfrobot_orp'` → formule SEN0165 **ORP uniquement**
   (pas le pH) : `V_mV = ADC * 5000 / 1024`, `ORP = 2000 - V_mV - OFFSET`.
2. Sinon → `regression[type](data_points, { order, precision }).predict(adc)[1]`
   avec la lib npm [`regression`](https://www.npmjs.com/package/regression).

Pour le pH :

- type `polynomial`, order **2** (ton YAML) ou **3** (exemple README),
- `precision: 20` = **chiffres significatifs des coefficients** passés à
  `regression`, **pas** l’arrondi de la valeur publiée,
- X = ADC, Y = pH,
- **aucun `mid_mv`**, aucun 2.5 V magique, aucune formule Nernst.

Fit ordre 2 exact sur tes 3 points (recalculé) :

```text
pH = -4.3852454545 + 0.0292299242·ADC − 0.0000111742·ADC²
```

| ADC | pH |
|-----|-----|
| 328 | 4.000 |
| 498 | 7.400 |
| 543 | ≈ 8.19 |
| 658 | 10.010 |

Pente locale à ADC 498 : **≈ 0.018 pH / compte** ≈ **0.8 pH pour 44 comptes**
≈ **216 mV** sur un Uno 5 V.

Polarité de **ta** courbe Arduino : **ADC plus haut → pH plus haut**
(328→acide, 658→basique). C’est l’**inverse** du module analogique DFRobot
SEN0161 « classique » et de l’exemple YAML pool_station (tension plus haute
→ pH plus bas).

### 1.3 Filtres puis MQTT

`handleChange` (`MQTTSensor.js`) :

```text
instantRaw (ADC, événement change)
  → applySampleWindow()     médiane glissante de filter_samples lectures
  → applyCalibration()      → "calibrated"
  → applyValueGuards()      min/max + max_jump
  → MQTT JSON retenu
```

Gardes (très différent de pool_station) :

- `value_min` / `value_max` : si hors plage → **rejet**, on **garde la
  dernière valeur acceptée** (`accepted: false`, `reject_reason:
  below_min|above_max`). Ce n’est **pas** un clamp.
- `max_jump` : saut en **unités calibrées (pH)**. Rejet jusqu’à
  `max_jump_streak` consécutifs, puis nouveau palier accepté.
- Publication principale = dernière valeur **acceptée** (ou calibrated une
  fois au démarrage si rien n’a encore été accepté).
- Compagnons : `raw_value` = ADC **instantané** (pré-fenêtre) ;
  `calibrated` = post-fenêtre + calib, **pré-gardes**.

Pas de compensation de température. Pas de gate. MQTT discovery + state
JSON, pas l’API native ESPHome.

---

## 2. Nouvelle pile pool_station — chemin réel (code)

### 2.1 ADS1115 → volts

`examples/pool-station-minimal.yaml` (référence hardware Eric) :

```yaml
ads1115:
  - address: 0x48
    continuous_mode: false

# source pH
multiplexer: 'A2_GND'     # single-ended vs GND
gain: 4.096               # pleine échelle ±4.096 V, LSB ≈ 0.125 mV
update_interval: 1s       # le composant natif ESPHome publie des VOLTS
```

Le channel `ph` **compose** ce sensor (`source_id: ads_ph`). Il ne relit
pas l’ADS lui-même.

`PoolStationChannelSensor::on_source_value_`
(`components/pool_station/pool_station.cpp`) :

```text
callback d’état du sensor ADS (volts)
  → throttle à update_interval (60 s en exemple) ou calibration_interval (1 s)
  → gate optionnel (bypass en mode calib)
  → médiane filter_samples (bypass en mode calib)
  → raw_sensor                    # volts, post-médiane
  → CalibrationEngine.calibrate() # X = volts
  → temp. compensation Nernst     # Lot 5, si enabled + Tw valide
  → calibrated_sensor             # pré-garde
  → clamp value_min / value_max   # modifie la valeur
  → jump guard                    # bypass + reset en mode calib
  → publish_state (entité principale)
```

**Domaine raw = volts.** Capturer écrit ce volt dans le X du point
(`publish_captured_point_x`).

### 2.2 Moteur de calibration

`CalibrationEngine::calibrate(float raw_voltage)` utilise le type **live**
(dernier Save), pas le brouillon Capturer.

| Type | Ce que fait le C++ | Min. points |
|------|--------------------|-------------|
| `piecewise` (exemple YAML pH) | Segments linéaires, extrapole hors plage | 2 |
| `polynomial` | Moindres carrés, Gauss + pivot, `float`, order défaut **2** (max 5) | order+1 |
| `linear` | Moindres carrés sur tous les points valides | 2 |
| `dfrobot_orp` | `ORP = mid_mv − V·1000 − offset_mv` (défaut `mid_mv=2500`) | 0 |
| `none` / calib invalide | **pass-through** : le pH publié = **le volt** | — |

`precision:` YAML (défaut **2**) = **arrondi de la sortie**
(`apply_precision_`), pas la précision des coefficients. Ce n’est **pas**
le `calibration_precision: 20` de j5.

Fallback pH si `cal_invalid` : publier le volt comme pH (≈ 1.3–2.5), **pas**
8.2. Donc 8.2 implique une courbe **valide** qui transforme le volt.

Points d’exemple YAML (polarité SEN0161, **à remplacer**) :

```text
2.03 V → 4.01
1.50 V → 7.00
0.98 V → 10.00
```

Après v0.7.16 la clé NVS a changé : sans recapture, le firmware retombe sur
ces seeds YAML. Voir [MIGRATION.md](MIGRATION.md#calibration-nvs-key-v0716).

### 2.3 Filtres, T, échantillonnage

Filtres exemple pH :

```yaml
filter_samples: 5
max_jump: 0.5
max_jump_streak: 3
value_min: 0.0
value_max: 14.0
```

- Médiane sur les échantillons **qui passent le throttle**. À
  `update_interval: 60s`, 5 échantillons ≈ **5 minutes**.
- `value_min` / `value_max` : **clamp** (`filter_functions::clamp_value`) —
  une valeur 10.5 avec max 9 devient **9.0**, elle n’est pas rejetée.
- Jump guard : même idée de streak que j5, implémentation C++ séparée
  (`JumpGuard::process`).
- Compensation T (exemple `enabled: true`) :

  ```text
  pH_comp = 7 + (pH_mes − 7) × (T_ref+273.15)/(Tw+273.15)
  ```

  À Tw = 30 °C et pH 8.2 → 8.18. À 10 °C → 8.26. **±0.06 pH max**, pas +0.8.
  j5 n’avait **aucune** compensation T.

ADS `continuous_mode: false` : une conversion par `update_interval` du
sensor source (1 s). Le channel jette la plupart de ces 1 s s’il est à 60 s.

---

## 3. Comparaison pomme-pomme

### 3.1 Domaine raw et mapping 328 / 498 / 658

| | j5 + Uno | pool_station + ADS1115 |
|--|----------|------------------------|
| Grandeur raw | comptes ADC 0–1023 | volts |
| Conversion interne | **aucune** vers volts pour le pH | ESPHome ADS1115 livre déjà V |
| Companion « raw » | ADC | V |
| X des points de calib | ADC | V |

Si on **traduit** tes anciens X en tension Uno (la seule conversion
physique licite, et **uniquement** si le module + isolation sortent la
même tension qu’avant) :

```text
V = ADC × Vref / dénominateur
```

| ADC | pH (Y) | 5 V / 1023 (typ. Uno) | 5 V / 1024 (sample DFRobot ORP) | 3.3 V / 1023 (erreur fréquente) |
|-----|--------|------------------------|----------------------------------|----------------------------------|
| 328 | 4.00 | **1.603 V** | 1.602 V | 1.058 V |
| 498 | 7.40 | **2.434 V** | 2.432 V | 1.606 V |
| 658 | 10.01 | **3.216 V** | 3.213 V | 2.123 V |

Ces volts **ne sont pas** interchangeables avec l’exemple YAML
(2.03 / 1.50 / 0.98). Polarité **opposée** et milieu différent.

Coller 328/498/658 comme X volts dans Capturer, puis évaluer à ~1.5 V
(loin à gauche de 328) donne pH ≈ **−4.3** (poly ordre 2). Ce n’est **pas**
le bug qui produit 8.2 — donc tes points live ne sont probablement **pas**
les comptes ADC bruts.

### 3.2 Hypothèses de milieu / référence

| | j5 pH | j5 ORP `dfrobot_orp` | pool_station pH | pool_station ORP |
|--|-------|----------------------|-----------------|------------------|
| Référence | aucune (courbe empirique) | Vref **5000 mV**, dénominateur **1024**, milieu formule = **2000 mV** | aucune (courbe empirique sur V) | `mid_mv` défaut **2500** |
| Formule constructeur | non | `ORP = 2000 − V_mV − OFFSET` | non | `ORP = 2500 − V·1000 − offset` |

Le pH n’utilise **ni** `mid_mv` **ni** 2.5 V dans les deux piles. Un
« milieu 2.5 V = pH 7 » n’existe dans le code que si **tes points** le
disent.

Les formules ORP j5 vs pool_station **diffèrent** (2000 vs 2500 +
dénominateur 1024). Hors sujet pH, mais ne pas porter un OFFSET j5 tel quel.

### 3.3 Filtres / saut / clamp

| | j5 (ta config) | pool_station (exemple) |
|--|----------------|------------------------|
| Médiane interne ADC | oui (J5 sur 12 s) | non (ADS 1 coup / 1 s) |
| Médiane applicative | 5 lectures `change` | 5 lectures throttlées (60 s → ~5 min) |
| Saut | 0.4 pH, streak **5** | 0.5 pH, streak **3** |
| Hors plage | **rejet** 4.5–9 | **clamp** 0–14 |
| 8.2 publié | accepté (dans 4.5–9) | accepté (dans 0–14) |

Les filtres **ne peuvent pas** créer un biais systématique de +0.8 pH.
Ils peuvent seulement **retenir un ancien palier** (j5 rejet, jump guard)
ou **plaquer** une valeur aux bornes (clamp pool_station). 8.2 n’est une
borne d’aucun des deux.

### 3.4 Cadence

| | j5 | pool_station exemple |
|--|----|----------------------|
| Période utile pH | 12 s (`freq`) × jusqu’à 5 échantillons | source 1 s, publish 60 s, médiane ×5 |
| Mode calib | n’existe pas | 1 s, **sans** médiane ni jump |
| Seuil de publication | `threshold` en **comptes ADC** (défaut 1) ; pas de `change` = HA « figé » | chaque tick après throttle, même si Δ = 0 |

### 3.5 ADC 5 V 10 bits vs ADS gain 4.096 single-ended

| | Arduino Uno + Firmata | ADS1115 gain 4.096, `A2_GND` |
|--|------------------------|------------------------------|
| Pleine échelle | 0–5 V | 0–4.096 V (au-delà : **saturation**) |
| LSB | ≈ 4.88 mV | ≈ 0.125 mV |
| Mode | single-ended vs GND | single-ended vs GND |
| Alim logique | 5 V | ESP32-S3 **3.3 V** |
| Isolation | selon câblage d’époque | module d’isolation sur A2 (prod) |

Tes points Arduino (1.60–3.22 V) tiennent dans ±4.096 V. Le gain n’est
**pas** trop petit pour cette sonde. Un signal **5 V** (pression, circuit
ouvert mal câblé) saturerait l’ADS — ce n’est pas un 8.2 pH, ce serait un
volt coincé à ~4.1 V → pH YAML extrapole vers l’acide (~très bas).

Ce qui **change** vraiment le volt :

1. **Alim du module pH** (5 V vs 3.3 V) — le SEN0161 analogique est conçu
   en 5 V, milieu ~2.5 V. En 3.3 V tout le transfer shift.
2. **Module d’isolation** — 1:1, diviseur 5 V→3.3 V, ou ~2:1. Voir §4.
3. **Masse isolée vs GND ADS** — offset, pas une erreur de `calibrate()`.
4. **Électrolyseur / sel** — courants de fuite, potentiel de jonction ;
   l’isolation est là pour ça, elle ne garantit pas un gain 1.000.

---

## 4. Pourquoi 8.2 plutôt que 7.4 — classement

Hypothèses **non contractuelles**, mais **calées sur le code + l’arithmétique
des deux courbes**. +0.8 pH sur ta courbe j5 ≈ **+44 LSB Arduino** ≈
**+216 mV** à 5 V. Sur l’exemple YAML piecewise, 8.2 correspond à
**1.292 V** et 7.4 à **1.431 V** (Δ = 139 mV).

### 1 — Très probable : points de calib à la mauvaise échelle / mauvaise polarité

L’exemple YAML (et toute courbe « SEN0161 textbook ») dit : **plus le volt
est bas, plus le pH est haut**. Ta courbe Arduino disait l’inverse.

Si le live utilise l’exemple YAML (seeds, ou NVS perdue en 0.7.16, ou
Capturer jamais Save après édition) :

- un ADS à **1.29 V** → pH **8.2** exactement ;
- c’est le volt qu’on obtient si l’ancien **2.43 V @ pH 7.4** passe dans un
  **diviseur ≈ 2:1** (2.434 / 2 = 1.217 V → YAML ≈ **8.6**) ou un isolation
  non-1:1 + un peu d’offset.

**Migrer 328/498/658 sans refaire les tampons sur l’ADS est un biais
garanti.** Soit les X restent des comptes (pH absurde ≈ −4), soit on
convertit en volts 5 V/1023 alors que l’isolation a changé l’échelle
(pH 2.6 au lieu de 7.4 si V a été /2), soit on garde l’exemple YAML sur
un volt « nouveau monde » (8.2 à 1.29 V).

Ce n’est pas un bug de `calibrate_polynomial_` / `calibrate_piecewise_`.
Les deux font ce qu’on leur demande dans **leur** domaine.

### 2 — Probable : isolation + alim 3.3 V / 5 V, pas le gain ADS

Le gain 4.096 et le single-ended sont corrects pour 0–3 V. Ils n’inventent
pas +0.8 pH. En revanche :

- isolation ≠ 1:1 → le volt n’est plus celui de tes points Arduino ;
- module pH alimenté autrement qu’en 5 V → milieu ≠ 2.5 V ;
- offset de masse isolée de quelques centaines de mV → plusieurs dixièmes
  de pH sur une pente ~5.8 pH/V (exemple YAML).

Un test décisif : **volt à sonde débranchée** vs **volt bassin** vs **volt
tampon 7**. Si circuit ouvert ≈ volt bassin ≈ 1.29 V, le problème est
électrique (module / isolation / masse), pas la chimie.

### 3 — Possible mais secondaire : sel / électrolyseur

L’eau n’était pas salée à l’époque j5. Le sel change la force ionique et
le potentiel de jonction ; l’électrolyseur injecte chlore + bruit de
puissance. Typiquement :

- ORP beaucoup plus sensible que le pH ;
- pH : plutôt **dixièmes**, pas un palier propre +0.8, sauf sonde empoisonnée
  ou courant de fuite fort.

Les flags Lot 8 (`shared_noise`, `coincident_jump`) détectent un **soupçon**
EMI pompe/ORP, ils **ne corrigent pas** le pH. Mode Calibration ON = pas
d’échantillons interférence.

Tu doutes plus de l’ADS que du sel : le code + le 1.29 V → 8.2 vont dans
ton sens (échelle / isolation), pas « l’ADS 16 bits est instable ».
L’ADS est **plus** stable et plus fin que le 10 bits Uno. L’instabilité
serait du jitter, pas un offset fixe de 0.8.

### 4 — Peu probable : filtres manquants ou différents

Portage incomplet des filtres (streak 3 vs 5, max_jump 0.5 vs 0.4, clamp
0–14 vs rejet 4.5–9, fenêtre 5 min vs ~1 min) : ça change la **réponse**
et les **pics**, pas le régime 8.2 vs 7.4.

### 5 — Peu probable : compensation T

Nouvelle, absente de j5. Effet < 0.07 pH dans la plage 10–35 °C. Un
pH-mètre de référence a souvent son propre ATC : l’écart résiduel reste
petit près de 7.4.

### 6 — Pas trouvé : bug firmware qui ajoute +0.8

Vérifié et **écarté** comme cause de 8.2 :

| Candidat | Pourquoi ce n’est pas +0.8 |
|----------|----------------------------|
| Poly `float` / `pow` sur V~1.5 | 3 points bien conditionnés ; erreur << 0.01 |
| `precision: 2` vs j5 `20` | arrondi de sortie vs digits des coeffs |
| Draft Capturer | le publish utilise `live_type_` + `live_points_` (v0.7.18) |
| `cal_invalid` pass-through | publierait ~1.5, pas 8.2 |
| Jump / clamp | 8.2 n’est pas une borne |
| `dfrobot_orp` appliqué au pH | seulement si tu as mis ce type sur le channel pH (min 0 points, résultat en mV absurdes, pas 8.2) |

Pas de second PR firmware.

---

## 5. Ce qu’il ne faut **pas** faire

- Recopier `x_point: 328/498/658` dans Capturer / YAML pool_station.
- Croire que `calibration_precision: 20` j5 = `precision: 2` pool_station.
- Croire que `freq: 12000` = 12 Hz ou que `freq: 25` = 25 Hz
  (`MIGRATION.md` disait `update_interval: 40ms` — c’est le **défaut J5
  25 ms**, pas ta config piscine).
- Porter `value_min: 4.5` / `value_max: 9` en s’attendant à un **rejet**.
  Ici ça **clamp** : un vrai 10.2 deviendrait 9.0 et aurait l’air « sage ».
- Comparer `Sonde PH raw` (ADC) à `Pool pH Raw` (volts) sans conversion.
- Porter l’OFFSET / `mid_mv` ORP j5 (formule 2000/1024) vers
  `dfrobot_orp` pool_station (2500 / volts).

---

## 6. Prochaines mesures concrètes

### A. Diagnostic en 5 minutes (sans tampon)

Dans HA, noter **au même instant** :

| Entité | Rôle |
|--------|------|
| `Pool pH Raw` | volt ADS (post-médiane) |
| points X/Y live + algo | courbe vraiment utilisée |
| `pH Cal Invalid` | doit être OFF |
| `Pool pH Calibrated` | pré-garde (+ T si ON) |
| `Pool pH` | publié |
| `Pool pH Temp Compensation` | ON/OFF |
| Tw eau | pour estimer l’effet T |
| proxy HA s’il existe | doit = natif |

Interpréter avec le tableau §0.

### B. Circuit ouvert vs bassin vs tampon 7

1. Mode Calibration ON (1 s, filtres off).
2. Sonde **débranchée** du module → noter raw V (30 s).
3. Sonde dans le bassin → raw V + pH publié + lecture du mètre.
4. Sonde rincée, tampon 7.00 (idéalement aussi 4.01 et 10.01) → raw V.

Attendu si le hardware est un SEN0161 5 V 1:1 : tampon 7 ≈ **2.5 V**.
Si tu vois ≈ **1.25 V** au tampon 7, l’isolation (ou l’alim) **divise par
~2** : une courbe d’exemple 1.50 V = pH 7 lira trop bas (trop alcalin).

### C. Recalibrer pour de bon (la seule migration valide)

1. Tampons frais, Tw notée (Save stocke Tw, Lot 5).
2. Calibration Mode ON.
3. Pour chaque tampon : attendre raw V stable → Capture point N → Y = pH
   tampon (4.01 / 7.00 / 10.01, pas « 7.4 du bassin »).
4. Algo : `piecewise` (exemple) ou `polynomial` order 2 (comme j5).
5. **Save** (draft_mode : sans Save, le live garde l’ancienne courbe).
6. Vérifier les 3 tampons sur l’entité principale, puis le bassin vs mètre.

Les X seront des volts **de cette** chaîne (module + isolation + ADS +
gain). Ils n’ont aucune raison de ressembler à 328/498/658 ni même à
1.60/2.43/3.22.

### D. Porter les filtres j5 (optionnel, après le recal)

```yaml
filters:
  filter_samples: 5
  max_jump: 0.4
  max_jump_streak: 5
  # Pas de value_min/value_max si tu veux le rejet j5 — le firmware
  # clamp, il ne tient pas « last good ». Préfère diagnostics.range_*
  # + automations HA pour alerter hors 4.5–9.
```

Garde `update_interval` pH raisonnable (15–60 s). En calib, les filtres
sont de toute façon bypass.

### E. Comparaison raw côte à côte (si l’Arduino est encore là)

Même sonde, même module, T + câbles identiques :

| | Arduino A5 | ADS A2 |
|--|------------|--------|
| Raw | ADC 498 @ pH 7.4 ? | ? V |
| V équivalent | ADC×5/1023 | valeur raw HA |

Si ADC≈498 et ADS≈2.43 V → isolation 1:1, le problème est **la courbe**.
Si ADC≈498 et ADS≈1.29 V → **l’échelle a changé** ; recal obligatoire,
gain ADS innocent.

### F. Sel / électrolyseur (après A–C)

- Couper l’électrolyseur 15 min, revoir raw V.
- Flags `Pool Interference` / `Pool Shared Noise`.
- Si raw V bouge de < 30 mV et le pH publié reste 8.2, ce n’est pas
  l’électrolyseur.

---

## 7. Références code

**j5_ha_bridge**

- Pipeline : `MQTTSensor.handleChange` → `applySampleWindow` →
  `applyCalibration` → `applyValueGuards`
- Poly / précision : `calibrationConfiguration`, `regression.polynomial`
- ORP (pas pH) : `dfrobotOrpRaw`, `DFROBOT_ORP_VREF_MV = 5000`,
  `DFROBOT_ORP_ADC_MAX = 1024`
- ADC 10 bits : `CustomIO.logBoardFirmwareIdentity`, `util.js`
  `ARDUINO_USB_PRODUCTS`
- Annonce raw en ADC : `announce()` `unit_of_measurement: 'ADC'`

**pool_station**

- Pipeline : `PoolStationChannelSensor::on_source_value_`
- Calib : `CalibrationEngine::calibrate`, `calibrate_piecewise_`,
  `calibrate_polynomial_`, `apply_precision_`
- Filtres : `SlidingWindow::median`, `JumpGuard::process`,
  `filter_functions::clamp_value`
- T : `temp_comp_functions::compensate_ph`
- Seeds YAML pH : `examples/pool-station-minimal.yaml` channel `ph`

---

## 8. Voir aussi

- [MIGRATION.md](MIGRATION.md) — parcours de migration (section j5 corrigée)
- [CHANGELOG.md](../CHANGELOG.md) — clé NVS v0.7.16 (recal obligatoire après flash)
- j5 README public : domaine ADC, `freq` en ms, `precision` = coeffs
