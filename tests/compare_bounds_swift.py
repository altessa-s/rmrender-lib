#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of text-free page growth and width calibration against
rmrender-swift contentBounds. The harness omits the text-layout branch and uses
the edge arithmetic as written. All fixtures are generated; no user documents or tablet assets.
"""
import argparse,json,random,subprocess,tempfile
from pathlib import Path
from compare_scene_swift import method
from grain_types import f32
MAIN=r'''
import Foundation
import CoreGraphics
import RMLines
while let line=readLine() {
 let input=try! JSONSerialization.jsonObject(with:Data(line.utf8)) as! [String:Any]
 let w=input["w"] as! Double,h=input["h"] as! Double,mode=input["edge"] as! Int,margin=input["margin"] as! Double
 let strokes=(input["strokes"] as! [[String:Any]]).map { item in
  Stroke(tool:PenTool(rawValue:item["tool"] as! Int),color:.black,thicknessScale:1,points:(item["points"] as! [[Double]]).map { StrokePoint(x:Float($0[0]),y:Float($0[1]),width:Float($0[2]),pressure:Float($0[3])) })
 }
 let images=(input["images"] as! [[[Double]]]).map { points in
  PageImage(file:"own",vertices:points.map { PageImage.Vertex(x:$0[0],y:$0[1],u:0,v:0) },strokesBefore:0)
 }
 let edge:Oracle.InkEdge=mode==0 ? .ribbon:mode==1 ? .uncut:.centreline(margin:margin)
 let b=Oracle.contentBounds(of:Page(strokes:strokes,images:images),pageSize:CGSize(width:w,height:h),edge:edge)
 print("\(b.minX-w/2) \(h-b.maxY) \(b.width) \(b.height)")
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path);a=p.parse_args()
 rng=random.Random(1872);cases=[]
 for edge in range(3):
  for x,y in [(0,200),(0,240),(0,247),(0,248),(50,200),(51,100),(-51,100),(2000,400),(0,-10)]:
   for tool in [1,6,8,12,15,16,17,23]:
    cases.append(dict(w=100,h=200,edge=edge,margin=20 if edge==2 else 0,strokes=[dict(tool=tool,points=[[x,y,40,128]])],images=[]))
 for _ in range(1000):
  w,h=rng.choice([(1404,1872),(954,1696),(1872,1404),(100,200)]);edge=rng.randrange(3)
  strokes=[]
  for _ in range(rng.randrange(8)):
   strokes.append(dict(tool=rng.choice([0,1,2,3,4,5,6,7,8,12,13,14,15,16,17,18,21,23,99]),points=[[f32(rng.uniform(-w,w)),f32(rng.uniform(-h/4,2*h)),f32(rng.uniform(0,900)),f32(rng.uniform(-10,300))] for _ in range(rng.randrange(10))]))
  images=[[[rng.uniform(-w,w),rng.uniform(-h/4,2*h)] for _ in range(4)] for _ in range(rng.randrange(3))]
  cases.append(dict(w=w,h=h,edge=edge,margin=rng.uniform(0,80) if edge==2 else 0,strokes=strokes,images=images))
 text=[]
 for c in cases:
  text.append(f"{c['w']} {c['h']} {c['edge']} {c['margin']} {len(c['strokes'])} {len(c['images'])}")
  for s in c['strokes']:
   text.append(f"{s['tool']} {len(s['points'])}");text.extend(' '.join(map(str,p)) for p in s['points'])
  for im in c['images']:text.extend(' '.join(map(str,v)) for v in im)
 got=subprocess.run([str(a.oracle.resolve())],input='\n'.join(text)+'\n',text=True,capture_output=True,check=True).stdout.splitlines()
 src=a.swift/'Sources/RMRender';source=(src/'PageRenderer.swift').read_text()
 body=method(source,'    static func contentBounds(').replace('of page: RMPage','of page: Page')
 body=body.replace('let geometry = laidOut ?? page.text.map { TextGeometry($0, pageSize: pageSize) }','let geometry: TextGeometry? = laidOut')
 start=body.index('        if let text = page.text, let geometry, let frame = geometry.frameBottom {')
 end=body.index('        // A sheet carried downwards',start);body=body[:start]+body[end:]
 code='''import Foundation
import CoreGraphics
import RMLines
enum ListNumbering { case decimal, letter, roman }
enum GrainTextures { enum Family { case light, base, hard } }
struct Page { let strokes:[Stroke]; let images:[PageImage] }
typealias TextGeometry = Never
enum Oracle {
static let defaultPageSize=CGSize(width:1404,height:1872)
static func strokes(of page:Page,geometry:TextGeometry?)->[Stroke] { page.strokes.map { $0.drawn } }
static func images(of page:Page,geometry:TextGeometry?)->[PageImage] { page.images }
'''+body+'\n'+method(source,'    enum InkEdge:')+'\n'+method(source,'    static func convert(_ point: StrokePoint')+'\n'+method(source,'    static func convert(x:')+'\n}\n'
 with tempfile.TemporaryDirectory(prefix='rmrender-bounds-') as temp:
  d=Path(temp);(d/'Oracle.swift').write_text(code);(d/'main.swift').write_text(MAIN)
  framework=a.swift/'ThirdParty/RMLines/RMLines.xcframework/macos-arm64_x86_64'
  files=['StrokeInk.swift','StrokeGrain.swift','Calibration.swift','ScriptClass.swift']
  subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),'-F',str(framework),'-framework','RMLines','-Xlinker','-rpath','-Xlinker',str(framework),*[str(src/f) for f in files],str(d/'Oracle.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
  want=subprocess.run([str(d/'oracle')],input=''.join(json.dumps(c)+'\n' for c in cases),text=True,capture_output=True,check=True).stdout.splitlines()
 assert len(got)==len(want)==len(cases)
 peak=0
 for i,(g,w) in enumerate(zip(got,want)):
  delta=max(abs(x-y) for x,y in zip(map(float,g.split()),map(float,w.split())));peak=max(peak,delta)
  assert delta<=1e-9,(i,g,w,delta)
 print(f'{len(cases)} page bounds comparisons passed; maximum absolute difference {peak:.3g} units.')
if __name__=='__main__':main()
