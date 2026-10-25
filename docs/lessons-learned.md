# Lessons learned (pitfalls and their fixes)

In the order they were met. Each one cost time; together they are probably the most useful page of this repository.

## Choosing the radio

* **Heltec V3 (SX1262) cannot receive OOK.** It only does LoRa and (G)FSK. An **SX1278 (Ra-02)** does OOK in both directions.
* Start with **rtl_433_ESP**'s `OOK_Receiver` example to see *something*: it showed signals, but no rtl_433 decoder knew the protocol ("signal parsing failed"), which is expected.

## Receiving

* **Ambient traffic is not your remote.** The first strong frame we saw coincided with a LoRa mesh message (a MeshCore node on 433 MHz) and was wrongly attributed to it — it was actually the stove. And a test where the wrong button was pressed (the ESP32 BOOT button!) produced only ambient signals. Always use **markers** ("I press X now") and compare with/without the remote.
* **rtl_433_ESP drops short frames.** Its defaults ignore signals shorter than 40 ms (`MINIMUM_SIGNAL_LENGTH`) and with fewer than 16 pulses (`PD_MIN_PULSES`, a plain `#define`, not overridable). The remote frames (≈ 35 ms, 8–27 pulses) were silently ignored.
* **Arduino IDE does not rebuild libraries when `build_opt.h` changes.** A changed compile flag had no effect until the sketch's build cache (`%LOCALAPPDATA%\arduino\sketches\<hash>`) was deleted.
* **The OOK slicer chatters in noise.** Start a frame only if the RSSI is above a threshold, and end it after a period without signal — otherwise you get endless garbage frames.
* **The stove pauses inside its long screen frames.** With a 15 ms end-of-frame gap, screens were split in two ("unknown" second half). **40 ms** keeps them whole.
* **"Stove unreachable" after putting the module in a case — the USB cable.** Wi-Fi was fine and the stove still answered every poll, but at −75 dBm instead of −55: frames arrived truncated, failed the checksum and were dropped. Moving the antenna out of the case gained only 1–2 dB; going back to the previous USB cable restored −53 dBm. At 433 MHz the supply cable is part of the antenna system and can carry noise. Read the RSSI in the DEBUG logs before suspecting the firmware.

## Decoding

* Thinking in raw bits (NRZ, 2400 baud) gave repeatable frames but no meaning. **Reading them as an inverted UART (8E1, LSB first)** turned them into bytes with a checksum — and the stove's frames into **readable text**: the stove sends the remote's screen.
* Commands turned out to be **relative** (+/−), and the **menus live in the stove**.

## Transmitting

* **The stove answered the real remote but ignored the ESP32.** Cause: in continuous (direct) transmit mode, the SX127x **samples the DIO2 data pin at the configured bit rate**. The bit rate was still 1.2 kbit/s (receive setting) and the OOK data shaping was on, so 2400-baud frames were mangled. Fix: **32.768 kbit/s and no shaping while transmitting**, back to the receive settings afterwards.
* **Receiver desensitised after transmitting.** After some transmissions the receiver lost ≈ 35 dB (the real remote at −43 dBm was read at −77 dBm) and captured noise continuously. Fix: **fully re-initialise the radio after every transmission**.
* **A replayed "+" did nothing although the stove answered.** The stove was not in its fan menu: menus are opened by the menu key and close after ≈ 8 s. Fix: menu key first, then "+".
* **The menu key is a toggle.** Pressed while the stove thinks the menu is open, it closes it (the real remote does the same: "the screen did not change"). Fix: read the answer and press again if needed.
* **A single `61` ("⏻ held") frame does not switch the stove.** It needs ≈ 3 s of repeated `61` frames, as the real long press.

## Reading the state

* **A poll does not always return the screen.** The stove sends the full screen only when it changed (or after a key); otherwise only the clock. A hypothesis linking this to the remote's state byte was tested and **refuted**. Fix for start-up: press the fan key once, without +/−.
* **One missed screen = a frozen state.** Since the stove does not repeat an unchanged screen, a single lost frame left Home Assistant showing "CONTROLE" while the stove was cleaning. Fix: repeat the fan-key resynchronisation when no main screen has been seen for a while.
* **The 8-bit checksum is weak.** Two corrupted screens passed it on the same day ("NIVEA? P1" with 9 °C, an unreadable line with 186 °C). Fix: also **reject any frame with a wrong parity or stop bit** in its useful bytes. Replayed on 895 archived frames with a good checksum, this check rejected none of them. And do not let an unrecognised state drive anything (see Home Assistant below).
* The remote updates its display only **when it polls**: after a command it can take up to ≈ 44 s to show the new value although the stove applied it immediately.

## Home Assistant

* **A toggle is not a switch.** The generic thermostat needs a switch; build it with state guards (see [home-assistant.md](home-assistant.md)).
* **State guards are not enough: debounce the toggle.** The state lags a few seconds behind a press, so two clicks on "off" plus the thermostat going to *Off* sent three toggles in 47 s: shutdown, then a new ignition. The component now ignores on/off requests for 90 s after a press.
* **Generic thermostat created at the minimum set-point** → switching to *Heat* would have stopped a running stove. Set the set-point first.
* **The minimum cycle restarts at every change of the heater switch's state**, including going through "unavailable" when the module briefly disconnects, and when template entities are reloaded. Make the template switch keep its last known state during outages (no `availability` template), and avoid reloading templates while testing the thermostat. The same applies to an unrecognised state (`Autre`): keep the last state.
* `generic_thermostat.reload` does not exist until the integration has been loaded once → create it as a UI helper (no restart needed).
* Temperature sensors of some integrations have **no `state_class`** → no long-term statistics. Re-expose them as measurement sensors if you want to analyse months of data.
* button-card styles must be lists of `{property: value}` pairs; strings like `"color: red"` are silently ignored.

## Working method

* A **live log** that the AI agent can read while you press buttons (web page → local server → JSONL file) made the analysis 10× faster than exporting and pasting logs. On Windows, do not keep the log file open with `tail -f`: the browser cannot replace it.
* Let the **human** press the stove's buttons and trigger real commands; the agent analyses, proposes and prepares. Heating appliances are not a place for unattended experiments.
