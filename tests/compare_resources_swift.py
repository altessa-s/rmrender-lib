#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check: grain and template assets match an rmrender-swift checkout
byte for byte, and the generated embedded tiles and templates are current."""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift', type=Path, required=True)
    args = parser.parse_args()
    for swift, native in [('Grain', 'grain'), ('Templates', 'templates')]:
        a = {p.name: p.read_bytes() for p in (args.swift/'Sources/RMRender'/swift).iterdir() if p.is_file()}
        b = {p.name: p.read_bytes() for p in (ROOT/'resources'/native).iterdir() if p.is_file()}
        assert a == b, f'{swift}: asset names or bytes differ'
        print(f'{len(a)} {swift} assets match byte for byte')
    for script in ['embed-pencil.py', 'embed-marker.py', 'generate-templates.py']:
        subprocess.run(['python3', str(ROOT/'scripts'/script), '--check'], check=True)

if __name__ == '__main__':
    main()
