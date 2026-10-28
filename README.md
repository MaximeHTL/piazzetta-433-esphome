# Piazzetta 433 MHz → ESPHome / Home Assistant

**Control a Piazzetta pellet stove from Home Assistant by talking to it over the air, exactly like its original radio remote does.**
No wiring into the stove, no modification of the stove: an ESP32 and a 433 MHz radio module take the place of the remote.

🇫🇷 **Version française : [README.fr.md](README.fr.md)**

<p align="center">
  <img src="docs/images/remote-f0203-1480v3.jpg" alt="Piazzetta F0203-1480V3 remote" height="360">
  &nbsp;&nbsp;
  <img src="docs/images/wiring.svg" alt="Wiring ESP32 DevKit to Ra-02" height="360">
</p>

---

> [!WARNING]
> **Experimental project — use at your own risk.**
> This drives a **combustion heating appliance**. It was reverse-engineered on **one** stove and **one** remote, by a hobbyist, without any documentation from the manufacturer. It is not affiliated with, endorsed by or supported by Piazzetta / Gruppo Piazzetta.
> Keep the original remote, follow the stove's manual and safety instructions, never leave the stove without the protections it is designed with, and do not rely on this project for safety-critical functions. You alone are responsible for what you send to your stove.

> [!NOTE]
> **Built with Claude Code — full transparency.**
> I am a DIY tinkerer with some electronics skills, not a radio or software engineer. This whole project — the protocol reverse-engineering, the firmwares, the ESPHome component, the Home Assistant configuration **and every piece of documentation in this repository** — was produced together with [Claude Code](https://claude.com/claude-code), Anthropic's AI coding agent. I did the wiring, the soldering, the button presses, the listening to the stove and the testing; Claude Code did the analysis, the code and the writing, which are beyond my technical level.
> Getting there, starting from "can I sniff my stove's remote?", was a big step for me. I hope this repository can be a **solid base for someone else**, ideally with their own AI agent, to connect their Piazzetta stove too. These stoves are extremely robust — I never had a single problem with mine — and the only thing I missed was being able to control it remotely. Now I can.
> See **[docs/ai-assistant-guide.md](docs/ai-assistant-guide.md)** for how to reproduce this project with an AI agent.

---

## What it does

* **Reads the stove's state continuously**: the stove sends the text of the remote's screen over the radio (`NIVEAU P1`, `ALLUMAGE PHASE I`, `NETTOYAGE BRASERO`, `OFF`…), plus the room temperature, the power level and the fan speed.
* **Controls the stove**: power level 1–4, fan speed 1–4, **on/off** (simulated long press on the ⏻ key) — each command is **verified** from the stove's answer.
* **Home Assistant integration through ESPHome**: entities appear automatically (state, phase, temperature, power, fan, on/off, radio diagnostics).
* **Thermostat**: a Home Assistant *generic thermostat* that switches the stove on/off around a set-point with hysteresis, using any room temperature sensor. A protected heater switch enforces a **1 h minimum on/off time based on the stove's real phases** (restart-proof) and never sends a second on/off before the stove screen has confirmed the first one.
* **Robust reception and diagnostics**: parity + checksum on every frame, automatic repair of a single stray bit, screen-age and rejected/corrected-frame counters, and Home Assistant alerts (unconfirmed command, frozen screen, long ignition, unreachable stove…).
* **A test bench** (ESP32 firmware + a local web page) to **capture, decode, compare and replay** frames — this is what made the reverse-engineering possible, and what you will use to check your own installation.

## How it works (in short)

```
 Original remote (F0203-1480V3)  ◄──── 433.92 MHz OOK, 2400 baud ────►  Piazzetta stove
                                                   ▲
                                                   │ same radio dialogue
                                                   ▼
 Home Assistant ◄── Wi-Fi / ESPHome API ──► ESP32 + Ra-02 (SX1278) module
```

The remote is a **dumb terminal**: the stove owns the menus and sends the text to display. The remote polls the stove every few seconds when awake and sends key codes. The module speaks the same dialogue.
Full details: **[docs/protocol.md](docs/protocol.md)**.

## Hardware (≈ 15 €)

| Part | Notes |
| --- | --- |
| ESP32 DevKit (ESP32-WROOM-32, 30 or 38 pins) | the classic ESP32; tested board |
| Ra-02 module (AI-Thinker, **SX1278**, 433 MHz) | must support **OOK** (the SX1262 of Heltec V3 boards does **not**) |
| 433 MHz antenna | a real 433 MHz antenna (spring or ¼-wave wire, 17.3 cm) — **never transmit without antenna** |
| USB power supply | the module stays powered permanently |

Wiring diagram, references and tips: **[docs/hardware.md](docs/hardware.md)**.

## Compatibility

| Tested | |
| --- | --- |
| Remote | Piazzetta **F0203-1480V3** (bidirectional, LCD screen, keys: fan, flame, +, −, thermometer, clock, ★, ⏻) |
| Stove | a Piazzetta pellet stove using this remote (power P1–P4, fan 1–4) |
| ESPHome | 2026.9, 2026.10 (Arduino framework, RadioLib 7.8.1) |
| Home Assistant | 2026.9 |

**Unknown**: whether the first byte of the remote frames (`0xC5` here, the `remote_id` option) is the same for every remote, or specific to each remote/stove pairing. The component logs any remote it hears with a different identifier. **Please report your findings** (see Contributing).
Other Piazzetta remotes / stoves may use the same system — untested.

## Getting started

1. **Build the hardware** — [docs/hardware.md](docs/hardware.md).
2. **Check your installation with the test bench** — flash `test-bench/firmware/test_bench`, open the web page, capture your remote's frames, find your `remote_id` — [docs/capture-guide.md](docs/capture-guide.md).
3. **Install the ESPHome component** — [docs/esphome.md](docs/esphome.md).
4. **Set up Home Assistant** (thermostat, dashboard, alerts, data collection) — [docs/home-assistant.md](docs/home-assistant.md).
5. Read **[docs/lessons-learned.md](docs/lessons-learned.md)** — every trap we fell into, so you don't have to.

## Repository layout

```
├── README.md / README.fr.md
├── docs/
│   ├── protocol.md            # the radio protocol, byte by byte
│   ├── hardware.md            # parts, wiring, antenna
│   ├── capture-guide.md       # check your own stove/remote with the test bench
│   ├── esphome.md             # the ESPHome component: options, entities, behaviour
│   ├── home-assistant.md      # thermostat, dashboard, alert, long-term data
│   ├── lessons-learned.md     # pitfalls and how they were solved
│   └── ai-assistant-guide.md  # how to reproduce this with an AI coding agent
├── esphome/
│   ├── piazzetta-stove.yaml   # example device configuration
│   └── components/piazzetta/  # the external component (C++ / Python)
├── test-bench/
│   ├── firmware/test_bench/   # Arduino sketch: capture + transmit over USB
│   └── web/                   # test-bench.html (Web Serial page) + serve.py (optional local server)
├── tools/                     # Python decoders for captured logs
├── home-assistant/            # package (switch + data sensors), dashboard cards, alert automation
└── captures/                  # example decoded frames
```

## Contributing

Very welcome, especially:

* **Your `remote_id`** (first byte of your remote's frames) and your remote/stove model — this tells whether the protocol is universal.
* **New keys and screens**: thermometer key, clock/programming key, ★ key, error messages (`DEFAUT…`), other languages.
* **Other Piazzetta models** that work (or don't).

Open an issue with your captures (the test bench exports JSON; frames contain no personal data apart from timestamps).

## Credits

* [RadioLib](https://github.com/jgromes/RadioLib) — radio driver (MIT).
* [rtl_433_ESP](https://github.com/NorthernMan54/rtl_433_ESP) — used for the very first captures (not included here).
* [ESPHome](https://esphome.io/) and [Home Assistant](https://www.home-assistant.io/).
* [Claude Code](https://claude.com/claude-code) — reverse-engineering, code and documentation.

## License

[MIT](LICENSE). "Piazzetta" is a trademark of its owner, used here only to describe compatibility.
