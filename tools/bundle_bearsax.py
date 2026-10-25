#!/usr/bin/env python3
"""Копіює з бібліотеки Karoryfer Bear Sax (роялті-фрі) лише те, що використовує SN Synth:
SFZ-мапи легато і семпли, на які вони посилаються. Використання: bundle_bearsax.py <Bear_Sax> <куди>"""
import os, re, shutil, sys

src, dst = sys.argv[1], sys.argv[2]
maps = ["dynfade_legato_first_map.sfz", "growl_map.sfz", "sub_map.sfz", "noises_map.sfz", "breath.sfz"]
os.makedirs(os.path.join(dst, "Programs", "legato"), exist_ok=True)
os.makedirs(os.path.join(dst, "Samples"), exist_ok=True)
need = set()
for m in maps:
    p = os.path.join(src, "Programs", "legato", m)
    shutil.copy(p, os.path.join(dst, "Programs", "legato", m))
    for s in re.findall(r"sample=\S+", open(p, encoding="latin-1").read()):
        need.add(s.split("=", 1)[1].replace("\\", "/").split("/")[-1])
for n in sorted(need):
    shutil.copy(os.path.join(src, "Samples", n), os.path.join(dst, "Samples", n))
lic = os.path.join(src, "readme - real.txt")
if os.path.exists(lic):
    shutil.copy(lic, os.path.join(dst, "LICENSE - Karoryfer Bear Sax.txt"))
print("bundled", len(need), "samples")
