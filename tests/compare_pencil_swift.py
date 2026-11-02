#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check: all five grain masks compared exactly with rmrender-swift.
Uses the procedural tile PNGs and portable capsule sampling. No user/device fixtures.
"""
import argparse
import ctypes as C
import base64
import json
from pathlib import Path
import random
import subprocess
import tempfile
from grain_types import Point,Rect,Mask,f32
class Raster(C.Structure):
    _fields_=[('abi',C.c_uint32),('size',C.c_size_t),('context',C.c_void_p),('render',C.c_void_p)]
SWIFT=r'''
import Foundation
import CoreGraphics
import RMLines
let grainDirectory=CommandLine.arguments[1]
// Calibration also references this unrelated text-style enum.
enum ListNumbering { case decimal, letter, roman }
while let line=readLine() {
    let input=try! JSONSerialization.jsonObject(with:Data(line.utf8)) as! [String:Any]
    if let xy=input["noise"] as? [Int] { print(StrokeGrainMask.placeNoise(x:xy[0],y:xy[1]));continue }
    let points=(input["points"] as! [[Double]]).map {
        StrokePoint(x:Float($0[0]),y:Float($0[1]),width:Float($0[2]),pressure:Float($0[3]),speed:Float($0[4]))
    }
    let b=input["box"] as! [Double],mode=input["grain"] as! Int
    let grain:StrokeGrain = mode==1 ? .stamp : mode==2 ? .noise : mode==3 ? .bristle : mode==4 ? .streak : .scratch
    let stroke=Stroke(tool:.pencil1,color:.black,thicknessScale:1,points:points)
    let bits=StrokeGrainMask.bits(for:stroke,grain:grain,in:CGRect(x:b[0],y:b[1],width:b[2],height:b[3]))!
    print("\(bits.width) \(bits.height) \(bits.bytesPerRow) \(Data(bits.data).base64EncodedString())")
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift',required=True,type=Path)
    parser.add_argument('--library',required=True,type=Path)
    parser.add_argument('--raster-library',type=Path)
    parser.add_argument('--rmlines-framework',required=True,type=Path)
    args=parser.parse_args()
    lib=C.CDLL(str(args.library.resolve()))
    lib.rmr_grain_noise.argtypes=[C.c_int64,C.c_int64];lib.rmr_grain_noise.restype=C.c_double
    lib.rmr_grain_mask.argtypes=[C.POINTER(Point),C.c_size_t,C.c_uint32,Rect,C.POINTER(Raster),C.c_void_p,C.POINTER(Mask),C.c_void_p]
    lib.rmr_grain_mask.restype=C.c_uint32;lib.rmr_mask_free.argtypes=[C.POINTER(Mask)]
    rng=random.Random(2304)
    fixtures=[{'noise':[rng.randrange(-10**12,10**12),rng.randrange(-10**12,10**12)]} for _ in range(5000)]
    for grain in [1,2,3,4,5]:
        for _ in range(100):
            points=[];x=y=0
            for i in range(rng.randrange(2,15)):
                if i%4:x+=rng.uniform(-20,20);y+=rng.uniform(-20,20)
                points.append([f32(x),f32(y),f32(rng.uniform(2.8,160)),f32(rng.uniform(-5,270)),f32(rng.uniform(0,180))])
            fixtures.append({'points':points,'grain':grain,'box':[-65,-64,129,128]})
        for pressure in [0,48,64,128,190,191.25,192,229.5,230,255]:
            for width in [2.8,8,24,80]:
                for stationary in [False,True]:
                    end=[0,0] if stationary else [40,20]
                    fixtures.append({'points':[[0,0,width,pressure,0],[*end,width,pressure,0]],'grain':grain,'box':[-32,-32,97,96]})
    with tempfile.TemporaryDirectory(prefix='rmrender-pencil-') as d:
        d=Path(d);(d/'main.swift').write_text(SWIFT)
        sources=args.swift/'Sources/RMRender';framework=args.rmlines_framework
        textures=(sources/'GrainTextures.swift').read_text().replace('import RMMarks','')
        textures=textures.replace('Bundle.module.url(forResource: resource, withExtension: "png", subdirectory: "Grain")','Optional(URL(fileURLWithPath: grainDirectory).appendingPathComponent(resource + ".png"))').replace('RMRenderLog.templateError','fatalError')
        (d/'GrainTextures.swift').write_text(textures)
        files=['SplitMix64.swift','StrokeGrainMask.swift','StrokeGrain.swift','StrokeScratch.swift','StrokeStreak.swift','StrokeEnd.swift','Calibration.swift','ScriptClass.swift','StrokeInk.swift']
        subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),'-F',str(framework),'-framework','RMLines','-Xlinker','-rpath','-Xlinker',str(framework),*[str(sources/f) for f in files],str(d/'GrainTextures.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
        lines=subprocess.run([str(d/'oracle'),str(sources/'Grain')],input=''.join(json.dumps(f)+'\n' for f in fixtures),text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(lines)==len(fixtures)
    for index,(f,want) in enumerate(zip(fixtures,lines)):
        if 'noise' in f:assert lib.rmr_grain_noise(*f['noise'])==float(want),(index,f,want)
        else:
            points=(Point*len(f['points']))(*(Point(x,y,min(max(w/4,.7),200),p,v) for x,y,w,p,v in f['points']))
            mask=Mask();status=lib.rmr_grain_mask(points,len(points),f['grain'],Rect(*f['box']),None,None,C.byref(mask),None)
            assert status==0,(index,status)
            try:
                w,h,stride,data=want.split();expected=base64.b64decode(data)
                assert (mask.width,mask.height,mask.stride)==(int(w),int(h),int(stride))
                actual=C.string_at(mask.bytes,mask.stride*mask.height)
                assert actual==expected,(index,f['grain'],sum(a!=b for a,b in zip(actual,expected)))
            finally:lib.rmr_mask_free(C.byref(mask))
    print(f'{len(fixtures)} Swift/C cases matched exactly: 5000 noise samples and {len(fixtures)-5000} stamp/noise/bristle/streak/scratch masks.')
if __name__=='__main__':main()
