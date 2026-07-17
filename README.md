# Samsung AC IR

ESPHome external component that exposes a Samsung air conditioner (tested on an **AR10TYHZCWKNST**) as a native Home Assistant **climate** entity over infrared. It *generates* Samsung IR frames on the fly from the current mode/temperature/fan — not a fixed set of replayed codes — so every state combination works from one entity.

Built on ESPHome's [`climate_ir`](https://esphome.io/components/climate/climate_ir) base. Protocol reverse-engineered and the checksum validated against 15 real remote captures; encoder verified byte-for-byte and the example compiled on ESPHome 2026.7 / esp-idf.

## Features

- Real climate card: **mode** (cool / heat / dry / fan-only / auto), **target temperature** (16–30 °C), **fan speed**.
- Fan speeds: Auto / Low / Medium / High, plus a custom **Turbo** speed (the remote's 4th / L4 speed, fan code 7).
- Correct power handling: 14-byte command powers the unit on and applies state; 21-byte extended frame for power off.
- Optional **state read-back**: wire a `remote_receiver` and the entity decodes frames from the physical remote.
- Timings are the microsecond values measured from real captures of this unit (38 kHz).

## Requirements

- ESPHome **2026.7** or newer (uses `climate_ir_with_receiver_schema` / `new_climate_ir`).
- A `remote_transmitter` (e.g. an IR LED / HX1838-T module) — esp-idf and Arduino frameworks both supported.

## Installation

Add the component as an external source and drop in a `climate` platform. No files to copy.

```yaml
external_components:
  - source: github://axelwai/samsung_ac_ir
    components: [samsung_ac_ir]
    refresh: 1h

remote_transmitter:
  id: ir_tx
  pin: GPIO21
  carrier_duty_percent: 50%

climate:
  - platform: samsung_ac_ir
    name: "Samsung AC"
    # transmitter_id auto-resolves when there is a single remote_transmitter.
```

A full working device config (ESP32-C3 Super Mini, GPIO21) is in [`example-samsung-ac.yaml`](example-samsung-ac.yaml).

### Optional: read state back

```yaml
remote_receiver:
  id: ir_rx
  pin:
    number: GPIO20
    inverted: true
  tolerance: 55%          # wide tolerance for the multi-section Samsung frames

climate:
  - platform: samsung_ac_ir
    name: "Samsung AC"
    receiver_id: ir_rx
```

## Fan / mode / power notes

- **Turbo** appears as a custom fan mode named `Turbo` — no configuration needed.
- **Power**: any active mode sends a 14-byte command whose power bits are set, which also turns the unit on from off. `Off` sends the 21-byte extended power-off frame. No separate power-on frame is required — the same behavior as the hardware-tested native [`samsung`](https://github.com/esphome/esphome/pull/8004) component.
- The reverse-engineered protocol (byte layout, field codes, checksum, measured timings) is documented in [`samsung-ac-protocol.md`](samsung-ac-protocol.md).

## Verify the encoder (no hardware needed)

```bash
python3 verify_encoder.py
```

All vectors print `OK` — they are the exact bytes decoded from the real remote captures (e.g. `Cool/24/FanAuto = 01 F2 BE 71 80 11 F0`).

## Tested devices

| Model | Status | Notes |
|---|---|---|
| AR10TYHZCWKNST | Protocol decoded, encoder + build verified | Standard Samsung AC protocol family |

Other Samsung units in the same protocol family (frame sizes, field layout, and checksum matching) are likely to work. Reports welcome.

## Credits

- [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) `ir_Samsung.cpp` — the authoritative Samsung AC protocol and checksum.
- ESPHome [PR #8004](https://github.com/esphome/esphome/pull/8004) (native `samsung` climate_ir, WIP) — cross-checked framing, fan codes, and power behavior.

## License

[MIT](LICENSE)
