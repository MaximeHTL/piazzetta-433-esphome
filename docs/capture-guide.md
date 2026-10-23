# Check your own stove and remote with the test bench

Before installing the ESPHome module, **check that your installation speaks the same protocol**, and find your **`remote_id`**. The test bench was the tool that made the whole reverse-engineering possible; it takes about 15 minutes.

The test bench is made of:

* `test-bench/firmware/test_bench/test_bench.ino` — an Arduino sketch for the ESP32 + Ra-02: it captures OOK frames and sends them to the PC over USB, and transmits frames on request;
* `test-bench/web/test-bench.html` — a web page (Chrome or Edge, Web Serial) that decodes, groups, compares and replays frames. Its interface is in French; the main words are translated below.

## 1. Flash the test bench

1. Install the **Arduino IDE** and the **ESP32 core** (Espressif, tested with 3.3.x).
2. Library manager: install **RadioLib** (tested 7.8.1). Nothing else is needed.
3. Open `test_bench.ino`, board **"ESP32 Dev Module"**, select the port, upload. Pins are at the top of the sketch.
4. Optional check: Serial Monitor at **115200 baud** → a line `I PRET banc poele 433 …` ("ready"). Close the Serial Monitor afterwards (only one program can open the port).

## 2. Open the page

* Double-click `test-bench.html` (Chrome/Edge), **or** run `python serve.py` and open `http://localhost:8765/test-bench.html`. The local server also appends everything to a JSONL log file (handy if an AI agent follows your tests live, see [ai-assistant-guide.md](ai-assistant-guide.md)).
* Click **"Connecter l'ESP"** (connect) and choose the ESP32 serial port.

## 3. Capture your remote

Main UI words: *Fil* = timeline · *Trames distinctes* = distinct frames · *Repère* = marker · *Émettre* = transmit · *Réglages* = settings · *Télécommande* = remote · *Poêle* = stove · *Interrogation* = poll.

1. Wake the remote (press any key): you should see **remote polls** every ≈ 4 s and **stove answers** right after. Untick *"Masquer les interrogations"* to see the polls.
2. Look at the **"Octets / texte"** column of a remote frame, e.g. `C5 3D 00 00 02 ✓`:
   * the ✓ means the checksum is correct — the protocol matches;
   * **the first byte is your `remote_id`**. If it is not `C5`, set it in the page settings (*"Identifiant télécommande"*) and in the ESPHome YAML.
3. Stove screen frames show the decoded text, e.g. `« NIVEAU P1 » · 12:34 20° · ventilateur 1 · P1`, and the **"Écran du poêle"** card reproduces the LCD.
4. Before each key press, type what you are about to do and click **"Poser un repère"** (set marker). Press: fan key, +, −, power key (flame), … The *Trames distinctes* tab groups identical frames; label them.

If you see **no frame at all**: check the wiring (SPI), the antenna, the distance; your remote may use another frequency (try 433.42 / 434.42 MHz in *Réglages* → *Fréquence*) or FSK instead of OOK (then this project will not work as is).

## 4. Transmit (carefully)

* **"Interroger le poêle"** (poll): harmless; the stove answers with its clock (`B9`) or its screen (`B5`).
* **"Ventilateur + / −"**, **"Puissance + / −"**: open the menu, check it opened, step, check the new value; a marker shows `✅ Ventilateur + : vitesse 1 → 2`.
* **"Appui long ⏻ (marche/arrêt)"**: **switches the stove on or off** (confirmation dialog). Only use it when you mean it, and respect the stove's own constraints (it will not restart until it has fully cooled down, etc.).
* After each transmission the page checks that the stove answered within 1.5 s (✅ / ❌ marker).

If the stove never answers your transmissions but answers the real remote: see [lessons-learned.md](lessons-learned.md) (transmit bit-rate trap).

## 5. Export and share

**"Exporter"** saves the session (frames, markers, labels, ESP log) to JSON. Frames contain no personal data apart from timestamps. Sharing your `remote_id`, remote/stove model and any new keys or screens (issue on GitHub) helps everyone.

## Tools

* `tools/decode_bytes.py` — decodes frames (bits → bytes, 8E1 inverted, checksum) from a JSONL log: `python tools/decode_bytes.py log.jsonl 12:00 12:30`.
* `tools/decode_nrz.py` — older raw-bit decoder for the first rtl_433_ESP logs.
