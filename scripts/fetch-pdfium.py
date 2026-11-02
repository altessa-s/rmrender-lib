#!/usr/bin/env python3
"""Fetch the pinned, checksum-verified PDFium test SDK. Never run by CMake."""
import argparse
import hashlib
import io
from pathlib import Path
import tarfile
import urllib.request

REVISION = "8125"
HASHES = {
    "linux-x64": "e1c3ce6e270cb5e0c20816b379ad9f2c1309972697c505f508de92bf5e7d6659",
    "linux-arm64": "8b8818f2ec57d1b213d952ca6d7f0ffeca9d0110487d0e20ad83ba918bc92efa",
}
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--platform", choices=HASHES, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if args.output.exists() and any(args.output.iterdir()):
    parser.error("output directory must be empty")
url = ("https://github.com/bblanchon/pdfium-binaries/releases/download/"
       f"chromium/{REVISION}/pdfium-{args.platform}.tgz")
with urllib.request.urlopen(url, timeout=120) as response:
    data = response.read(128 * 1024 * 1024 + 1)
if hashlib.sha256(data).hexdigest() != HASHES[args.platform]:
    raise SystemExit("PDFium checksum mismatch")
args.output.mkdir(parents=True, exist_ok=True)
with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
    archive.extractall(args.output, filter="data")
print(f"PDFium chromium/{REVISION}: {args.output}")
