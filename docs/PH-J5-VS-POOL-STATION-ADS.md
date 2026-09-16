# pH DFRobot : j5_ha_bridge (Arduino ADC) vs pool_station (ADS1115 volts)

Comparaison **vérifiée dans le code**, recentrée sur l’inventaire **live pool-io**
(septembre 2026). Ce n’est pas une réécriture de feature.

Pile j5 lue : [github.com/echavet/j5_ha_bridge](https://github.com/echavet/j5_ha_bridge)
(`MQTTSensor.js` → `applyCalibration`). Une copie locale
`Documents/2023/j5_ha_bridge` sur le Mac d’Eric n’est **pas** la référence :
préférer le dépôt public.

---

## 0. Inventaire live pool-io (ne pas ignorer)

| Élément | Valeur live | Ce que ça veut dire |
|---------|-------------|---------------------|
| Calib pH NVS | **linéaire, 2 points seulement** | Plus le 3ᵉ point YAML / j5 |
| Point 1 | **1.602 V → pH 4.0** | = migration `328 × 5/1023` (ou `/1024`) |
| Point 2 | **3.406 V → pH 7.4** | **Anormal** : le 7.4 j5 était **≈ 2.43 V**, pas 3.41 V |
| Point 3 (10.01) | **absent** | Seed YAML l’avait (`3.213 V → 10.01`) ; le NVS l’a écrasé |
| Volt pH ADS | **≈ 4.094 V** | Gain **4.096** → **rail** (99.95 % de la pleine échelle) |
| Entité | `sensor.sonde_ph_2` ≈ **8.709** | = extrap. de la droite 2 pts au rail, pas « l’eau à 8.2 » |

Droite live (`CalibrationEngine::calibrate_linear_`, 2 points = la droite
qui les joint) :

```text
pH = 4.0 + (V − 1.602) × (7.4 − 4.0) / (3.406 − 1.602)
   = 4.0 + (V − 1.602) × 1.8847   [pH / V]

V = 4.094  →  pH ≈ 8.70
V = 4.096  →  pH ≈ 8.70
```

Ça **matche** 8.709 (arrondi, LSB, compensation T éventuelle). Le firmware
fait exactement ce qu’on lui demande. Le problème est **électrique +
courbe**, pas un off-by-0.8 dans `calibrate()`.

**Conséquence :** tant que raw reste collé à ≈ 4.09 V, **aucun** recapture
de Y ne peut donner un pH juste. Le vrai volt (peut-être 2.4 V, 5 V, ou
n’importe quoi au-dessus de 4.096 V) est **invisible**.

---

## 1. Migration comptes → volts (cœur du décalage d’échelle)

### 1.1 Ce que j5 utilisait vraiment

`MQTTSensor.handleChange` → `applySampleWindow` → `applyCalibration(adc)`
([j5 `MQTTSensor.js`](https://github.com/echavet/j5_ha_bridge/blob/main/MQTTSensor.js)).

- Entrée = **comptes ADC Johnny-Five / Firmata**, 0–1023 sur Uno 5 V.
- Companion raw : `unit_of_measurement: 'ADC'`.
- pH : `regression.polynomial` (order 2 chez Eric ; le README public montre
  parfois order 3). **Pas** de conversion interne en volts.
- `calibration_precision: 20` = digits des **coefficients** npm `regression`,
  pas l’arrondi HA.
- `freq: 12000` = 12 **secondes**.
- `value_min` / `value_max` : **rejet** (garde last-good), pas un clamp.

Points j5 qui ont marché des années (sonde PH pin A5) :

```text
328 → 4.00
498 → 7.40     ← « classic 7.4 »
658 → 10.01
```

### 1.2 La seule conversion licite (si le module sortait encore la même tension)

```text
V = ADC × 5 / 1023     (Vref Uno typique)
V = ADC × 5 / 1024     (dénominateur sample DFRobot ORP ; Δ < 3 mV)
```

| ADC j5 | Y | ×5/1023 | ×5/1024 | Seed YAML pool_io (rappel) |
|--------|---|---------|---------|------------------------------|
| 328 | 4.00 | **1.603 V** | 1.602 V | 1.602 → 4 |
| 498 | 7.40 | **2.434 V** | 2.432 V | 2.432 → 7.4 |
| 658 | 10.01 | **3.216 V** | 3.213 V | 3.213 → 10.01 |

Le seed YAML 3 points était une **bonne** migration count→volt. Le NVS live
a gardé le point 4.0 (1.602 V) et **remplacé** le 7.4 par 3.406 V, puis a
**perdu** le 10.01. `load_from_preferences()` écrase les seeds YAML
(`calibration_engine.cpp`) : après un Save 2 points, le 3ᵉ n’existe plus.

### 1.3 pool_station ne voit que des volts

ADS1115 ESPHome (`A2_GND`, gain 4.096) publie des **volts**. Capturer écrit
ce volt dans X. `calibrate(raw_voltage)` n’a aucun chemin « comptes 0–1023 ».

Porter 328/498/658 tels quels comme X donnerait pH ≈ −4 (évaluation à ~1–4 V
d’un poly calé sur 328–658). Ce n’est **pas** le live : les X sont bien en
volts, mais le 2ᵉ est faux et l’ADC est au plafond.

---

## 2. Pourquoi 3.406 V @ pH 7.4 est anormal

Le 7.4 **historique** (eau / tampon, même sonde DFRobot, Arduino 5 V) était
**498 comptes ≈ 2.43 V**. Écart live :

```text
3.406 − 2.434 = +0.97 V
3.406 × 1023 / 5 ≈ 697 comptes   ← jamais un point j5
```

À **2.434 V** (le vrai 7.4 j5), la droite live donnerait **pH 5.57**, pas
7.4. Donc soit le module ne sort plus 2.43 V à pH 7.4, soit on a **figé
un volt déjà trop haut** en l’appelant 7.4.

Causes plausibles du 3.406 V (classées) :

1. **Capture terrain** : Y = 7.4 du mètre de référence alors que l’ADS
   lisait déjà ~3.4 V (signal en train de monter vers le rail). La faute
   électrique est cuite dans la courbe.
2. **Mauvais point / mauvais slot** : 3.406 V n’est ni 2.432 (7.4 migré)
   ni 3.213 (10.01 migré). Plus proche d’un ADC ~697 que d’un des trois
   tampons j5.
3. **Pas** une erreur `×5/1023` sur 498 (ça donne 2.43 V, déjà dans le
   seed). Le point 4.0 **est** cette migration ; le 7.4 live **ne l’est pas**.

Polarité : la courbe j5 et le seed 3 pts vont **dans le même sens**
(volt plus haut → pH plus haut). Ce n’est pas l’inversion SEN0161
textbook (2.5 V @ pH 7, volt qui **baisse** quand le pH monte). Le
hardware d’Eric s’est toujours comporté comme « V croît avec le pH ».
Le 3.406 V casse l’échelle, pas le signe.

---

## 3. Rail ADS1115 @ gain 4.096

### 3.1 Ce que 4.094 V signifie

PGA gain `4.096` → pleine échelle **±4.096 V**. En single-ended `A2_GND`,
toute tension **≥ 4.096 V** est publiée ≈ **4.094–4.096 V**.

```text
4.094 / 4.096 = 99.95 % du FSR
≈ 32751 / 32767 comptes positifs 15 bits
```

Ce n’est **pas** une mesure. C’est « au moins 4.096 V, peut-être 5 V ».

Sur Arduino, le même module pouvait sortir jusqu’à **5 V** (10 bits, pas
de plafond à 4.096). Les points j5 (1.60–3.22 V) passaient sous 4.096 V
**à l’époque**. Aujourd’hui le signal live est **au-dessus** de ce que
l’ADS a le droit de voir.

L’exemple YAML disait « module pH 0–3 V » + gain 4.096. **Faux pour cette
install** : l’historique j5 est déjà 1.6–3.2 V, et le live tape 5 V-class.

### 3.2 Pourquoi ça publie 8.7 (et pas 13)

Si on avait gardé le seed 3 pts interpolé/extrapole :

| V | 3 pts seed (piecewise / poly j5-migré) | Droite live 2 pts |
|---|----------------|-------------------|
| 2.432 | 7.40 | 5.57 |
| 3.213 | 10.01 | 7.04 |
| 3.406 | ≈ 10.7 | **7.40** (forcé) |
| 4.094 | ≈ **13.0** | **8.70** |

Le point 3.406 V @ 7.4 ** aplati la pente** (1.88 pH/V au lieu de
~3.3–4.1 pH/V sur le segment 2.43–3.22). L’extrapole au rail donne un
8.7 « crédible » au lieu d’un 13 qui aurait crié. Les filtres
(`value_max: 9` côté j5 = rejet ; clamp 0–14 ici) n’inventent pas 8.709.

Compensation T Nernst (si ON) : ±0.07 pH entre 10 et 35 °C. Pas +1.3.

### 3.3 Ce qui peut coller A2 au rail

À vérifier **avant** de recalibrer :

1. **Gain trop petit** pour un module / isolateur 0–5 V → passer à
   **6.144** (lit jusqu’à 6.144 V, LSB 0.1875 mV). Ne pas descendre à
   2.048.
2. **Isolateur** : sortie côté ADS à VCC (5 V ou 3.3 V), gain ≠ 1:1,
   masse isolée non commune avec GND ADS, fil A2 ouvert (certaines
   sorties flottent au rail).
3. **Alim module pH** : toujours du 5 V analogique ? L’ESP32-S3 est en
   3.3 V logique ; le module DFRobot analogique est conçu en 5 V.
4. **Sonde débranchée / entrée module ouverte** — noter le volt
   (souvent rail ou ~Vcc/2).
5. **Câblage A2** : court vers 5 V isolé, mauvais multiplexer
   (`A2_GND` vs différentiel), autre capteur sur la même entrée.
6. Sel / électrolyseur : peut **bruit**er ou décaler de quelques
   dixièmes ; un palier **plat à 4.094 V** est une sat. ADC, pas de la
   chimie.

L’ADS 16 bits n’est pas « instable ». Il est **plein**. Plus fin que
l’Uno (0.125 mV vs 4.9 mV) **tant qu’on n’est pas au rail**.

---

## 4. Chemins logiciels (rappel court)

### j5

```text
Uno A5 10-bit 5 V
  → Firmata ADC 0…1023
  → J5 médiane sur `freq` (12 s) + `threshold` (comptes)
  → médiane filter_samples=5
  → polynomial(ADC) → pH
  → reject min/max + max_jump → MQTT
```

Pas de `mid_mv` pH. ORP `dfrobot_orp` (A4) est une autre formule
(`V_mV = ADC×5000/1024`, `ORP = 2000 − V_mV − OFFSET`) — ne pas la
porter sur le pH.

### pool_station

```text
ADS A2_GND gain 4.096  (1 s, volts, saturé à 4.096)
  → throttle channel (souvent 60 s ; 1 s en Calibration Mode)
  → médiane filter_samples
  → live calib (ici LINEAR 2 pts NVS, pas le seed 3 pts)
  → compensate_ph (si enabled)
  → clamp min/max   ← pas un rejet j5
  → jump guard → HA (proxy éventuel en plus)
```

`precision:` YAML = arrondi de **sortie** (défaut 2). Ordre poly défaut
**2**. Draft Capturer ne change pas le live tant que Save n’est pas
validé (v0.7.18).

Aucun bug C++ trouvé qui ajouterait +0.8 à une courbe juste **et** un
volt dans la plage. Pas de second PR firmware.

---

## 5. Marche à suivre (gain / câblage / recal)

Faire **dans cet ordre**. Recalibrer maintenant, au rail, reproduirait
un 3.406 V encore pire.

### A. Sortir du rail (obligatoire)

1. Lire `Pool pH Raw` / source ADS. Si ça reste **4.09x V**, tout le
   reste est du théâtre.
2. YAML source pH (et pression 0–5 V si isolateur 5 V) :

   ```yaml
   - platform: ads1115
     ads1115_id: ads
     multiplexer: 'A2_GND'
     gain: 6.144          # lit 0–5 V ; 4.096 sature
     id: ads_ph
     update_interval: 1s
   ```

   Flasher, Calibration Mode ON, **ne pas Save** tout de suite.
3. Noter raw V dans trois états (30 s chacun) :
   - sonde **débranchée** du module ;
   - sonde dans le **bassin** (mètre à côté) ;
   - si possible **tampon 7.00** (rinçage).
4. Interpréter :

   | Raw après gain 6.144 | Diagnostic |
   |----------------------|------------|
   | Toujours ≈ 5.0 V / Vcc | Isolateur / câble / entrée ouverte → **câblage** |
   | ≈ **2.3–2.6 V** au bassin ou tampon 7 | Signal retrouvé ; 4.096 **coupait** le haut |
   | ≈ **3.4 V** au bassin @ mètre 7.4 | Module vraiment haut ; pas seulement le PGA |
   | ≈ 1.5 V et pH mètre 7.4 | Autre échelle (diviseur / 3.3 V) ; recal sur **ces** volts |

5. Isolateur : continuité GND côté ADS, alim analogique 5 V du module
   pH, A2 = sortie isolée (pas l’entrée 5 V). Mesurer au voltmètre
   **sur A2** vs GND ADS : si le voltmètre dit 2.5 V et l’ADS 4.09 V
   (encore en gain 4.096), c’est le PGA. Si les deux disent ~5 V, c’est
   le câblage / le module.

### B. Jeter la courbe 2 points live

Dès que raw **bouge** sous le rail (idéalement 1.5–3.5 V selon tampons) :

1. Ne **pas** réutiliser 3.406 V.
2. Ne **pas** recoller 328/498/658 comme X.
3. Tampons 4.01 / 7.00 / 10.01 (pas « 7.4 du bassin » pour le point
   milieu — le 7.4 j5 était un **tampon / une vérité terrain**, le
   bassin d’aujourd’hui n’est pas un X).
4. Capture X = volt **stable** (mode calib 1 s, filtres off) → Y =
   valeur tampon → 3 points → `piecewise` ou `polynomial` order 2 →
   **Save**.
5. Contrôle : aux 3 tampons, raw ≠ 4.09 V, et pH publié ≈ Y. Puis
   bassin vs mètre.

Les X **ne ressembleront** aux 1.603 / 2.434 / 3.216 **que si**
module + isolateur sont 1:1 vs l’époque Arduino. Ce n’est pas un
objectif ; c’est un test de câblage.

### C. Filtres (après A+B seulement)

```yaml
filters:
  filter_samples: 5
  max_jump: 0.4
  max_jump_streak: 5
  # Pas de value_min/max si tu veux le rejet j5 : ici ça clampe.
```

Un clamp `value_max: 9` aurait **caché** un 13 au rail ; 8.709 passe.

### D. Sel / électrolyseur (en dernier)

Couper l’électrolyseur 15 min **après** un raw non saturé. Si raw
reste à 4.09 V, ce n’est pas le sel. Lot 8 (`shared_noise`,
`coincident_jump`) signale l’EMI, il ne corrige pas le pH.

### E. Arduino encore dispo (optionnel)

Même sonde, même module :

| | Uno A5 | ADS A2 gain 6.144 |
|--|--------|-------------------|
| @ pH 7.4 (mètre) | ADC ≈ 498 ? | V ≈ 2.43 ? |

Oui + oui → l’échelle est revenue ; seul le NVS 2 pts / le gain 4.096
étaient en cause.  
ADC 498 mais ADS 5 V → isolateur / entrée ADS.  
Les deux au rail → module / sonde / 5 V sur le signal.

---

## 6. Classement des causes (live)

| Rang | Cause | Fit live |
|------|--------|----------|
| **1** | **Rail gain 4.096** (4.094 V) | Explique un raw plat et toute calib aveugle |
| **2** | **Point 7.4 à 3.406 V** (vs 2.43 V j5) + **2 pts only** | Explique 8.709 = extrapole au rail, pente trop plate |
| **3** | Isolateur / alim 5 V / entrée ouverte | Explique *pourquoi* on est au rail ou à 3.4 V |
| **4** | Seed 3 pts écrasé par NVS (Save 2 pts / v0.7.16 key) | Explique l’absence du 10.01 |
| 5 | Sel / électrolyseur | Secondaire tant que raw = FSR |
| 6 | Filtres / T / `precision` | N’inventent pas 8.709 |
| 7 | Bug `calibrate()` | Écarté : 8.709 = la droite |

---

## 7. Références code

**j5** ([echavet/j5_ha_bridge](https://github.com/echavet/j5_ha_bridge))

- `MQTTSensor.applyCalibration` / `handleChange`
- `CustomIO.logBoardFirmwareIdentity` (`RESOLUTION.ADC === 1023`)
- README : `x_point` = ADC, `freq` en ms

**pool_station**

- Pipeline : `PoolStationChannelSensor::on_source_value_`
- Droite 2 pts : `calibrate_linear_`
- Prefs écrasent YAML : `load_from_preferences`
- Exemple gain : `examples/pool-station-minimal.yaml` (`gain: 4.096`)

---

## 8. Voir aussi

- [MIGRATION.md](MIGRATION.md) — table j5 (ADC vs volts ; plus de `scale`/`fsr`)
- [CHANGELOG.md](../CHANGELOG.md) — clé NVS v0.7.16
