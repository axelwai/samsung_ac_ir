#!/usr/bin/env python3
"""Export samsung_ac_ir.svg cropped to the circuit, so it reads well on GitHub.

kicad-cli always exports the whole page. This runs `kicad-cli sch export svg
--exclude-drawing-sheet`, then sets the SVG viewBox to the bounding box of the drawn
geometry plus a margin. KiCad draws text as stroked paths with only absolute M/L(/Z)
commands; its <text> elements are invisible (opacity 0, there for search) and are ignored.
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
NAME = "samsung_ac_ir"
MARGIN = 5.0  # mm

def main():
    with tempfile.TemporaryDirectory() as d:
        r = subprocess.run(["kicad-cli", "sch", "export", "svg", "--exclude-drawing-sheet", "-o", d,
                            os.path.join(HERE, f"{NAME}.kicad_sch")], capture_output=True, text=True)
        src = os.path.join(d, f"{NAME}.svg")
        if r.returncode or not os.path.exists(src):
            sys.exit(f"kicad-cli export failed:\n{r.stdout}{r.stderr}")
        svg = open(src).read()

    page = re.search(r'viewBox="0\.0+ 0\.0+ ([\d.]+) ([\d.]+)"', svg)
    if not page:
        sys.exit("unexpected SVG header: no page viewBox")
    pw, ph = float(page.group(1)), float(page.group(2))

    xs, ys = [], []
    for dattr in re.findall(r'\sd="([^"]*)"', svg):
        if re.search(r"[^MLZz\d.,\s-]", dattr):   # absolute moves/lines (comma or space separated), close-path
            sys.exit("SVG path uses commands other than absolute M/L/Z; cropping logic needs updating")
        nums = [float(n) for n in re.findall(r"-?[\d.]+", dattr)]
        xs += nums[0::2]; ys += nums[1::2]
    for cx, cy, rr in re.findall(r'<circle cx="([-\d.]+)" cy="([-\d.]+)" r="([\d.]+)"', svg):
        cx, cy, rr = float(cx), float(cy), float(rr)
        xs += [cx - rr, cx + rr]; ys += [cy - rr, cy + rr]
    for x, y, w, h in re.findall(r'<rect x="([-\d.]+)" y="([-\d.]+)" width="([\d.]+)" height="([\d.]+)"', svg):
        x, y, w, h = map(float, (x, y, w, h))
        if x == 0 and y == 0 and abs(w - pw) < 0.01 and abs(h - ph) < 0.01:
            continue  # page background
        xs += [x, x + w]; ys += [y, y + h]
    if not xs:
        sys.exit("no geometry found")

    x0, y0 = max(min(xs) - MARGIN, 0), max(min(ys) - MARGIN, 0)
    x1, y1 = min(max(xs) + MARGIN, pw), min(max(ys) + MARGIN, ph)
    w, h = x1 - x0, y1 - y0
    svg, n = re.subn(r'width="[\d.]+mm" height="[\d.]+mm" viewBox="[^"]*"',
                     f'width="{w:.4f}mm" height="{h:.4f}mm" viewBox="{x0:.4f} {y0:.4f} {w:.4f} {h:.4f}"', svg, count=1)
    if n != 1:
        sys.exit("could not rewrite the <svg> size attributes")
    out = os.path.join(HERE, f"{NAME}.svg")
    open(out, "w").write(svg)
    print(f"wrote {NAME}.svg cropped to {w:.1f} x {h:.1f} mm (page {pw:.0f} x {ph:.0f})")

main()
