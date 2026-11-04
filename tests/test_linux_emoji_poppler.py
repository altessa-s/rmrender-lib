# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Check default reading order as well as raw Unicode using independent Poppler."""
import pathlib
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory(prefix="rmrender-emoji-") as directory:
    preview = pathlib.Path(directory) / "emoji.png"
    subprocess.run([sys.argv[1], sys.argv[2], str(preview)], check=True)
    cases = sorted(pathlib.Path(directory).glob("emoji.png-*.txt"))
    assert cases, "No PDF extraction fixtures generated"
    for expected_path in cases:
        expected = expected_path.read_text(encoding="utf-8")
        for mode in ([], ["-raw"]):
            result = subprocess.check_output(
                [sys.argv[3], *mode, "-enc", "UTF-8", str(expected_path.with_suffix(".pdf")), "-"]
            ).decode("utf-8").rstrip("\r\n\f")
            assert result == expected, (expected_path.name, mode, result, expected)
    print(f"{len(cases)} PDFs: exact default and raw Poppler extraction")
