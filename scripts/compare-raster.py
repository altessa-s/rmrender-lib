#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Compare decoded RGBA pixels. Exit 1 on a difference; never compare PNG encodings."""
import argparse
import json
from pathlib import Path
from PIL import Image, ImageChops, ImageStat

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("left", type=Path)
parser.add_argument("right", type=Path)
parser.add_argument("--difference", type=Path, help="Write absolute RGBA difference times four")
args = parser.parse_args()
with Image.open(args.left) as a, Image.open(args.right) as b:
    left, right = a.convert("RGBA"), b.convert("RGBA")
if left.size != right.size:
    print(json.dumps({"equal": False, "left_size": left.size, "right_size": right.size}))
    raise SystemExit(1)
difference = ImageChops.difference(left, right)
channels = difference.split()
mask = channels[0]
for channel in channels[1:]:
    mask = ImageChops.lighter(mask, channel)
histogram = mask.histogram()
changed = sum(histogram[1:])
print(json.dumps({"equal": changed == 0, "size": left.size,
                  "different_pixels": changed,
                  "total_pixels": left.width * left.height,
                  "maximum_channel_difference": max(i for i, count in enumerate(histogram) if count),
                  "mean_absolute_rgba_difference": ImageStat.Stat(difference).mean}))
if args.difference:
    # Include alpha differences in the visible map as white. The output alpha
    # is opaque, so a transparent difference cannot accidentally look black.
    visible = [ImageChops.lighter(channel, channels[3]) for channel in channels[:3]]
    Image.merge("RGB", tuple(visible)).point(lambda value: min(255, value * 4)).save(args.difference)
raise SystemExit(1 if changed else 0)
