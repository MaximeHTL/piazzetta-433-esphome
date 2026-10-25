# Home Assistant setup

Files in `home-assistant/`. Entity IDs assume the example ESPHome config (friendly name "Pellet stove"); adapt them if yours differ (Settings → Entities).

## 1. Add the device

*Settings → Devices & services → Add integration → ESPHome* → the module's IP address (reserve it in your router) and the API encryption key. All entities of [esphome.md](esphome.md) appear under the device.

## 2. Thermostat

Goal: a heating entity like a heat pump's — set-point, hysteresis, presets — that switches the stove on/off from **any room temperature sensor** (the stove's own sensor sits next to the stove; a sensor in the living area is better).

### a) A protected heater switch

The thermostat needs a `switch`. The ESPHome on/off button is a **toggle**, so a naive switch would switch **off** a stove that is already on when asked to "turn on". The template switch of `package_piazzetta_stove.yaml` only presses the button when the stove state justifies it:

* state = on if the stove state is `Marche` (running) or `Allumage` (igniting);
* **turn on** presses ⏻ only if the state is `Arrêt` (off);
* **turn off** presses ⏻ only if the state is `Marche` or `Allumage`;
* the switch **keeps its last state** when the stove state is `unknown`, `unavailable` or `Autre` (corrupted frame, clock or language menu), so that the thermostat's minimum cycle is not restarted.

These guards rely on the stove state, which only changes a few seconds after a press: the component's 90 s **on/off debounce** covers that gap (see [esphome.md](esphome.md)).

Install: in `configuration.yaml`
```yaml
homeassistant:
  packages: !include_dir_named packages
```
copy the file to `/config/packages/`, *Developer tools → Check configuration*, then *Reload template entities* (YAML tab).

### b) The generic thermostat (UI)

*Settings → Devices & services → Helpers → Create helper → Generic thermostat*:

| Field | Value used | Why |
| --- | --- | --- |
| Heater | `switch.pellet_stove_heater` | the protected switch |
| Temperature sensor | your room sensor | |
| Cold / hot tolerance | **1.0 / 1.0 °C** | set-point 21 → ignites at 20, stops at 22 |
| Minimum cycle duration | **1 h** | ignition takes ≈ 17 min and shutdown ≈ 42 min: never short-cycle a pellet stove |
| Min / max temperature | 15 / 25 | |
| Presets (2nd page) | Comfort 21, Eco 19, Away 16 | |

Notes (Home Assistant 2026.9):

* The helper starts with a set-point equal to the **minimum** (15 °C here). If you switch to *Heat* before changing it, the thermostat will immediately switch a running stove off. Set the set-point first.
* The +/− of the card change the **current** set-point, **not** the preset values; presets are edited in the helper's options (*Helpers → the thermostat → ⚙ → Generic thermostat options → Next*).
* **Off** mode = the thermostat leaves the stove alone (manual control with the button or the remote). In **Heat** mode, a manual on/off is overridden at the next temperature change (after the minimum cycle).
* There is no schedule in the generic thermostat: use a schedule helper + automation that changes the preset.
* Tip: to "boost", raise the set-point rather than forcing the stove on.

## 3. Dashboard

* `dashboard/home-cards.yaml` — compact home cards (Mushroom): thermostat, one **on/off toggle** with confirmation (subtitle = current stove screen text), power and fan − / + on one line.
* `dashboard/stove-page.yaml` — a full sub-view: reconstructed **LCD of the remote** (button-card), thermostat with presets, state history (24 h / 7 days), phase logbook, temperatures, diagnostics.

The on/off button is deliberately **always available**: after a failed ignition you must be able to force a stop, then restart — exactly like with the remote. Keep the confirmation dialog.

## 4. Error alert

`automation-error-alert.yaml`: notification when the state becomes `Défaut`. The exact wording of the stove's error screens has not been observed yet; the component maps any screen containing `DEFAUT`, `ALARM` or `ERR` to `Défaut`.

## 5. Long-term data (for smarter regulation)

Home Assistant keeps detailed history 10 days by default, but keeps **statistics forever** for sensors with `state_class: measurement`. Many integrations' temperature sensors have no `state_class` (no long-term statistics), so the package re-exposes what is needed as measurement sensors: indoor and outdoor temperature, running / igniting / residual-heat (0/1), effective power and fan, set-point, thermostat active, people at home.

The idea (not implemented yet): learn how fast each power level heats the house depending on the outdoor temperature, then let the regulation pick the power and fan automatically — e.g. low power and fan 1 when someone is home (high power/fan are noisy), high power and fan to homogenise the house when nobody is home or to catch up before coming back. A few weeks of varied data (different power levels!) are needed before modelling.
