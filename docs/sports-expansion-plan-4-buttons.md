# Pla per afegir mes esports amb una ESP32-C3 Mini i 4 botons

## Objectiu

Definir una versio del sistema pensada per una `ESP32-C3 Mini` amb `4 botons fisics`, de manera que:

- es puguin suportar `mes esports`
- es pugui canviar d'esport des del `mobil Android`
- es puguin carregar esports nous a l'`ESP32`
- el control local amb botons sigui molt mes util que en la versio d'`1 boto`

## Diferencia clau respecte a la versio d'1 boto

Amb `4 botons`, l'`ESP32` ja no es nomes un control auxiliar.

Ara pot assumir una part important del control local del marcador.

Aixo permet:

- fer servir l'aparell sense dependre tant del mobil
- reduir gestos complicats com `doubleClick`
- tenir una UX mes clara en partit real

## Repartiment de responsabilitats

Amb `4 botons`, la millor estrategia es aquesta:

- `botons fisics`: accions de partit rapides i frequents
- `app Android`: configuracio, canvis d'esport, opcions avançades i esports complexos

## Arquitectura recomanada

Cal mantenir la mateixa separacio de capes:

1. `Engine de marcador`
2. `Definicio d'esport`
3. `Transport BLE + app Android`
4. `Input mapper de 4 botons`

### 1. Engine de marcador

Responsabilitats:

- mantenir l'estat del partit
- aplicar regles de puntuacio
- resetejar
- canviar d'esport
- exportar l'estat

### 2. Definicio d'esport

Responsabilitats:

- definir el model de puntuacio
- definir quines accions existeixen
- definir el mapatge dels `4 botons`

### 3. BLE + Android

Responsabilitats:

- seleccionar esport
- pujar esports nous
- mostrar estat
- oferir controls extra quan els `4 botons` no siguin suficients

### 4. Input mapper de 4 botons

Responsabilitats:

- llegir els `4 GPIO`
- aplicar `debounce`
- detectar `longPress` si cal
- traduir cada boto a una `accio` segons l'esport actiu

## Principi de disseny amb 4 botons

No s'ha d'intentar que tots els esports facin exactament el mateix mapatge.

Cada esport ha de definir quin us dona mes valor als `4 botons`.

La regla bona es:

- botons per a les `4 accions principals`
- la resta a l'app

## Mapatge generic recomanat

Com a perfil base, els 4 botons poden representar:

- `BTN_1`
- `BTN_2`
- `BTN_3`
- `BTN_4`

I opcionalment:

- `BTN_1_LONG`
- `BTN_2_LONG`
- `BTN_3_LONG`
- `BTN_4_LONG`

Per una primera versio, recomano:

- implementar `clic curt` per a tots 4
- deixar `longPress` nomes per `reset` o funcions molt controlades

## Exemples de mapatge per esport

### Padel

Mapatge recomanat:

- `BTN_1` -> `POINT_A`
- `BTN_2` -> `POINT_B`
- `BTN_3` -> `UNDO`
- `BTN_4` -> `RESET`

Alternativa si no vols `UNDO`:

- `BTN_1` -> `POINT_A`
- `BTN_2` -> `POINT_B`
- `BTN_3` -> `NEXT_SET_MODE` o funcio extra
- `BTN_4` -> `RESET`

### Tennis

- `BTN_1` -> `POINT_A`
- `BTN_2` -> `POINT_B`
- `BTN_3` -> `UNDO`
- `BTN_4` -> `RESET`

### Futbol Sala

- `BTN_1` -> `GOAL_A`
- `BTN_2` -> `GOAL_B`
- `BTN_3` -> `UNDO_A`
- `BTN_4` -> `UNDO_B`

Si vols reservar reset a long press:

- `BTN_4_LONG` -> `RESET`

### Basquet

Hi ha dues opcions bones.

Opcio A: control local minim i clar

- `BTN_1` -> `ADD1_A`
- `BTN_2` -> `ADD1_B`
- `BTN_3` -> `SUB1_A`
- `BTN_4` -> `SUB1_B`

L'app Android faria:

- `+2`
- `+3`
- `reset`

Opcio B: orientada a puntuacio positiva

- `BTN_1` -> `ADD1_A`
- `BTN_2` -> `ADD2_A`
- `BTN_3` -> `ADD1_B`
- `BTN_4` -> `ADD2_B`

I l'app faria:

- `+3`
- `-1`
- `reset`

La recomanacio practica es l'`Opcio A`, perquè és mes coherent en ús real.

### Volei

- `BTN_1` -> `POINT_A`
- `BTN_2` -> `POINT_B`
- `BTN_3` -> `UNDO`
- `BTN_4` -> `RESET`

### Hoquei o Handbol

- `BTN_1` -> `GOAL_A`
- `BTN_2` -> `GOAL_B`
- `BTN_3` -> `UNDO_A`
- `BTN_4` -> `UNDO_B`

## Canvi important al model JSON

Ara cada esport hauria d'incloure un `inputProfile` per a `4 botons`.

Exemple:

```json
{
  "id": "padel",
  "name": "Padel",
  "scoreMode": "sequence_with_advantage",
  "commands": [
    "POINT_A",
    "POINT_B",
    "UNDO",
    "RESET"
  ],
  "inputProfile": {
    "BTN_1": "POINT_A",
    "BTN_2": "POINT_B",
    "BTN_3": "UNDO",
    "BTN_4": "RESET"
  }
}
```

Exemple per futbol sala:

```json
{
  "id": "futsal",
  "name": "Futbol Sala",
  "scoreMode": "integer_counter",
  "commands": [
    "GOAL_A",
    "GOAL_B",
    "UNDO_A",
    "UNDO_B",
    "RESET"
  ],
  "inputProfile": {
    "BTN_1": "GOAL_A",
    "BTN_2": "GOAL_B",
    "BTN_3": "UNDO_A",
    "BTN_4": "UNDO_B",
    "BTN_4_LONG": "RESET"
  }
}
```

## GPIO i maquinari

Si la placa te `4 botons externs`, la connexio recomanada es:

- cada boto entre `GPIO` i `GND`
- configurar com `INPUT_PULLUP`
- evitar `strapping pins` si es pot

En una `ESP32-C3 Mini`, els GPIO exactes dependran de la placa concreta, pero la recomanacio es:

- reservar `GPIO` normals per als 4 botons
- evitar fer servir el boto `BOOT` com a boto principal si ja tens 4 botons externs

## Canvis necessaris al firmware

## 1. Nova capa d'entrada

Afegir alguna cosa com:

- `src/input/ButtonInput.h`
- `src/input/ButtonInput.cpp`

Amb:

- lectura de 4 botons
- `debounce`
- deteccio de `short press`
- deteccio opcional de `long press`

## 2. Input mapper generic

Afegir:

- `src/input/InputMapper.h`
- `src/input/InputMapper.cpp`

Aquest modul ha de fer:

- llegir quin esport esta actiu
- consultar el seu `inputProfile`
- convertir `BTN_1`, `BTN_2`, `BTN_3`, `BTN_4` en comandes del `ScoreEngine`

## 3. Engine generic

Igual que en el pla general:

- `ScoreEngine`
- `SportDefinition`
- `SportStorage`

Pero ara ja no depen del `single/double click`, sinó d'events de botons clars.

## Canvis necessaris a BLE

El servei BLE pot ser molt semblant al del pla general.

Caracteristiques recomanades:

- `State`
- `Command`
- `SportList`
- `ActiveSport`
- `InputProfile`
- `SportUpload`
- `SportUploadControl`

La diferencia principal es que `InputProfile` ara ha de descriure `4 botons`.

## Canvis necessaris a l'app Android

L'app ha d'adaptar la UI segons dues coses:

1. l'esport actiu
2. quines accions ja estan cobertes pels `4 botons`

Per tant, la UI hauria de:

- mostrar l'estat del marcador
- mostrar l'esport actiu
- mostrar accions extra que no estan als botons
- permetre configurar o importar esports nous

Exemple:

### Padel

Si els 4 botons ja fan:

- `Punt A`
- `Punt B`
- `Undo`
- `Reset`

L'app pot quedar molt simple:

- veure marcador
- canviar esport

### Basquet

Si els botons fan:

- `+1 A`
- `+1 B`
- `-1 A`
- `-1 B`

L'app ha d'afegir:

- `+2 A`
- `+3 A`
- `+2 B`
- `+3 B`
- `Reset`

## Carregar esports nous des del mobil

La recomanacio segueix sent la mateixa:

- guardar-los com a `JSON`
- enviar-los per `BLE` en fragments
- validar-los a l'ESP32
- guardar-los a `LittleFS`

Flux:

1. l'usuari selecciona o crea un esport a l'app
2. l'app valida el JSON
3. l'app envia `BEGIN`
4. l'app envia fragments
5. l'app envia `END`
6. l'ESP32 valida
7. l'ESP32 guarda
8. l'usuari pot activar l'esport

## Validacions importants per a la versio 4 botons

Cada esport nou hauria de validar:

- `id` unic
- `name` no buit
- `scoreMode` conegut
- `commands` no buides
- `inputProfile` valid
- que cada `BTN_n` apunti a una accio existent
- que no hi hagi conflictes absurds com 4 botons fent exactament el mateix sense motiu

## Estrategia recomanada d'implementacio

### Fase 1

Objectiu: suportar esports predefinits amb `4 botons`.

Fer:

- refactor del firmware a `ScoreEngine`
- afegir capa de `ButtonInput`
- afegir `InputMapper`
- definir esports base:
  - `padel`
  - `futbol sala`
  - `basquet`
- afegir `ActiveSport` i `SportList` per BLE
- app Android amb selector d'esport

### Fase 2

Objectiu: pujar esports nous des del mobil.

Fer:

- `SportUpload`
- validacio JSON
- guardat a `LittleFS`
- activacio immediata des de l'app

### Fase 3

Objectiu: UI Android adaptativa per esport.

Fer:

- controls dinamics segons `commands`
- vista del mapatge dels 4 botons
- importacio/exportacio de perfils

## Recomanacio final

Amb `4 botons`, el projecte guanya molt de valor.

La ruta tecnica mes bona es:

1. convertir el marcador en un `engine generic`
2. definir `inputProfile` per `4 botons`
3. afegir esports predefinits
4. afegir seleccio d'esport des del mobil
5. despres afegir pujada de JSON des de l'app

En aquesta variant, l'`ESP32` ja pot ser realment usable per si sola durant el partit, i l'app Android passa a ser sobretot:

- configurador
- visor
- extensio per a accions avançades

## Fitxers del projecte relacionats

- firmware actual: [src/main.cpp](d:\marcador2\src\main.cpp)
- app Android actual: [android-app](d:\marcador2\android-app)
- pla general d'1 boto: [sports-expansion-plan.md](d:\marcador2\docs\sports-expansion-plan.md)
