# Samsung AC IR Protocol — AR10TYHZCWKNST (reverse-engineered & confirmed)

**Status:** Protocol fully decoded and checksum **validated against all 15 captured frames**.
This is the build spec for a native ESPHome climate component (esp-idf) for the blaster.

## Summary
- The unit uses the **standard Samsung AC protocol** (same family IRremoteESP8266 calls `SAMSUNG_AC`).
  Model not in its list (AR09/AR12 are), but frame sizes, structure, field layout, and checksum all match.
- Two message types:
  - **14-byte** (2 sections) — normal command: mode / temp / fan / swing.
  - **21-byte** (3 sections) — power ON/OFF toggle (extended frame).
- Carrier **38.0 kHz**. Bytes are **LSB-first**. Each section = header + 56 data bits; sections separated
  by a gap; frame ends with a bit mark + a long stop gap.
- **Measured timings** (µs, pooled from all 15 captures — used verbatim in `samsung_ac_ir.h`):
  HDR_MARK **2990**, HDR_SPACE **9100**, BIT_MARK **470**, ONE_SPACE **1570**, ZERO_SPACE **540**,
  SECTION_GAP **3050** (between sections), STOP_SPACE **10120** (after the final section).

## Byte layout — 14-byte frame
Section 1 is **constant**: `02 92 0F 00 00 00 F0` (its own checksum baked in: F9).

Section 2 (the state):
```
byte:  0    1     2    3    4      5       6
       01  [C|2] [B|c] 71  [T|0]  [M f 1]  F0
```
- **byte0** = `0x01` (const)
- **byte1** = high nibble = checksum low nibble; low nibble = `2` (const)
- **byte2** = high nibble = `B` (const data); low nibble = checksum high nibble
- **byte3** = `0x71` (const)
- **byte4** = high nibble = **temperature** = `temp − 16` (so 23°→7, 24°→8, 25°→9; range 16–30 → 0x0–0xE); low nibble = `0`
- **byte5** = `(mode << 4) | (fan << 1) | 1`
- **byte6** = `0xF0` (const)

### Field codes (confirmed from captures)
| Field | Values |
|---|---|
| **Temp** | nibble = °C − 16 |
| **Mode** (byte5 high nibble) | cool=`1`, dry=`2`, fan=`3`, heat=`4`, auto=`8` (auto sets bit7) |
| **Fan** (3-bit, byte5 bits 1–3) | auto=`0`, L1=`2`, L2=`4`, L3=`5`, L4/turbo=`7` |

Examples decoded (section 2 shown):
```
Cool 24 FanAuto : 01 F2 BE 71 80 11 F0
Cool 23 FanAuto : 01 D2 BE 71 70 11 F0
Cool 25 FanAuto : 01 E2 BE 71 90 11 F0
Cool 24 Fan L1  : 01 E2 BE 71 80 15 F0
Cool 24 Fan L4  : 01 C2 BE 71 80 1F F0
Heat 24 FanAuto : 01 F2 BE 71 80 41 F0
Dry  23 FanAuto : 01 D2 BE 71 70 21 F0
Fan-mode        : 01 D2 BE 71 80 39 F0
Auto 24         : 01 F2 BE 71 80 81 F0
```

## Byte layout — 21-byte power frame
Three sections:
- Section 1: `02 92 0F 00 00 00 F0` (same const as above)
- Section 2: `01 D2 0F 00 00 00 00` (fixed "power command" section)
- Section 3: full state + power flag:
```
Power ON  (cool/24/turbo): 01 C2 BE 71 80 1F F0
Power OFF                 : 01 12 BF 71 80 11 C0
```
Power ON vs OFF differ in byte2 low nibble (checksum) and **byte6** (`F0` on / `C0` off) — treat byte6
high nibble as the power/enable indicator.

### Power-on TODO — RESOLVED
The byte6 high nibble is the absolute power state: `0xF0` → power bits `0b11` (on), `0xC0` → `0b00` (off).
This is the same `power_1`/`power_2` field (byte6 bits 4–5 of each section) modelled by IRremoteESP8266
and by the native ESPHome `samsung` component ([PR #8004](https://github.com/esphome/esphome/pull/8004),
hardware-tested on real Samsung units).

**Question:** does the 14-byte command power the unit on from OFF, or is an explicit 21-byte power-on
frame required? **Answer:** the 14-byte command powers it on. Because every normal 14-byte command already
carries byte6 = `0xF0` (power bits on), sending it from OFF turns the unit on — no separate extended
power-on frame is needed. PR #8004 relies on exactly this: it sends a plain 14-byte frame to turn on and
only uses the 21-byte extended frame to turn **OFF**. Our own captures agree (OFF was captured as a 21-byte
extended frame; normal commands are 14-byte).

**Implemented accordingly:** `transmit_state()` sends a 14-byte command for every active mode (also powering
on from OFF) and the 21-byte extended frame for OFF. The separately-captured 21-byte power-ON frame
(`cool/24/turbo`) is therefore not required and is not emitted.

## Checksum — CONFIRMED (matches all frames, both/all sections)
Per 7-byte section:
```python
def countBits(v): return bin(v & 0xFF).count("1")
def section_checksum(sec):          # sec = 7 bytes
    s  = countBits(sec[0])
    s += countBits(sec[1] & 0x0F)         # low nibble of byte1
    s += countBits((sec[2] >> 4) & 0x0F)  # high nibble of byte2
    s += sum(countBits(b) for b in sec[3:7])
    return s ^ 0xFF                        # bitwise invert (8-bit)
# stored: byte1 high nibble = checksum & 0x0F ; byte2 low nibble = checksum >> 4
```
Validated: power_on/off, cool/heat/dry/fan/auto, temps 23/24/25, fans auto/1/2/3/4 — all sections pass.

## Build plan — ESPHome native component (chosen approach)
1. New external component `samsung_ac_ir` implementing ESPHome's `climate_ir` base (Climate + IR),
   esp-idf compatible. Reference: ESPHome `climate_ir` framework + IRremoteESP8266 `ir_Samsung.cpp`
   + ESPHome PR #8004 (WIP Samsung climate_ir) for structure.
2. `transmit_state()`: build section2 (or the 21-byte extended frame for power changes) from the
   current mode/temp/fan using the field codes above, compute section checksums, send via the
   Samsung AC timing on the `remote_transmitter`.
3. Optional: use `remote_receiver` (ir_rf_proxy already on the learner) to read state back.
4. Verify against captures: encoding Cool/24/FanAuto etc. must reproduce the exact bytes above.
5. Extend/verify: full temp range 16–30, swing, and the power-bit position for arbitrary ON states.

## Related work found
- IRremoteESP8266 `SAMSUNG_AC` — authoritative protocol (Arduino); the checksum above is its algorithm.
- ESPHome PR #8004 — native Samsung `climate_ir` (WIP, ESP8266-tested, ESP32-IDF unverified).
- SmartIR — climate card via lookup codesets (not true generation).
- Captured raw codes live in `esphome/samsung-ac-ir-codes.md` (this project).
