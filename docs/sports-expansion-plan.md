# Pla per afegir mes esports i carregar-los des del mobil a l'ESP32

## Objectiu

Fer que el sistema actual deixi de ser un marcador fix de padel i passi a ser un marcador generic capac de:

- suportar `mes esports`
- canviar d'esport des del `mobil Android`
- carregar una configuracio nova d'esport directament a l'`ESP32`
- guardar aquesta configuracio a memoria no volatil
- recuperar l'ultim esport actiu en reiniciar

## Restriccio clau del projecte

Aquest projecte nomes te `1 boto usable` a l'`ESP32-C3 SuperMini`.

Aixo canvia completament el disseny:

- l'`ESP32` no pot ser la interfície principal per a tots els esports
- el `mobil Android` ha de ser la interfície principal
- el `boto fisic` ha de quedar com a control rapid auxiliar

Conclusio practica:

- el boto fisic ha de servir nomes per a les accions mes frequents
- les accions secundaries o complexes han d'anar a l'app Android

## Estat actual

Ara mateix el firmware de l'ESP32:

- te la logica de `padel` codificada directament a [src/main.cpp](d:\marcador2\src\main.cpp)
- exposa un servei `BLE`
- envia l'estat del marcador per una caracteristica `State`
- rep ordres simples `A`, `B` i `R` per la caracteristica `Command`

Ara mateix l'app Android:

- busca `MarcadorPadel-BLE`
- es connecta al servei BLE
- mostra `punts` i `jocs`
- envia ordres de marcador

## Canvi d'arquitectura necessari

Cal separar tres capes:

1. `Engine de marcador`
2. `Definicio d'esport`
3. `Transport BLE + app Android`

### 1. Engine de marcador

Ha de ser generic i independent del transport.

Responsabilitats:

- mantenir l'estat del partit
- aplicar les regles de puntuacio
- resetejar
- canviar d'esport
- exportar l'estat com a JSON
- resoldre quina accio correspon al `boto unic`

### 2. Definicio d'esport

Ha de descriure com funciona un esport sense haver de recompilar.

Responsabilitats:

- nom de l'esport
- etiquetes d'equip o jugador
- sistema de punts
- sets, jocs, parts o quarts
- condicions de victoria
- accions permeses
- mapa d'accions del `boto fisic unic`

### 3. BLE + app Android

Ha de servir per:

- enviar accions del marcador
- seleccionar un esport guardat
- pujar un esport nou a l'ESP32
- llegir la llista d'esports disponibles
- llegir com esta mapejat el `boto unic`

## Principi de disseny per al boto unic

El boto fisic ha de cobrir nomes les accions que es fan mes sovint durant el partit.

Mapa recomanat:

- `singleClick`
- `doubleClick`
- `longPress`

Si algun dia cal, es podria afegir:

- `veryLongPress`

Pero no recomano anar mes lluny en una primera versio.

## Exemples de mapatge del boto unic

### Padel

- `singleClick` -> `POINT_A`
- `doubleClick` -> `POINT_B`
- `longPress` -> `RESET`

### Futbol Sala

- `singleClick` -> `GOAL_A`
- `doubleClick` -> `GOAL_B`
- `longPress` -> `RESET`

### Basquet

Com que el basquet te mes accions, la proposta realista es:

- `singleClick` -> `ADD1_A`
- `doubleClick` -> `ADD1_B`
- `longPress` -> `RESET`

I a l'app Android:

- `+2 A`
- `+3 A`
- `+2 B`
- `+3 B`
- `-1 A`
- `-1 B`

Conclusio:

- en esports simples, el boto pot cobrir gairebe tot
- en esports complexos, el boto es nomes un `shortcut`

## Recomanacio de model de dades

La millor opcio per aquest projecte es guardar cada esport com un fitxer `JSON`.

Exemple per padel:

```json
{
  "id": "padel",
  "name": "Padel",
  "sides": 2,
  "scoreMode": "sequence_with_advantage",
  "pointSequence": ["0", "15", "30", "40"],
  "useAdvantage": true,
  "tracks": [
    {
      "id": "games",
      "label": "Jocs",
      "winAt": 6,
      "winBy": 2,
      "resetsLowerTrack": true
    }
  ],
  "commands": [
    "POINT_A",
    "POINT_B",
    "RESET"
  ],
  "inputProfile": {
    "singleClick": "POINT_A",
    "doubleClick": "POINT_B",
    "longPress": "RESET"
  }
}
```

Exemple per basquet:

```json
{
  "id": "basket",
  "name": "Basquet",
  "sides": 2,
  "scoreMode": "integer_counter",
  "commands": [
    "ADD1_A",
    "ADD2_A",
    "ADD3_A",
    "ADD1_B",
    "ADD2_B",
    "ADD3_B",
    "SUB1_A",
    "SUB1_B",
    "RESET"
  ],
  "inputProfile": {
    "singleClick": "ADD1_A",
    "doubleClick": "ADD1_B",
    "longPress": "RESET"
  }
}
```

Exemple per futbol sala:

```json
{
  "id": "futsal",
  "name": "Futbol Sala",
  "sides": 2,
  "scoreMode": "integer_counter",
  "commands": [
    "GOAL_A",
    "GOAL_B",
    "UNDO_A",
    "UNDO_B",
    "RESET"
  ],
  "inputProfile": {
    "singleClick": "GOAL_A",
    "doubleClick": "GOAL_B",
    "longPress": "RESET"
  }
}
```

## Modes de puntuacio recomanats

Per no complicar massa el firmware, convé definir pocs modes generics:

- `integer_counter`
  Pensat per futbol, basquet, handbol, hoquei

- `sequence_counter`
  Pensat per esports on els punts segueixen una llista fixa

- `sequence_with_advantage`
  Pensat per tennis i padel

- `best_of_sets`
  Pensat per volei, tennis, padel

- `period_based`
  Pensat per esports amb parts o quarts

La recomanacio es implementar primer:

1. `integer_counter`
2. `sequence_with_advantage`

Amb aquests dos ja cobreixes molts casos.

## On guardar els esports a l'ESP32

### Opcio recomanada: `LittleFS`

Guardar cada esport com:

- `/sports/padel.json`
- `/sports/basket.json`
- `/sports/futsal.json`

Avantatges:

- facil de depurar
- facil d'actualitzar des del mobil
- no obliga a recompilar
- permet backup i restauracio

També convé guardar un fitxer petit de configuracio:

- `/config/active_sport.json`

Exemple:

```json
{
  "activeSportId": "padel"
}
```

### Opcio secundaria: `Preferences` o `NVS`

Bo per:

- guardar quin esport esta actiu
- guardar l'ultim marcador

No es la millor opcio per fitxers grans o estructures canviants.

## Canvis necessaris al firmware ESP32

## 1. Refactor intern

Cal treure la logica de padel de [src/main.cpp](d:\marcador2\src\main.cpp) i repartir-la en moduls:

- `src/score/ScoreEngine.h`
- `src/score/ScoreEngine.cpp`
- `src/score/SportDefinition.h`
- `src/score/SportDefinition.cpp`
- `src/storage/SportStorage.h`
- `src/storage/SportStorage.cpp`
- `src/ble/BleProtocol.h`
- `src/ble/BleProtocol.cpp`

## 2. Capa de persistencia

Funcions minimes:

- `listSports()`
- `loadSport(id)`
- `saveSport(definition)`
- `deleteSport(id)`
- `setActiveSport(id)`
- `getActiveSportId()`

## 3. Capa BLE nova

El servei BLE actual s'hauria d'ampliar.

Caracteristiques recomanades:

- `State`
  Estat actual del marcador

- `Command`
  Accions de joc: `POINT_A`, `POINT_B`, `RESET`, etc.

- `SportList`
  Retorna la llista d'esports disponibles

- `ActiveSport`
  Permet llegir i canviar l'esport actiu

- `InputProfile`
  Permet llegir quin mapa d'accions te el boto fisic

- `SportUpload`
  Permet pujar JSON d'un esport nou

- `SportUploadControl`
  Permet iniciar, confirmar o cancelar una pujada

## Protocol BLE recomanat

### Opcio simple per fitxers petits

Enviar el JSON sencer en fragments per `write` sobre `SportUpload`.

Control:

- `BEGIN:<sportId>:<totalBytes>`
- `CHUNK:<index>:<payload>`
- `END`
- `COMMIT`
- `CANCEL`

Resposta de l'ESP32:

- `ACK_BEGIN`
- `ACK_CHUNK:<index>`
- `ACK_END`
- `ACK_COMMIT`
- `ERR:<reason>`

### Recomanacio practica

Per no complicar-ho massa:

- limit inicial de `4 KB` per esport
- fragments de `120` a `180 bytes`
- validacio JSON abans de guardar

## Flux d'usuari des del mobil

### Cas 1: canviar a un esport ja guardat

1. l'app llegeix `SportList`
2. l'usuari tria l'esport
3. l'app escriu el nou `id` a `ActiveSport`
4. l'ESP32 el carrega
5. l'ESP32 aplica el `inputProfile` del boto unic
6. l'app adapta la UI segons l'esport actiu
7. l'ESP32 envia estat nou per `State`

### Cas 2: pujar un esport nou

1. l'usuari omple o importa una definicio d'esport a l'app
2. l'app valida el JSON localment
3. l'app envia `BEGIN`
4. l'app envia fragments a `SportUpload`
5. l'app envia `END`
6. l'ESP32 valida el JSON
7. si es valid, el guarda a `LittleFS`
8. l'app pot activar-lo immediatament amb `ActiveSport`

## Canvis necessaris a l'app Android

## 1. Pantalla principal

Afegir:

- selector d'esport actiu
- boto `Gestionar esports`
- controls dinamics segons l'esport actiu

## 2. Pantalla de llista d'esports

Ha de permetre:

- veure esports disponibles
- activar un esport
- eliminar un esport personalitzat

## 3. Pantalla d'ediccio o importacio

Dues opcions:

- editor formulari
- importacio de fitxer JSON

La recomanacio es:

- primer `importar JSON`
- despres, si cal, fer un editor visual

## 4. Capa BLE Android

Cal afegir noves operacions a [android-app/app/src/main/java/com/marcador/padelble/ScoreBleManager.kt](d:\marcador2\android-app\app\src\main\java\com\marcador\padelble\ScoreBleManager.kt):

- `readSportList()`
- `readActiveSport()`
- `readInputProfile()`
- `setActiveSport(id)`
- `uploadSport(json)`
- `deleteSport(id)`

## Estrategia recomanada d'implementacio

### Fase 1

Objectiu: suportar `mes esports predefinits` sense editor.

Fer:

- refactor del firmware a `ScoreEngine`
- afegir `LittleFS`
- incloure 3 esports base: `padel`, `basquet`, `futbol sala`
- afegir `ActiveSport` i `SportList` per BLE
- afegir `inputProfile` per al `boto unic`
- app Android amb selector d'esport

### Fase 2

Objectiu: carregar un esport nou des del mobil.

Fer:

- afegir `SportUpload`
- pujada de JSON en fragments
- validacio i guardat a `LittleFS`
- importacio de fitxer JSON a l'app Android

### Fase 3

Objectiu: editor d'esports al mobil.

Fer:

- formulari visual per crear esports
- vista previa del marcador
- exportacio i importacio

## Validacions importants

Cada esport nou hauria de validar:

- `id` unic
- `name` no buit
- `sides = 2` en la primera versio
- `commands` no buides
- `scoreMode` conegut
- `inputProfile` valid
- si hi ha sequencia, que tingui almenys 2 valors
- si hi ha `tracks`, que estiguin ben definits

Si falla la validacio:

- no s'ha de guardar el fitxer
- s'ha de retornar un `ERR` clar al mobil

## Limitacions inicials recomanades

Per mantenir el projecte controlable, val la pena limitar la primera versio a:

- `2 equips o 2 jugadors`
- `1 boto fisic unic`
- sense rellotge de partit al principi
- sense imatges ni icones
- sense sincronitzacio amb internet
- mida maxima de definicio: `4 KB`

## Exemples d'esports que encaixen be

Facils d'afegir:

- `Padel`
- `Tennis`
- `Basquet`
- `Futbol Sala`
- `Handbol`
- `Hoquei`

Mes complexos:

- `Volei`
- `Tenis taula`
- `Badminton`

## Recomanacio final

La millor ruta tecnica es:

1. convertir el marcador actual en un `engine generic`
2. definir un `inputProfile` clar per al `boto unic`
3. guardar esports com a `JSON` a `LittleFS`
4. afegir `ActiveSport` i `SportList` per `BLE`
5. fer que l'app Android pugui `seleccionar esport`
6. despres afegir `upload` de definicions noves

No recomano començar directament per un editor visual complex. El cami mes pragmatic es:

- primer esports predefinits
- despres importacio JSON
- despres editor visual

## Fitxers del projecte relacionats

- firmware actual: [src/main.cpp](d:\marcador2\src\main.cpp)
- app Android actual: [android-app](d:\marcador2\android-app)
- gestor BLE Android actual: [ScoreBleManager.kt](d:\marcador2\android-app\app\src\main\java\com\marcador\padelble\ScoreBleManager.kt)
