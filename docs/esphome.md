# The ESPHome component

`esphome/components/piazzetta/` is an ESPHome **external component** (C++ + Python codegen). It reuses the radio code validated on the test bench. Tested with ESPHome 2026.9 and 2026.10, Arduino framework, RadioLib 7.8.1 (pulled automatically).

## Installation

1. Copy `esphome/piazzetta-stove.yaml` and the `esphome/components/` folder into your ESPHome configuration folder (with the ESPHome dashboard/add-on: `/config/esphome/` or the folder mounted as `/config`).
   Alternatively reference the component from GitHub (see the commented `external_components` block in the YAML).
2. Add to ESPHome's `secrets.yaml`: `wifi_ssid`, `wifi_password`, `stove_api_key` (32-byte base64), `stove_ota_password`, `stove_ap_password`.
3. Set **`remote_id`** (found with the test bench).
4. Keep the two settings of the example YAML outside the `piazzetta:` block:
   * `api: reboot_timeout: 0s` — by default ESPHome reboots the module after 15 min without Home Assistant. A reboot loses the on/off toggle lock (see below) and the start-up resynchronisation briefly opens the stove's fan menu; with an unreliable Home Assistant server this happened again and again.
   * `wifi: power_save_mode: none` — the Wi-Fi radio stays awake instead of waking up in bursts, a possible source of glitches during 433 MHz receptions (the module is mains-powered anyway).
5. **First install over USB** (the test-bench firmware is replaced). From the ESPHome dashboard: *Install → Manual download → Factory format*, then flash with [ESPHome Web](https://web.esphome.io) or `esptool.py --chip esp32 write_flash 0x0 firmware.factory.bin`. Later updates are wireless (OTA).
6. In Home Assistant: *Settings → Devices & services → Add integration → ESPHome*, host = the module's IP (mDNS discovery works only if ESPHome/HA can see the module's mDNS), then the API encryption key.

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
| `temperature` | sensor °C | temperature shown by the remote (from the stove's main screen). ⚠️ The stove **alternates** its internal set-point and the room temperature in this field and nothing in the frame tells them apart: of limited use, prefer your own room sensor |
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
| `bascule_en_attente` | binary (diagnostic) | **on** from an on/off press until a main screen shows the new state (toggle lock, max 20 min) |
| `age_ecran` | sensor s (diagnostic) | seconds since the last main screen was received (published every 60 s); should stay under 30 min |
| `trames_rejetees` | sensor (diagnostic) | frames of 5 bytes or more rejected since boot (bad checksum, parity or stop bit, unknown type) |
| `trames_corrigees` | sensor (diagnostic) | frames accepted since boot after removing one stray bit (see *Reception* below) |

The state values are in French (they come from the French screen texts); see the mapping above for templates and automations.

## Behaviour

* **Listens permanently**: every stove answer — including answers to the real remote — updates the entities.
* **Polls** every 30 s, **except** when the real remote was heard in the last 10 s (its own polls are enough; avoids collisions).
* **Start-up synchronisation**: the stove only sends its full screen when something changed (see [protocol.md](protocol.md) §5). If no main screen was received 20 s after boot, the module presses the **fan key once** (no +/−): the stove sends its fan menu, the menu times out, and the next poll returns the full main screen. Nothing is changed on the stove.
* **Periodic resynchronisation**: the same fan-key trick is repeated whenever no main screen has been received for **5 min** during a transition (ignition, cleaning, shutdown…), **30 min** while running or off, **45 s** after an on/off, and every **2 min** while an on/off is waiting for confirmation. Every main screen received postpones it. Reason: a single missed screen frame left Home Assistant stuck on an outdated phase, since the stove then only sends its clock. Side effect: the fan menu briefly opens on the remote, at most every 30 min when nothing changes.
* **Setting a value** (power/fan): sends the **menu key** (a key always triggers a full screen; a poll may not), reads the answer; if it is not the adjustment screen (the key is a toggle), presses again (max 4 tries); then sends `+`/`−` one step at a time, reading the value after each step (max 8 steps). Gives up cleanly and republishes the real value.
* **One radio command at a time**; requests during a running command are ignored (logged).
* **On/off**: `81` then 11 × `61` every 350 ms, as the real remote. There is **no state guard in the component** (you may need to force a stop after a failed ignition); put the guards in Home Assistant (see [home-assistant.md](home-assistant.md)). The component only protects against repeated toggles (the two points below).
* **On/off debounce**: after a long press, any new on/off request is **ignored for 90 s** (logged). The state only changes several seconds after the press, so Home Assistant's guards cannot stop a second request in between: two clicks plus the thermostat once sent three toggles in 47 s, which stopped the stove and restarted an ignition. A thermostat request ignored this way is retried later (keep_alive or next sensor update), since the heater switch did not change.
* **Toggle lock until confirmation**: after a long press, any new on/off request is also ignored (logged) until a main screen received **since the press** shows a known state (`etat` other than `Autre`) **different** from the one before the press. Ceiling: **20 min**, after which the lock is released with a warning (the command probably had no effect). The `bascule_en_attente` entity shows the lock. Reason: when the screen stayed frozen after a command, the thermostat sent the same toggle again at every keep_alive (5 min), switching the stove on and off. A long press on the real remote is not affected.
* **Reception**: every byte's **parity and stop bit** are checked in addition to the frame checksum, and a frame with a defect is rejected — except one case that is **corrected**: a single byte with a wrong parity (stop bit correct) **and** a checksum difference equal to one `1` bit of that byte means one stray bit was added by a glitch during a silence; that bit is removed (DEBUG log `(bit parasite corrige)`, counter `trames_corrigees`). Before decoding, glitches shorter than **0.45 bit** (187 µs) are merged with their neighbours, shortest first. The RSSI is read **only while the carrier is on**: an SPI access during a silence of a frame creates a glitch on DIO2. See [lessons-learned.md](lessons-learned.md).
* **After each transmission the radio is fully re-initialised** (otherwise the receiver may stay desensitised, see lessons-learned).
* Logs: every decoded frame at DEBUG level (`Trame -58 dBm : B5 …`), screens at INFO (`Ecran : NIVEAU P1`), a warning if a remote with another `remote_id` is heard.

## Resources

On the reference build: RAM 38 %, flash 51 % (ESP32, 4 MB). Each transmission blocks the loop ≈ 40–60 ms (ESPHome may log "took a long time", harmless).
