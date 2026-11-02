#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of paper size, rounded sheets and placement against
rmrender-swift.
Extracts the tablet sheet method and calibration constant from the Swift sources.
"""
import argparse,json,random,subprocess,tempfile
from pathlib import Path
from compare_scene_swift import method
MAIN=r'''
import Foundation
import CoreGraphics
while let line=readLine() {
 let v=try! JSONSerialization.jsonObject(with:Data(line.utf8)) as! [Double]
 let canvas=CGSize(width:v[1],height:v[2])
 let paper=[PaperSize.a4,.letter,.tablet][Int(v[0])].points(forCanvas:canvas)
 let scale=paper.width/canvas.width
 let bounds=CGRect(x:v[3]+canvas.width/2,y:canvas.height-(v[4]+v[6]),width:v[5],height:v[6])
 let sheet=PageRenderer.grownSheet(from:bounds,floor:paper,scale:scale)
 let t=PageRenderer.placement(of:bounds,on:sheet,scale:scale)
 let tx=t.tx+canvas.width/2*scale,ty=t.ty+canvas.height*scale
 print("\(paper.width) \(paper.height) \(sheet.width) \(sheet.height) \(scale) 0 0 \(-scale) \(tx) \(ty)")
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path);a=p.parse_args()
 rng=random.Random(84189);cases=[]
 for paper in range(3):
  for w,h in [(1404,1872),(1872,1404),(820,1458),(1458,820),(100,100)]:
   for x,y,bw,bh in [(-w/2,0,w,h),(-h/2,-30,h,h*2),(-w/2,0,w,4000.5)]:
    cases.append([paper,w,h,x,y,bw,bh])
 for _ in range(2000):
  paper=rng.randrange(3);w=rng.uniform(400,2400);h=rng.uniform(400,3000)
  cases.append([paper,w,h,rng.uniform(-3000,0),rng.uniform(-500,0),rng.uniform(w,6000),rng.uniform(h,9000)])
 got=subprocess.run([str(a.oracle.resolve())],input=''.join(' '.join(map(str,c))+'\n' for c in cases),text=True,capture_output=True,check=True).stdout.splitlines()
 src=a.swift/'Sources/RMRender';source=(src/'PageRenderer.swift').read_text()
 code='import CoreGraphics\npublic enum PageRenderer {\npublic static let defaultPageSize=CGSize(width:1404,height:1872)\n'+method(source,'    static func grownSheet(')+'\n'+method(source,'    static func placement(')+'\n}\n'
 constant=next(line.strip() for line in (src/'Calibration.swift').read_text().splitlines() if 'public static let pointsPerUnit:' in line)
 code+='public enum Calibration { '+constant+' }\n'
 code+='public enum AnnotationGeometry {\n'+method((src/'AnnotatedPDFRenderer.swift').read_text(),'    public static func sheet(forScene')+'\n}\n'
 with tempfile.TemporaryDirectory(prefix='rmrender-paper-') as temp:
  d=Path(temp);(d/'Oracle.swift').write_text(code);(d/'main.swift').write_text(MAIN)
  framework=a.swift/'ThirdParty/RMMarks/RMMarks.xcframework/macos-arm64_x86_64'
  subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),'-F',str(framework),'-framework','RMMarks','-Xlinker','-rpath','-Xlinker',str(framework),str(src/'PaperSize.swift'),str(d/'Oracle.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
  want=subprocess.run([str(d/'oracle')],input=''.join(json.dumps(c)+'\n' for c in cases),text=True,capture_output=True,check=True).stdout.splitlines()
 assert len(got)==len(want)==len(cases)
 peak=0
 for i,(g,w) in enumerate(zip(got,want)):
  g=list(map(float,g.split()));w=list(map(float,w.split()))
  assert g[2:4]==w[2:4],(i,g,w)
  delta=max(abs(x-y) for x,y in zip(g,w));peak=max(peak,delta)
  assert delta<=1e-9,(i,g,w,delta)
 print(f'{len(cases)} paper/sheet comparisons passed: rounded sizes exact; maximum absolute difference {peak:.3g}.')
if __name__=='__main__':main()
