#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of generated RGBA image placements against rmrender-swift
drawImage/decodeImage.
The PNG fixture is generated here with zlib; no device or document assets.
"""
import argparse,base64,json,math,random,struct,subprocess,tempfile,zlib
from pathlib import Path
from compare_scene_swift import method

def fixture():
    def chunk(t,b):return struct.pack('>I',len(b))+t+b+struct.pack('>I',zlib.crc32(t+b))
    colors=((255,0,0,255),(0,255,0,255),(0,0,255,255),(255,255,0,128))
    rows=b''.join(b'\0'+b''.join(bytes(colors[(y//4)*2+x//4]) for x in range(8)) for y in range(8))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',8,8,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b'')

MAIN=r'''
import Foundation
import CoreGraphics
import RMLines
let asset=try! Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[1]))
while let line=readLine() {
    let input=try! JSONSerialization.jsonObject(with:Data(line.utf8)) as! [[Double]]
    let image=PageImage(file:"own.png",vertices:input.map { PageImage.Vertex(x:$0[0],y:$0[1],u:$0[2],v:$0[3]) },strokesBefore:0)
    var bytes=[UInt8](repeating:0,count:64*64*4)
    bytes.withUnsafeMutableBytes { data in
        let context=CGContext(data:data.baseAddress,width:64,height:64,bitsPerComponent:8,bytesPerRow:256,space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue|CGBitmapInfo.byteOrder32Big.rawValue)!
        Oracle.render(image,asset:asset,in:context)
    }
    print(Data(bytes).base64EncodedString())
}
'''
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path)
    a=p.parse_args();rng=random.Random(1404)
    cases=[ [[-32,0,0,0],[32,0,1,0],[32,64,1,1],[-32,64,0,1]],
            [[-32,0,0,0],[0,0,.5,0],[0,64,.5,1],[-32,64,0,1]],
            [[-32,0,0,0],[32,0,0,0],[32,64,0,1],[-32,64,0,1]],
            [[32,0,0,0],[32,64,1,0],[-32,64,1,1],[-32,0,0,1]] ]
    for _ in range(100):
        angle=rng.uniform(-math.pi,math.pi);sx=rng.uniform(5,90)*rng.choice([-1,1]);sy=rng.uniform(5,90)
        x0=rng.uniform(-30,20);y0=rng.uniform(-10,50)
        u0=rng.uniform(-.3,.4);v0=rng.uniform(-.3,.4);u1=rng.uniform(.6,1.3);v1=rng.uniform(.6,1.3)
        c=math.cos(angle);s=math.sin(angle)
        cases.append([[x0+sx*c*u-sy*s*v,y0+sx*s*u+sy*c*v,u,v] for u,v in [(u0,v0),(u1,v0),(u1,v1),(u0,v1)]])
    text=''.join(' '.join(map(str,v))+'\n' for case in cases for v in case)
    got=subprocess.run([str(a.oracle.resolve())],input=text,text=True,capture_output=True,check=True).stdout.splitlines()
    source=(a.swift/'Sources/RMRender/PageRenderer.swift').read_text()
    helpers='\n'.join(method(source,k) for k in ['private static func drawImage(','private static func decodeImage(','static func convert(x:'])
    code='''import Foundation
import CoreGraphics
import RMLines
enum RMRenderLog { static func pageError(_ message:String) {} }
enum Oracle {
static let maximumImagePixels=64_000_000
static let defaultPageSize=CGSize(width:64,height:64)
'''+helpers+'''
static func render(_ image:PageImage,asset:Data,in context:CGContext) {
drawImage(image,assets:["own.png":asset],in:context,pageSize:defaultPageSize)
}
}
'''
    with tempfile.TemporaryDirectory(prefix='rmrender-images-') as temp:
        d=Path(temp);(d/'Oracle.swift').write_text(code);(d/'main.swift').write_text(MAIN);(d/'own.png').write_bytes(fixture())
        framework=a.swift/'ThirdParty/RMLines/RMLines.xcframework/macos-arm64_x86_64'
        subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),'-F',str(framework),'-framework','RMLines','-Xlinker','-rpath','-Xlinker',str(framework),str(d/'Oracle.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
        want=subprocess.run([str(d/'oracle'),str(d/'own.png')],input=''.join(json.dumps(c)+'\n' for c in cases),text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(got)==len(want)==len(cases)
    maximum=0;different=0;total=0
    for i,(g,w) in enumerate(zip(got,want)):
        x=bytes.fromhex(g);y=base64.b64decode(w);assert len(x)==len(y)==16384
        delta=[abs(a-b) for a,b in zip(x,y)];peak=max(delta);mean=sum(delta)/len(delta)
        maximum=max(maximum,peak);different+=int(x!=y);total+=sum(delta)
        assert peak<=1 and mean<=.001,(i,peak,mean)
    print(f'{len(cases)} image comparisons passed: {different} non-identical, maximum channel difference {maximum}, mean {total/(len(cases)*16384):.6f}.')
if __name__=='__main__':main()
