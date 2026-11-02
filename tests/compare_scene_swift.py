#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of synthetic stroke scenes against the rmrender-swift
PageRenderer stroke loop. The harness takes the stroke-only loop and its helpers
as written and leaves out image insertion (these fixtures contain none).
"""
import argparse
import base64
import json
from pathlib import Path
import random
import subprocess
import tempfile
from grain_types import f32

def method(source,signature):
    start=source.index(signature);opening=source.index('{',start);depth=1;at=opening+1
    while depth:
        if source[at]=='{':depth+=1
        elif source[at]=='}':depth-=1
        at+=1
    return source[start:at]

def oracle_source(source):
    helpers='\n'.join(method(source,signature) for signature in [
        'static func sharedLayerKey(', 'private static func coveredBox(',
        'private static func outline(', 'private static func strokedOutline(', 'private static func drawStroke(',
        'static func convert(_ point: StrokePoint', 'static func convert(_ point: CGPoint', 'static func convert(x:'])
    start=source.index('        let erasers = placed.enumerated()')
    end=source.index('        drawImages(before: Int.max)',start)
    loop=source[start:end]
    loop=loop.replace('''            if nextImage < placedImages.count, placedImages[nextImage].strokesBefore <= position {
                closeSharedLayer()
            }
            drawImages(before: position)
''','')
    return '''import Foundation
import CoreGraphics
import RMLines
enum ListNumbering { case decimal, letter, roman }
enum Oracle {
static let defaultPageSize=CGSize(width:64,height:64)
'''+helpers+'''
static func render(_ strokes:[Stroke], in context:CGContext) {
let pageSize=defaultPageSize
let placed=strokes.map { $0.drawn }
'''+loop+'''
}
}
'''
MAIN=r'''
import Foundation
import CoreGraphics
import RMLines
let grainDirectory=CommandLine.arguments[1]
while let line=readLine() {
    let input=try! JSONSerialization.jsonObject(with:Data(line.utf8)) as! [[String:Any]]
    let strokes=input.map { item -> Stroke in
        let points=(item["points"] as! [[Double]]).map { StrokePoint(x:Float($0[0]),y:Float($0[1]),width:Float($0[2]),pressure:Float($0[3]),speed:Float($0[4])) }
        let argb=item["argb"] as! UInt32,source=item["source"] as! Int
        return Stroke(tool:PenTool(rawValue:item["tool"] as! Int),color:PenColor(rawValue:item["color"] as! Int)!,thicknessScale:1,points:points,argb:argb==0 ? nil:argb,sourceId:source==0 ? nil:SceneItemID(author:1,counter:UInt64(source)))
    }
    var bytes=[UInt8](repeating:0,count:64*64*4)
    bytes.withUnsafeMutableBytes { data in
        let context=CGContext(data:data.baseAddress,width:64,height:64,bitsPerComponent:8,bytesPerRow:256,space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue|CGBitmapInfo.byteOrder32Big.rawValue)!
        Oracle.render(strokes,in:context)
    }
    print(Data(bytes).base64EncodedString())
}
'''
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift',required=True,type=Path)
    parser.add_argument('--oracle',required=True,type=Path)
    args=parser.parse_args();rng=random.Random(2305)
    def stroke(tool,points,source=0,argb=0,color=0):return dict(tool=tool,points=points,source=source,argb=argb,color=color)
    h=[[-20,20,40,128,0],[20,20,40,128,0]];v=[[0,0,32,255,0],[0,40,32,255,0]]
    fixtures=[[],[stroke(17,h),stroke(6,v)],[stroke(6,v),stroke(17,h)],
        [stroke(17,h),stroke(6,v),stroke(17,h)], [stroke(17,h),stroke(8,v)],
        [stroke(23,h,1),stroke(23,h,1)], [stroke(23,h,1),stroke(23,h,2)],
        [stroke(23,h,1),stroke(8,[]),stroke(23,h,1)],
        [stroke(23,h,1),stroke(23,h,1,0x800000ff)],
        [stroke(23,h,1),stroke(23,h,1),stroke(6,v)]]
    for tool in [0,1,2,3,4,5,7,12,13,14,15,16,17,18,21,23]:
        fixtures.append([stroke(tool,h),stroke(6,v)])
    for _ in range(100):
        scene=[]
        for index in range(rng.randrange(2,9)):
            tool=rng.choice([1,2,5,6,8,12,14,15,16,17,18,23,23]);source=rng.choice([0,1,1,2])
            width=rng.uniform(8,100);points=[]
            for i in range(rng.randrange(2,7)):
                points.append([f32(rng.uniform(-28,28)),f32(rng.uniform(6,58)),f32(width),f32(rng.uniform(20,255)),f32(rng.uniform(0,100))])
            scene.append(stroke(tool,points,source,rng.choice([0,0,0x80211e1c,0x400000ff]),rng.choice([0,1,6,8])))
        fixtures.append(scene)
    text=[]
    for scene in fixtures:
        text.append(str(len(scene)))
        for s in scene:
            text.append(f"{s['tool']} {s['color']} {s['argb']} {int(bool(s['source']))} 1 {s['source']} {len(s['points'])}")
            text.extend(' '.join(map(str,p)) for p in s['points'])
    result=subprocess.run([str(args.oracle.resolve())],input='\n'.join(text)+'\n',text=True,capture_output=True,check=True)
    c_lines=result.stdout.splitlines()
    with tempfile.TemporaryDirectory(prefix='rmrender-scene-') as d:
        d=Path(d);sources=args.swift/'Sources/RMRender';framework=args.swift/'ThirdParty/RMLines/RMLines.xcframework/macos-arm64_x86_64'
        (d/'Oracle.swift').write_text(oracle_source((sources/'PageRenderer.swift').read_text()))
        (d/'main.swift').write_text(MAIN)
        textures=(sources/'GrainTextures.swift').read_text().replace('import RMMarks','')
        textures=textures.replace('Bundle.module.url(forResource: resource, withExtension: "png", subdirectory: "Grain")','Optional(URL(fileURLWithPath: grainDirectory).appendingPathComponent(resource + ".png"))').replace('RMRenderLog.templateError','fatalError')
        (d/'GrainTextures.swift').write_text(textures)
        files=['StrokeOutline.swift','StrokeEnd.swift','StrokeInk.swift','StrokeAppearance.swift','StrokeGrain.swift','StrokeGrainMask.swift','StrokeStreak.swift','StrokeScratch.swift','Calibration.swift','ScriptClass.swift']
        subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),'-F',str(framework),'-framework','RMLines','-Xlinker','-rpath','-Xlinker',str(framework),*[str(sources/f) for f in files],str(d/'GrainTextures.swift'),str(d/'Oracle.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
        expected=subprocess.run([str(d/'oracle'),str(sources/'Grain')],input=''.join(json.dumps(f)+'\n' for f in fixtures),text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(expected)==len(c_lines)==len(fixtures)
    maximum=0;total=0;different=0
    for index,(got,want) in enumerate(zip(c_lines,expected)):
        a=bytes.fromhex(got);b=base64.b64decode(want);assert len(a)==len(b)==16384
        delta=[abs(x-y) for x,y in zip(a,b)];peak=max(delta);mean=sum(delta)/len(delta)
        maximum=max(maximum,peak);total+=sum(delta);different+=int(a!=b)
        assert peak<=1 and mean<=0.001,(index,peak,mean)
    print(f'{len(fixtures)} scene comparisons passed: {different} non-identical, maximum channel difference {maximum}, mean {total/(len(fixtures)*16384):.6f}.')
if __name__=='__main__':main()
