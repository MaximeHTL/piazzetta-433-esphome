# The radio protocol

Everything here was **observed** on one installation (Piazzetta pellet stove + remote **F0203-1480V3**) by capturing and replaying frames. Nothing comes from manufacturer documentation. Where something is uncertain, it is said so.

## 1. Physical layer

| | |
| --- | --- |
| Frequency | **433.92 MHz** |
| Modulation | **OOK** (carrier on = 1, carrier off = 0) |
| Bit rate | **2400 baud** → one bit = **416.7 µs** |
| Frame start | a **preamble** `1010101`, then a **sync gap** of ≈ 8–9 ms of silence (≈ 19–22 bit times), then the bytes |
| Typical frame durations | remote: ≈ 35 ms · stove clock frame: ≈ 40–75 ms · stove screen frame: ≈ 170–210 ms |

The stove sometimes pauses for 15–40 ms **in the middle** of a long screen frame. A receiver must not end the frame on a gap shorter than ≈ 40 ms (see [lessons-learned.md](lessons-learned.md)).

## 2. Bytes: an inverted serial line (8E1)

After the sync gap, bits are sent like a UART **with inverted polarity**:

* idle = `0`
* **start bit = `1`**
* **8 data bits, least significant bit first**
* **1 even parity bit** (the number of `1` among the 8 data bits + parity is even)
* **stop bit = `0`**

That makes 11 bits per byte, sent back to back. Trailing zeros of the last byte are indistinguishable from silence (it does not matter).

**Checksum**: the **last byte of every frame = 8-bit sum of all the previous bytes** (`sum & 0xFF`). Verified on all remote frames and on all stove frames.

### Worked example: the "+" key

Bytes `C5 BD 00 88` → checksum `C5+BD+00+88 = 0x20A` → `0A`. Frame: `C5 BD 00 88 0A`.

| Byte | Data bits LSB first | Parity | 11 bits sent (start, data, parity, stop) |
| --- | --- | --- | --- |
| `C5` = 1100 0101 | 1010 0011 | 0 (four 1s) | `1 10100011 0 0` |
| `BD` = 1011 1101 | 1011 1101 | 0 (six 1s) | `1 10111101 0 0` |
| `00` | 0000 0000 | 0 | `1 00000000 0 0` |
| `88` = 1000 1000 | 0001 0001 | 0 | `1 00010001 0 0` |
| `0A` = 0000 1010 | 0101 0000 | 0 | `1 01010000 0 0` |

Concatenated (trailing zeros dropped): `11010001100110111101001000000000010001000100 10101`. This is **bit-for-bit identical** to the frame captured from the real remote.

On air, each bit is 416.7 µs of carrier (1) or silence (0); a transmitter turns the bit string into run lengths (e.g. `11` → 833 µs of carrier).

## 3. Who talks when

* **The stove never transmits on its own.** It only **answers** a remote frame, ≈ 0.2–0.3 s later.
* **Awake remote** (screen lit, after a key press): it polls the stove **every ≈ 4 s**.
* **Sleeping remote**: it wakes up by itself **every ≈ 44 s** and asks for the **clock** only, to keep its display up to date.
* **The remote is a dumb terminal.** The stove manages the menus and sends the text to display; the remote only shows it and sends key codes. Consequence: after a command, the remote only displays the new value **at its next poll** (up to ≈ 44 s if it was asleep) — the stove itself has already applied it.

## 4. Remote → stove: `ID STATE 00 KEY SUM`

| Byte | Value | Meaning |
| --- | --- | --- |
| 1 | `C5` | **remote / installation identifier** (constant here). ⚠️ May differ on other remotes — option `remote_id` of the component |
| 2 | `3D` `3E` `BD` `BE` | **state byte**. Bit 7 (`0x80`) set = a menu is open on the remote's screen. Low bits `…D` / `…E` alternate in ways not fully understood (they seem to follow the main screen being shown). The stove accepted commands with any of these values. |
| 3 | `00` | always `00` |
| 4 | key | see table below |
| 5 | sum | checksum |

### Keys

| Key byte | Key | Notes |
| --- | --- | --- |
| `00` | **poll** | "give me your state" |
| `84` | **fan** key (✻) | opens / **closes** the fan menu on the stove (toggle) |
| `98` | **power** key (flame) | opens / **closes** the power menu (toggle) |
| `88` | **+** | **relative**: one step up in the open menu |
| `8C` | **−** | **relative**: one step down in the open menu |
| `81` | **⏻ pressed** | alone (short press): no effect |
| `61` | **⏻ held** | repeated while the key is held; a long press switches the stove on/off |
| `E4`, `64` | ? | seen once each, not identified (possibly ★ or a key bounce) |

Not decoded yet: thermometer key, clock key, ★ key (contributions welcome).

**Commands are relative**: `+` from 1→2 and from 2→3 is the same frame `C5 BD 00 88 0A`.

### Changing power or fan speed

The menus live **in the stove**:

1. Send the **menu key** (`C5 3D 00 84 86` fan, or `C5 3D 00 98 9A` power). The stove answers with its **adjustment screen** (`REGLER VENT-1` or `REGLER PUISSANCE`) showing the current value.
2. Within a few seconds, send `+` (`C5 BD 00 88 0A`) or `−` (`C5 BD 00 8C 0E`). The stove answers with the adjustment screen showing the **new value**.
3. The menu closes by itself after ≈ 8 s.

⚠️ **The menu key is a toggle**: if the stove thinks the menu is still open, the key **closes** it and the stove answers with its main screen; a `+` sent then is ignored. Robust method (used by the component): send the menu key, **read the answer**, press again if the screen is not the adjustment screen, then send `+`/`−` and **check the value** in the answer.

### On / off: long press on ⏻

Captured from the real remote and replayed successfully (both switching off and switching on):

```
C5 3E 00 81 84        ⏻ pressed
C5 BE 00 61 E4        ⏻ held  (repeated every ≈ 0.35 s)
C5 BE 00 61 E4
C5 BE 00 61 E4
C5 BE 00 61 E4
C5 3E 00 61 64
C5 3E 00 61 64
C5 3E 00 61 64
C5 3E 00 61 64
C5 BE 00 61 E4
C5 BE 00 61 E4
C5 3E 00 61 64        (11 "held" frames ≈ 4 s in total)
```

* **One single `61` frame is not enough**: the stove answers but does not switch. It reacts after **≈ 3 s of "held" frames**.
* It is a **toggle**: on → starts the shutdown, off → starts the ignition. During the post-shutdown cleaning phase, a new long press **restarts an ignition** (behaviour of the original remote).
* Extra `61` frames after the switch do not toggle again.

## 5. Stove → remote

### Clock frame `B9` (6 bytes)

`B9 h1 h2 m1 m2 SUM` — digits as raw values 0–9, bit 7 = display attribute (colon/blinking) to mask out.
Example: `B9 01 82 85 02 C3` → 1, 2 (`82`&`7F`), 5, 2 → **12:52**.

### Screen frame `B5` (32 bytes)

```
B5 | bottom-left (4) | bottom-right (5) | line 1 (9 chars) | line 2 (9 chars) | icons (3) | SUM
```

This maps 1:1 to the remote's LCD: a row of icons at the top, two big text lines in the middle, a bottom line (clock left, temperature right).

Character encoding:

* `0x00`–`0x09` → **digits 0–9** (raw values, not ASCII); bit 7 may be set (attribute), mask with `0x7F`
* `0x5E` → **degree sign** (°)
* other printable bytes → ASCII (`0x20` space, `0x41` A…)

Main screen example (12:45, 21 °C, power 1):

```
B5  01 82 84 05  20 20 02 01 5E  20 4E 49 56 45 41 55 20 20  20 20 20 50 01 20 20 20 20  08 41 08  SUM
    1  2  4  5   ␣  ␣  2  1  °   ␣  N  I  V  E  A  U  ␣  ␣   ␣  ␣  ␣  P  1  ␣  ␣  ␣  ␣   icons
    → 12:45       →   21°        → " NIVEAU  "                → "   P1    "
```

Adjustment screens put the value in the bottom area instead of the clock:

| Screen | Bytes 1–5 | Lines |
| --- | --- | --- |
| fan menu | `20 20 20 00 <speed>` | `REGLER` / `VENT-1` |
| power menu | `20 20 20 50 <power>` (`50` = "P") | `REGLER` / `PUISSANCE` |

**Byte 5 (index 5) = current value** in both menus. Detection rule: `o[1] == 0x20 && line1 contains "REGLER"`; byte 4 tells which menu (`00` fan, `50` power).

### Icons (last 3 bytes before the checksum)

`08 A B`:

* `A`, low 4 bits = **power bar-graph**: `41` P1, `43` P2, `47` P3, `4F` P4 (one more bar per level). Bit 7 of `A` is set when the fan is above speed 1.
* `B` = **fan bar-graph**: `08` speed 1, `18` speed 2, `39` speed 3, `7B` speed 4.

So a single main screen gives **power and fan speed**, without opening any menu.

### When does the stove send the full screen?

**Only when the screen content has changed** since its last screen frame (phase, temperature, value…), **or right after a key**. Otherwise it answers a poll with the clock frame `B9` only. A permanent listener therefore receives every change; to force a full screen at start-up (nothing changes), press the **fan key once without +/−**: the stove sends `REGLER VENT`, the menu times out, and the next poll returns the full main screen (because it changed).

## 6. Phases seen on the main screen

| Phase | Line 1 / line 2 | Observed duration |
| --- | --- | --- |
| Off | `OFF` / (empty) | — |
| Ignition 1 | `CONTROLE` | ≈ 45 s |
| Ignition 2 | `ALLUMAGE` / `PHASE I` | ≈ 12 min |
| Ignition 3 | `ALLUMAGE` / `PHASE II` | ≈ 4 min 30 s |
| Running | `NIVEAU` / `P1`…`P4` | — (total ignition ≈ 17 min) |
| Shutdown | `NETTOYAGE` / `BRASERO` | ≈ 42 min until `OFF` (pellet feed stopped, strong ventilation to burn what is left in the burn pot; can also appear briefly while running) |
| Error | `DEFAUT …` (expected, exact wording not seen yet) | — |
| Other menus seen | `TRIER` / `LANGUE` (language menu) | — |

Screen texts are French on this stove (language setting); other languages will show other words.

## 7. Open questions

* Is `C5` universal or pairing-specific? Are bytes 2–3 installation-specific?
* Meaning of the `3D`/`3E` low bits.
* Keys: thermometer, clock, ★; `E4`/`64`.
* Error screens and their exact text.
* Other Piazzetta remotes (P956, P963, …) and stoves.
