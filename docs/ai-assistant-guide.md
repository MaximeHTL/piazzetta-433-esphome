# Reproducing this project with an AI coding agent

This project was built by a hobbyist **together with Claude Code** (Anthropic's AI coding agent): the human did the wiring, the button presses and the listening; the agent did the analysis, the code and the documentation. This page explains how to do the same at home with your own agent — Claude Code or any similar tool that can read files, run commands and talk to a serial port.

## What the agent needs

* This repository cloned on your PC (the agent reads the docs and the code).
* Access to the **ESP32's serial port** (USB) — to flash, read logs, or let you use the test-bench page.
* Optionally: Python (decoding scripts), the Arduino IDE / arduino-cli, ESPHome (dashboard or CLI), and network access to Home Assistant (API token, or SSH to the host) if you want the agent to install the HA part. Keep tokens and passwords out of the repository and out of the chat when possible: let the agent read them from a local file.

## The workflow that worked

1. **Start with the docs.** Ask the agent to read `README.md`, `docs/protocol.md`, `docs/lessons-learned.md` before anything else. It will then know the protocol, the traps and the tools.
2. **Hardware first, then the test bench.** Wire per `docs/hardware.md`; flash `test-bench/firmware/test_bench` (you can flash it from the Arduino IDE yourself, the agent can also compile with `arduino-cli` to check).
3. **Set up a live log** so the agent sees what you do in real time: run `python test-bench/web/serve.py`, open `http://localhost:8765/test-bench.html`, connect the ESP. Every frame, marker and transmission is appended to `Downloads/banc-poele-live.jsonl`. Tell the agent to **read** this file (copy then parse; never keep it open with `tail -f` on Windows).
4. **Capture with markers.** Before each key press, type what you do and click "Poser un repère". Give the agent the exact times ("12:22:17 fan key, 12:22:22 + from 1 to 2"). Decode with `tools/decode_bytes.py`.
5. **Compare with `docs/protocol.md`.** Same structure? Same `remote_id`? Same keys? Note the differences.
6. **Transmit from the page** (poll, then fan +/−) — **you** click, after the agent has explained what will be sent.
7. **Install the ESPHome component** (`docs/esphome.md`) with your `remote_id`, then the Home Assistant part (`docs/home-assistant.md`). Ask the agent to back up the Home Assistant configuration/dashboard before each change and to verify after.

## If your remote or stove behaves differently

Give the agent this checklist:

* No frames at all → frequency (try ±0.5 MHz), antenna, wiring, RSSI threshold; maybe FSK instead of OOK (then the SX1278 must be configured for FSK).
* Frames but no checksum ✓ → check the bit time (416.7 µs = 2400 baud?) by histogramming pulse widths; check the UART hypothesis (start=1, LSB first, even parity, stop=0); look for the sync gap.
* Checksum ✓ but a different first byte → set `remote_id`.
* Stove does not react to replayed frames → see "Transmitting" in `docs/lessons-learned.md` (transmit bit rate, menu state, toggle keys, long press).

## Rules worth giving your agent

Paste this into your agent's project instructions (e.g. `CLAUDE.md`):

```markdown
## Pellet stove project — rules
- This drives a real combustion heating appliance. NEVER send a command to the stove
  (transmission from the test bench, ESPHome button/number, Home Assistant service) unless
  I explicitly ask for it in this conversation. By default, only read: logs, states, history.
- Explain what a transmission will do before I trigger it; I confirm by looking at / listening to the stove.
- Before modifying Home Assistant (YAML, dashboard, automations): back up, change one thing, verify.
- Never write secrets (Wi-Fi, API keys, tokens) into the repository or the chat; read them from local files.
- Keep a dated log of findings (what was pressed, what was received, what changed).
- Protocol reference: docs/protocol.md. Pitfalls: docs/lessons-learned.md.
```

## Prompts that helped

* "Here is what I see on the serial monitor: … What does it mean, what should I check?"
* "I pressed the fan key at 12:22:17 and + at 12:22:22. Decode the frames around those times from the live log."
* "The stove answers the real remote but not the ESP. List the possible causes, most likely first, and how to test each without risk."
* "Make the button on the dashboard safer: one toggle, always available, with a confirmation dialog."
* "Document everything we learned today in the project notes, without personal data."

Good luck — and please share what you find (remote_id, models, new keys) so the next person goes even faster.
