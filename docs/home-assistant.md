# Home Assistant setup

Files in `home-assistant/`. Entity IDs assume the example ESPHome config (friendly name "Pellet stove"); adapt them if yours differ (Settings → Entities).

## 1. Add the device

*Settings → Devices & services → Add integration → ESPHome* → the module's IP address (reserve it in your router) and the API encryption key. All entities of [esphome.md](esphome.md) appear under the device.

## 2. Thermostat

Goal: a heating entity like a heat pump's — set-point, hysteresis, presets — that switches the stove on/off from **any room temperature sensor** (the stove's own sensor sits next to the stove; a sensor in the living area is better).

### a) A protected heater switch

The thermostat needs a `switch`. The ESPHome on/off button is a **toggle**, so a naive switch would switch **off** a stove that is already on when asked to "turn on". The template switch of `package_piazzetta_stove.yaml` only presses the button when it is safe:

* state = on if the stove state is `Marche` (running) or `Allumage` (igniting);
* the switch **keeps its last state** when the stove state is `unknown`, `unavailable` or `Autre` (corrupted frame, clock or language menu), so that a module outage or an odd screen does not make the thermostat act;
* **turn on** presses ⏻ only if the state is `Arrêt` (off) **and** the last stop started **at least 1 h ago**;
* **turn off** presses ⏻ only if the state is `Marche` or `Allumage` **and** the last ignition started **at least 1 h ago** — and **never while the thermostat is set to Off** (see below);
* **toggle lock**: no press while the previous one, sent less than 20 min ago, has not been followed by any phase change, nor while the module reports `bascule_en_attente` (on/off pending confirmation);
* the guards are **closed by default**: a missing time stamp (lost database) blocks instead of allowing.

A request refused by a guard is not lost: the thermostat repeats its order at every keep_alive (5 min) and it goes through once the guard opens.

**Why the 1 h minimum cycle is not in the thermostat.** A pellet stove must not short-cycle (ignition ≈ 18–51 min, shutdown cleaning ≈ 40 min). The first version used the generic thermostat's *minimum cycle duration* (1 h). It was abandoned because that timer counts from the heater switch's last change **as seen by the running Home Assistant**: it restarts from zero at every Home Assistant restart (and template reload), so a restart could delay a needed shutdown by a full hour. The 1 h guard is therefore held on the stove's **real phases**, time-stamped in `input_datetime` helpers that survive restarts:

* `input_datetime.pellet_stove_last_ignition` — set when the state goes to `Allumage`/`Marche` from a stopped state, and on a re-ignition (`Marche` → `Allumage`);
* `input_datetime.pellet_stove_last_stop` — set when the state goes to `Nettoyage`/`Extinction`/`Arrêt`/`Défaut` from `Allumage`/`Marche`;
* returning from `unknown`/`unavailable`/`Autre` does not count, unless it reveals a missed transition: a missed ignition is stamped *now*, a missed stop *now* (or *now − 40 min* if the stove is already at `Arrêt`, never earlier than the last ignition);
* a **catch-up** automation compares the state with the time stamps 2 min after each Home Assistant start, then every 10 min, and fixes a missed transition (with a logbook line).

**Why the toggle lock.** Without the thermostat's minimum cycle, nothing stopped it from repeating an order: the generic thermostat re-sends its order at every keep_alive, and if the stove screen stayed frozen after a toggle (missed frames), the switch still showed the old state and the same toggle was sent again 5 min later — switching the stove back. Hence the lock on both sides: in Home Assistant (`input_datetime.pellet_stove_last_toggle`, 20 min) and in the component (until a screen confirms the new state, see [esphome.md](esphome.md)). The 90 s on/off debounce of the component remains as well.

**Thermostat Off never switches the stove off.** In *Off* mode the thermostat only stops regulating; switch the stove off with the On-off button or the remote. Besides being predictable, this fixes a nasty side effect: at start-up, a generic thermostat in *Off* mode turns its heater off — Home Assistant restarting would switch off a stove lit by hand. `turn_off` waits 1 s (the thermostat writes its new mode after calling the switch) and does nothing if `climate.pellet_stove` is `off`.

The dashboard On-off button presses the ESPHome button directly: it is **not** subject to these guards (only to the component's debounce and lock), so you can always force a stop after a failed ignition.

Install: in `configuration.yaml`
```yaml
homeassistant:
  packages: !include_dir_named packages
```
copy the file to `/config/packages/`, *Developer tools → Check configuration*, then restart Home Assistant the first time (the package creates `input_datetime` helpers, automations and template entities; later changes only need the matching reloads). Replace `sensor.your_room_temperature`, `sensor.your_outdoor_temperature` and `notify.notify` with your own entities.

### b) The generic thermostat (UI)

*Settings → Devices & services → Helpers → Create helper → Generic thermostat*:

| Field | Value used | Why |
| --- | --- | --- |
| Heater | `switch.pellet_stove_heater` | the protected switch |
| Temperature sensor | your room sensor | |
| Cold / hot tolerance | **0.5 / 0.5 °C** | set-point 21 → ignites **below** 20.5 and stops **above** 21.5 (strict comparisons: 20.4 and 21.6 °C with a 0.1 °C sensor); the stove's inertia does the rest |
| Minimum cycle duration | **none** | the 1 h guard is in the switch (restart-proof), see above |
| Keep-alive | **5 min** | re-sends the current order regularly, so a request refused by a guard is retried |
| Min / max temperature | 15 / 25 | |
| Presets (2nd page) | Comfort 21, Eco 19, Away 16 | |

Notes (Home Assistant 2026.9):

* The helper starts with a set-point equal to the **minimum** (15 °C here). If you switch to *Heat* before changing it, the thermostat will ask to switch a running stove off. Set the set-point first.
* The +/− of the card change the **current** set-point, **not** the preset values; presets are edited in the helper's options (*Helpers → the thermostat → ⚙ → Generic thermostat options → Next*).
* **Off** mode = the thermostat leaves the stove alone (manual control with the button or the remote) and never switches it off. In **Heat** mode, a manual on/off is overridden by the next thermostat order once the 1 h guard allows it.
* There is no schedule in the generic thermostat: use a schedule helper + automation that changes the preset.
* Tip: to "boost", raise the set-point rather than forcing the stove on.

## 3. Dashboard

* `dashboard/home-cards.yaml` — compact home cards (Mushroom): thermostat, one **on/off toggle** with confirmation (subtitle = current stove screen text), power and fan − / + on one line.
* `dashboard/stove-page.yaml` — a full sub-view: reconstructed **LCD of the remote** (button-card), thermostat with presets, state history (24 h / 7 days), phase logbook, temperatures, diagnostics.

The on/off button is deliberately **always available**: after a failed ignition you must be able to force a stop, then restart — exactly like with the remote. Keep the confirmation dialog.

## 4. Alerts

`automation-error-alert.yaml`: notification when the state becomes `Défaut`. The exact wording of the stove's error screens has not been observed yet; the component maps any screen containing `DEFAUT`, `ALARM` or `ERR` to `Défaut`.

The package adds **"Pellet stove - operating alerts"** (set your notify service in place of `notify.notify`). Thresholds come from real cycles on the reference stove (a command shows on the screen within 1–4 s, ignition 18–51 min, cleaning 39–43 min, screen re-read at least every 30 min):

| Alert | Condition |
| --- | --- |
| on/off not confirmed | `bascule_en_attente` on for 10 min |
| stove unreachable | `joignable` off for 15 min (radio, stove unplugged) |
| module offline | state `unavailable` for 15 min (power, Wi-Fi) |
| frozen screen | screen age above 45 min |
| long ignition | `Allumage` for more than 1 h (failed ignition, no pellets) |
| unknown screen | `Autre` for 15 min (possibly an alarm) |
| room sensor silent | sensor unavailable for 30 min — only while the thermostat heats |
| too hot / too cold | room above 23.5 °C for 15 min / below 17 °C for 30 min — only while the thermostat heats |

## 5. Long-term data (for smarter regulation)

Home Assistant keeps detailed history 10 days by default, but keeps **statistics forever** for sensors with `state_class: measurement`. Many integrations' temperature sensors have no `state_class` (no long-term statistics), so the package re-exposes what is needed as measurement sensors: indoor and outdoor temperature, running / igniting / residual-heat (0/1), effective power and fan, set-point, thermostat active, people at home.

The idea (not implemented yet): learn how fast each power level heats the house depending on the outdoor temperature, then let the regulation pick the power and fan automatically — e.g. low power and fan 1 when someone is home (high power/fan are noisy), high power and fan to homogenise the house when nobody is home or to catch up before coming back. A few weeks of varied data (different power levels!) are needed before modelling.
