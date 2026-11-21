#!/usr/bin/env python3
"""Regression check: samsung_ac_ir.kicad_sch (the master, edited in KiCad) against the wiring
traced from the working build (2026-11-21). If you change the circuit on purpose, update EXPECTED below to match.
Exports the netlist with kicad-cli and compares every net. Exit 0 = match."""
import subprocess, sys, tempfile, os
import xml.etree.ElementTree as ET

SCH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "samsung_ac_ir.kicad_sch")

# Each set = one net. Node = "REF.pin" where pin is the pin name (or number for resistors).
EXPECTED = [
    {"U1.5V", "R1.1"},                  # + rail (5V) -> 10 Ω pair
    {"R1.2", "R2.1"},                   # 10 Ω + 10 Ω in series
    {"R2.2", "D1.A"},                   # pair -> LED long leg
    {"D1.K", "Q1.C"},                   # LED short leg -> collector
    {"Q1.B", "R3.2"},                   # base <- 1 kΩ
    {"R3.1", "U1.GPIO21"},              # 1 kΩ <- GPIO21 (IR TX)
    {"U1.GND", "Q1.E", "U2.GND"},       # − rail
    {"U1.3V3", "U2.VCC"},               # receiver power
    {"U1.GPIO20", "U2.OUT"},            # receiver signal (IR RX)
]
NAMED = {"GND": "U1.GND", "+5V": "U1.5V", "+3V3": "U1.3V3", "GPIO21": "U1.GPIO21", "GPIO20": "U1.GPIO20"}

def main():
    if not os.path.exists(SCH):
        print(f"FAIL: {SCH} missing"); return 1
    with tempfile.TemporaryDirectory() as d:
        out = os.path.join(d, "n.xml")
        r = subprocess.run(["kicad-cli", "sch", "export", "netlist", "--format", "kicadxml", "-o", out, SCH],
                           capture_output=True, text=True)
        if r.returncode or not os.path.exists(out):
            print("FAIL: netlist export\n", r.stdout, r.stderr); return 1
        root = ET.parse(out).getroot()
    got, names = [], {}
    for net in root.iter("net"):
        nodes = set()
        for n in net.iter("node"):
            ref = n.get("ref")
            if ref.startswith("#"):
                continue
            num, func = n.get("pin"), n.get("pinfunction")
            if func and func.endswith("_" + num):          # KiCad 10 writes pinfunction as NAME_NUMBER
                func = func[: -len(num) - 1]
            pin = num if (ref.startswith("R") or not func) else func
            nodes.add(f"{ref}.{pin}")
        if nodes:
            got.append(nodes); names[frozenset(nodes)] = net.get("name")
    ok = True
    for e in EXPECTED:
        if e not in got:
            ok = False; print("MISSING net:", sorted(e))
    for g in got:
        if g not in EXPECTED:
            ok = False; print("UNEXPECTED net:", names[frozenset(g)], sorted(g))
    for name, node in NAMED.items():
        hit = [n for n, s in names.items() if node in n]
        if not hit or names[hit[0]].lstrip("/") != name:
            ok = False; print(f"NAME: net with {node} should be '{name}', got {names[hit[0]] if hit else None}")
    print("OK: netlist matches traced wiring" if ok else "FAIL")
    return 0 if ok else 1

sys.exit(main())
