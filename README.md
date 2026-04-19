# Marcador de Padel amb ESP32-C3 SuperMini

Projecte base per fer un marcador Wi-Fi de padel amb:

- `ESP32-C3 SuperMini`
- `1 boto`
- web local accessible des del mobil

## Funcionament

- `1 clic curt`: suma `+1` a l'equip A
- `2 clics rapids`: suma `+1` a l'equip B
- puntuacio de padel: `0`, `15`, `30`, `40`, `Ad`
- quan una parella guanya el punt decisiu, suma `1 joc` i els punts tornen a zero
- des de la web tambe pots fer `+1` i `reset`

## Cablejat

Per defecte, el projecte fa servir el boto integrat `BOOT`.

- `BOOT` acostuma a estar connectat a `GPIO9`
- `RESET` reinicia la placa i no s'utilitza pel marcador

Important:

- si mantens `BOOT` premut mentre la placa arrenca o es reinicia, pots entrar en mode bootloader
- per a l'us normal del marcador, el boto `BOOT` es pot fer servir despres de l'arrencada sense problema

El firmware esta configurat per defecte aixi:

- `GPIO9` -> boto del marcador

Si prefereixes usar un boto extern en lloc del `BOOT`, canvia aquesta constant a [src/main.cpp](d:\marcador2\src\main.cpp):

- `SCORE_BUTTON_PIN`

## Compilar i pujar

Necessites `PlatformIO`.

```bash
pio run -t upload
pio device monitor
```

## Connexio des del mobil

Quan l'ESP32 arrenca, crea aquesta xarxa:

- `SSID`: `Marcador-ESP32`
- `Password`: cap, es una xarxa oberta
- `Channel`: `1`
- `Bandwidth`: `HT20`

Despres obre al mobil:

```text
http://192.168.4.1
```

El firmware tambe activa una redireccio tipus `captive portal` per ajudar alguns mobils
a mantenir-se a la Wi-Fi local del marcador.

## Fitxer principal

La logica es a [src/main.cpp](d:\marcador2\src\main.cpp).
