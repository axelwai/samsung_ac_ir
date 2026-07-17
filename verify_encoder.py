#!/usr/bin/env python3
"""Reference encoder mirroring the C++ component. Proves the encoder reproduces
the exact bytes decoded from the real remote captures."""

def count_bits(v): return bin(v & 0xFF).count("1")

def set_cksum(s):
    su = (count_bits(s[0]) + count_bits(s[1] & 0x0F) + count_bits((s[2] >> 4) & 0x0F)
          + sum(count_bits(b) for b in s[3:7]))
    ck = su ^ 0xFF
    s[1] = (s[1] & 0x0F) | ((ck & 0x0F) << 4)
    s[2] = (s[2] & 0xF0) | ((ck >> 4) & 0x0F)
    return s

MODES = {"cool": 1, "dry": 2, "fan": 3, "heat": 4, "auto": 8}
FANS  = {"auto": 0, "low": 2, "med": 4, "high": 5, "turbo": 7}

def state(temp, mode, fan, byte6=0xF0):
    s = [0x01, 0x02, 0xB0, 0x71, (temp - 16) << 4,
         (MODES[mode] << 4) | (FANS[fan] << 1) | 1, byte6]
    return set_cksum(s)

# expected section-2 bytes decoded from the actual remote captures
TESTS = {
    "Cool 24 auto":  (state(24, "cool", "auto"),  [0x01,0xF2,0xBE,0x71,0x80,0x11,0xF0]),
    "Cool 23 auto":  (state(23, "cool", "auto"),  [0x01,0xD2,0xBE,0x71,0x70,0x11,0xF0]),
    "Cool 25 auto":  (state(25, "cool", "auto"),  [0x01,0xE2,0xBE,0x71,0x90,0x11,0xF0]),
    "Cool 24 low":   (state(24, "cool", "low"),   [0x01,0xE2,0xBE,0x71,0x80,0x15,0xF0]),
    "Cool 24 turbo": (state(24, "cool", "turbo"), [0x01,0xC2,0xBE,0x71,0x80,0x1F,0xF0]),
    "Heat 24 auto":  (state(24, "heat", "auto"),  [0x01,0xF2,0xBE,0x71,0x80,0x41,0xF0]),
    "Dry 23 auto":   (state(23, "dry",  "auto"),  [0x01,0xD2,0xBE,0x71,0x70,0x21,0xF0]),
    "Fan mode":      (state(24, "fan",  "high"),  None),  # informational
    "Auto 24":       (state(24, "auto", "auto"),  [0x01,0xF2,0xBE,0x71,0x80,0x81,0xF0]),
    "PowerOFF sec3": (state(24, "cool", "auto", 0xC0), [0x01,0x12,0xBF,0x71,0x80,0x11,0xC0]),
    "PowerON sec3":  (state(24, "cool", "turbo",0xF0), [0x01,0xC2,0xBE,0x71,0x80,0x1F,0xF0]),
}

ok = True
for name, (got, exp) in TESTS.items():
    g = " ".join(f"{x:02X}" for x in got)
    if exp is None:
        print(f"  ..  {name}: {g}")
        continue
    match = got == exp
    ok &= match
    e = " ".join(f"{x:02X}" for x in exp)
    print(f"  {'OK ' if match else 'FAIL'} {name}: {g}" + ("" if match else f"  != {e}"))
print("\nALL ENCODER VECTORS PASS ✅" if ok else "\nENCODER MISMATCH ✗")
