#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of C outlines against rmrender-swift on deterministic,
generated fixtures.
macOS only. Uses bundled RMLines framework; no user documents or device assets.
The tiny extension supplies raw-width conversion, not an outline implementation.
"""
import argparse
import ctypes as C
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import tempfile

SWIFT = r'''
import CoreGraphics
import Foundation
import RMLines
extension StrokePoint {
    var lineWidth: Double { min(max(Double(width) / 4, 0.7), 200) }
}
while let line = readLine() {
    let input = try! JSONSerialization.jsonObject(with: Data(line.utf8)) as! [String: Any]
    let points = (input["points"] as! [[Double]]).map {
        StrokePoint(x: Float($0[0]), y: Float($0[1]), width: Float($0[2]), pressure: 128)
    }
    let stroke = Stroke(tool: PenTool(rawValue: input["tool"] as! Int), color: .black,
                        thicknessScale: 1, points: points)
    let elements: [[Double]] = StrokeOutline.elements(of: stroke).map {
        switch $0 {
        case .move(let p): return [0, p.x, p.y, 0, 0, 0, 0]
        case .line(let p): return [1, p.x, p.y, 0, 0, 0, 0]
        case .curve(let a, let b, let p): return [2, p.x, p.y, a.x, a.y, b.x, b.y]
        case .close: return [3, 0, 0, 0, 0, 0, 0]
        }
    }
    print(String(data: try! JSONSerialization.data(withJSONObject: elements), encoding: .utf8)!)
}
'''

class Point(C.Structure):
    _fields_ = [('x', C.c_double), ('y', C.c_double)]
class Sample(C.Structure):
    _fields_ = [('position', Point), ('width', C.c_double)]
class Element(C.Structure):
    _fields_ = [('kind', C.c_uint32), ('end', Point), ('control1', Point), ('control2', Point)]
class PathResult(C.Structure):
    _fields_ = [('elements', C.POINTER(Element)), ('count', C.c_size_t)]

def f32(value):
    return struct.unpack('f', struct.pack('f', value))[0]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift', required=True, type=Path)
    parser.add_argument('--library', required=True, type=Path)
    parser.add_argument('--rmlines-framework', type=Path, help='Directory containing RMLines.framework')
    args = parser.parse_args()
    lib = C.CDLL(str(args.library.resolve()))
    lib.rmr_outline.argtypes = [C.POINTER(Sample), C.c_size_t, C.c_uint32, C.c_uint32,
                               C.c_void_p, C.POINTER(PathResult), C.c_void_p]
    lib.rmr_outline.restype = C.c_uint32
    lib.rmr_path_free.argtypes = [C.POINTER(PathResult)]
    rng = random.Random(2301)
    fixtures = []
    for tool in [2, 3, 5, 18]:
        for points in [[], [[0, 0, 20]], [[0, 0, 20], [0, 0, 40]],
                       [[i, 0, 20] for i in range(1000)]]:
            fixtures.append(dict(tool=tool, points=points))
        for _ in range(250):
            x = y = 0.0
            points = []
            for i in range(rng.randrange(2, 60)):
                if i % 7:
                    x += rng.uniform(-50, 50)
                    y += rng.uniform(-50, 50)
                points.append([f32(x), f32(y), f32(rng.uniform(-10, 1000))])
            fixtures.append(dict(tool=tool, points=points))
    with tempfile.TemporaryDirectory(prefix='rmrender-swift-') as folder:
        folder = Path(folder)
        main_file = folder / 'main.swift'
        main_file.write_text(SWIFT)
        framework = args.rmlines_framework or args.swift / 'ThirdParty/RMLines/RMLines.xcframework/macos-arm64_x86_64'
        sources = args.swift / 'Sources/RMRender'
        subprocess.run(['swiftc', '-O', '-module-cache-path', str(folder / 'cache'),
                        '-F', str(framework), '-framework', 'RMLines', '-Xlinker', '-rpath',
                        '-Xlinker', str(framework), str(sources / 'StrokeOutline.swift'),
                        str(sources / 'StrokeEnd.swift'), str(main_file), '-o', str(folder / 'oracle')], check=True)
        result = subprocess.run([str(folder / 'oracle')], input=''.join(json.dumps(f)+'\n' for f in fixtures),
                                text=True, capture_output=True, check=True)
    outputs = result.stdout.splitlines()
    assert len(outputs) == len(fixtures)
    for index, (fixture, output) in enumerate(zip(fixtures, outputs)):
        points = fixture['points']
        samples = (Sample * len(points))(*(Sample(Point(x, y), min(max(w/4, .7), 200)) for x, y, w in points))
        highlighter = fixture['tool'] in (5, 18)
        moving = points and any(p[:2] != points[0][:2] for p in points)
        cap = (2 if moving else 1) if highlighter else 0
        join = 2 if highlighter else 0
        path = PathResult()
        assert lib.rmr_outline(samples, len(samples), cap, join, None, C.byref(path), None) == 0
        try:
            expected = json.loads(output)
            assert path.count == len(expected), (index, path.count, len(expected))
            for j, want in enumerate(expected):
                e = path.elements[j]
                got = [e.kind, e.end.x, e.end.y, e.control1.x, e.control1.y, e.control2.x, e.control2.y]
                assert all(math.isclose(a, b, abs_tol=1e-9, rel_tol=1e-12) for a,b in zip(got,want)), (index,j,got,want)
        finally:
            lib.rmr_path_free(C.byref(path))
    print(f'{len(fixtures)} Swift/C outline fixtures matched (absolute tolerance 1e-9).')

if __name__ == '__main__':
    main()
