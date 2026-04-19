# Marcador de Padel BLE per Android

Projecte per a `ESP32-C3 SuperMini` amb:

- `1 boto` integrat `BOOT`
- connexio `BLE` pensada per `Android`
- marcador de padel amb `punts` i `jocs`
- app Android nativa dins [android-app](d:\marcador2\android-app)

## Funcionament

- `1 clic curt`: suma `+1` a la `Parella A`
- `2 clics rapids`: suma `+1` a la `Parella B`
- puntuacio de padel: `0`, `15`, `30`, `40`, `Ad`
- quan una parella guanya el punt decisiu, suma `1 joc`

## Bluetooth

L'ESP32 anuncia aquest nom:

- `MarcadorPadel-BLE`

Servei BLE:

- `Service UUID`: `4fafc201-1fb5-459e-8fcc-c5c9c331914b`

Caracteristiques BLE:

- `State UUID`: `beb5483e-36e1-4688-b7f5-ea07361b26a8`
- `Command UUID`: `e3223119-9445-4e96-a4a1-85358c4046a2`

La caracteristica `State` retorna JSON, per exemple:

```json
{"teamAPoints":"15","teamAGames":2,"teamBPoints":"30","teamBGames":1}
```

La caracteristica `Command` accepta aquestes ordres:

- `A` o `1` -> punt per a la `Parella A`
- `B` o `2` -> punt per a la `Parella B`
- `R` o `0` -> `reset`

## Android

### Opcio 1: prova rapida amb nRF Connect

1. installa `nRF Connect for Mobile`
2. busca el dispositiu `MarcadorPadel-BLE`
3. connecta't
4. entra al servei `4fafc201-1fb5-459e-8fcc-c5c9c331914b`
5. activa `Notify` a la caracteristica `State`
6. escriu `A`, `B` o `R` a la caracteristica `Command`

### Opcio 2: app Android nativa

L'app Android es troba a [android-app](d:\marcador2\android-app).

Que fa:

- busca automaticament `MarcadorPadel-BLE`
- es connecta per BLE
- mostra `punts` i `jocs`
- envia `Punt A`, `Punt B` i `Reset`
- rep l'estat per `notify`
- fa una lectura periodica de suport per evitar que alguns Androids quedin sense refresc en viu

Passos:

1. obre `android-app` amb `Android Studio`
2. deixa que faci la sincronitzacio de `Gradle`
3. instal-la al telefon Android
4. concedeix permisos de `Bluetooth`
5. encen el marcador BLE
6. l'app hauria de trobar `MarcadorPadel-BLE` i connectar-s'hi sola

Fitxers principals de l'app:

- UI: [MainActivity.kt](d:\marcador2\android-app\app\src\main\java\com\marcador\padelble\MainActivity.kt)
- BLE: [ScoreBleManager.kt](d:\marcador2\android-app\app\src\main\java\com\marcador\padelble\ScoreBleManager.kt)
- estat: [ScoreState.kt](d:\marcador2\android-app\app\src\main\java\com\marcador\padelble\ScoreState.kt)

## Boto integrat

Per defecte, el projecte fa servir el boto `BOOT` del `ESP32-C3 SuperMini`.

- acostuma a estar connectat a `GPIO9`
- no mantinguis `BOOT` premut mentre la placa arrenca

Si prefereixes un boto extern, canvia `SCORE_BUTTON_PIN` a [src/main.cpp](d:\marcador2\src\main.cpp).

## Compilar i pujar el firmware

```bash
pio run
pio run -t upload
pio device monitor
```

## Fitxers principals

- firmware ESP32: [src/main.cpp](d:\marcador2\src\main.cpp)
- configuracio PlatformIO: [platformio.ini](d:\marcador2\platformio.ini)
- app Android: [android-app](d:\marcador2\android-app)
