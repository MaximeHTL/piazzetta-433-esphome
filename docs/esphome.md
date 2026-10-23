# The ESPHome component

`esphome/components/piazzetta/` is an ESPHome **external component** (C++ + Python codegen). It reuses the radio code validated on the test bench. Tested with ESPHome 2026.9, Arduino framework, RadioLib 7.8.1 (pulled automatically).

## Installation

1. Copy `esphome/piazzetta-stove.yaml` and the `esphome/components/` folder into your ESPHome configuration folder (with the ESPHome dashboard/add-on: `/config/esphome/` or the folder mounted as `/config`).
   Alternatively reference the component from GitHub (see the commented `external_components` block in the YAML).
2. Add to ESPHome's `secrets.yaml`: `wifi_ssid`, `wifi_password`, `stove_api_key` (32-byte base64), `stove_ota_password`, `stove_ap_password`.
3. Set **`remote_id`** (found with the test bench).
4. **First install over USB** (the test-bench firmware is replaced). From the ESPHome dashboard: *Install → Manual download → Factory format*, then flash with [ESPHome Web](https://web.esphome.io) or `esptool.py --chip esp32 write_flash 0x0 firmware.factory.bin`. Later updates are wireless (OTA).
5. In Home Assistant: *Settings → Devices & services → Add integration → ESPHome*, host = the module's IP (mDNS discovery works only if ESPHome/HA can see the module's mDNS), then the API encryption key.

## Options

| Option | Default | |
| --- | --- | --- |
| `remote_id` | `0xC5` | first byte of the remote frames |
| `frequence` | `433.92` | MHz |
| `puissance_emission` | `10` | transmit power, dBm (2–17) |
| `seuil_rssi` | `-78` | a frame is captured only if RSSI exceeds this (the OOK slicer chatters in noise) |
| `intervalle_interrogation` | `30s` | polling period |
| `pin_sck`… `pin_dio2` | 18, 19, 23, 5, 14, 26, 33, 32 | wiring |

## Entities

| YAML key | Type | Content |
| --- | --- | --- |
| `temperature` | sensor °C | room temperature shown by the remote (from the stove's main screen) |
| `phase` | text | main screen text only (`NIVEAU P1`, `CONTROLE`, `ALLUMAGE PHASE I`, `NETTOYAGE BRASERO`, `OFF`…) — menus excluded, clean history |
| `etat` | text | category: `Arrêt` (off), `Allumage` (igniting), `Marche` (running), `Nettoyage` (burn-pot cleaning / shutting down), `Extinction`, `Défaut` (error), `Autre` |
| `ecran_ligne1`, `ecran_ligne2`, `ecran_bas` | text | live copy of the remote screen, menus included |
| `heure` | text | stove clock (diagnostic) |
| `rssi` | sensor dBm | radio level of the stove's frames |
| `joignable` | binary (connectivity) | a stove answer was received in the last 2 min |
| `puissance` | number 1–4 | **set** = target; the module opens the menu and steps until reached |
| `ventilateur` | number 1–4 | same |
| `interroger` | button | poll now |
| `marche_arret` | button | **toggle** = long press on ⏻ (on if off, off if running, re-ignite during cleaning) |

The state values are in French (they come from the French screen texts); see the mapping above for templates and automations.

## Behaviour

* **Listens permanently**: every stove answer — including answers to the real remote — updates the entities.
* **Polls** every 30 s, **except** when the real remote was heard in the last 10 s (its own polls are enough; avoids collisions).
* **Start-up synchronisation**: the stove only sends its full screen when something changed (see [protocol.md](protocol.md) §5). If no main screen was received 20 s after boot, the module presses the **fan key once** (no +/−): the stove sends its fan menu, the menu times out, and the next poll returns the full main screen. Nothing is changed on the stove.
* **Setting a value** (power/fan): sends the **menu key** (a key always triggers a full screen; a poll may not), reads the answer; if it is not the adjustment screen (the key is a toggle), presses again (max 4 tries); then sends `+`/`−` one step at a time, reading the value after each step (max 8 steps). Gives up cleanly and republishes the real value.
* **One radio command at a time**; requests during a running command are ignored (logged).
* **On/off**: `81` then 11 × `61` every 350 ms, as the real remote. There is **no state guard in the component** (you may need to force a stop after a failed ignition); put the guards in Home Assistant (see [home-assistant.md](home-assistant.md)).
* **After each transmission the radio is fully re-initialised** (otherwise the receiver may stay desensitised, see lessons-learned).
* Logs: every decoded frame at DEBUG level (`Trame -58 dBm : B5 …`), screens at INFO (`Ecran : NIVEAU P1`), a warning if a remote with another `remote_id` is heard.

## Resources

On the reference build: RAM 38 %, flash 51 % (ESP32, 4 MB). Each transmission blocks the loop ≈ 40–60 ms (ESPHome may log "took a long time", harmless).
