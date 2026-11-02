#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of generated resolved-pen cases against rmrender-swift
placement methods.
The supplied pen is synthetic: this checks placement, not paragraph/font layout.
"""
import argparse,random,struct,subprocess,tempfile
from pathlib import Path

def method(source,needle):
    start=source.index(needle);pos=source.index('{',start)+1;depth=1
    while depth:
        if source[pos]=='{':depth+=1
        elif source[pos]=='}':depth-=1
        pos+=1
    return source[start:pos]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path);p.add_argument('--rmlines',type=Path,help='RMLines Swift source package (defaults to sibling rmlines-swift)');a=p.parse_args()
    src=a.swift/'Sources/RMRender';ink=(src/'StrokeInk.swift').read_text();geometry=(src/'TextGeometry.swift').read_text()
    swift='import Foundation\nextension Stroke {\n'+method(ink,'func placed(x:')+'\n}\n'
    swift+='struct TextGeometry { let x:Double;let y:Double\nfunc pen(at anchor:TextAnchor)->(x:Double,y:Double){(x,y)}\n'
    swift+=method(geometry,'func placing(_ strokes:')+'\n'+method(geometry,'func placing(_ images:')+'\n}\n'
    swift+='''while let line=readLine() {
 let v=line.split(separator:" ").map { Double($0)! }
 let anchor:TextAnchor?=v[4] != 0 ? TextAnchor(paragraph:0,offset:0) : nil
 let stroke=Stroke(tool:PenTool(rawValue:17),color:PenColor(rawValue:0)!,thicknessScale:1,points:[StrokePoint(x:Float(v[0]),y:Float(v[1]),width:8,pressure:0.5,speed:0)],anchor:anchor)
 let image=PageImage(file:"own.png",vertices:[PageImage.Vertex(x:v[0],y:v[1],u:0,v:0)],strokesBefore:0,anchor:anchor)
 let geometry=TextGeometry(x:v[2],y:v[3]);let s=geometry.placing([stroke])[0];let im=geometry.placing([image])[0]
 precondition(s.anchor == nil && im.anchor == nil)
 print("\\(Double(s.points[0].x)) \\(Double(s.points[0].y)) \\(im.vertices[0].x) \\(im.vertices[0].y)")
}
'''
    rng=random.Random(602);rows=[]
    boundaries=[0.,-0.,1.,-1.,16777215.,16777216.,16777217.,.1,-.1,1e-20,-1e-20,1e10,-1e10]
    for i in range(2000):
        values=[rng.choice(boundaries) if i<500 else rng.uniform(-1e5,1e5) for _ in range(4)]
        rows.append([*values,i%2])
    data=''.join(' '.join(map(str,row))+'\n' for row in rows)
    got=subprocess.run([str(a.oracle.resolve())],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
    with tempfile.TemporaryDirectory(prefix='rmrender-placement-') as temp:
        d=Path(temp);(d/'main.swift').write_text(swift)
        lines=(a.rmlines or a.swift.parent/'rmlines-swift')/'Sources/RMLines'
        types=['Stroke.swift','StrokePoint.swift','TextAnchor.swift','PageImage.swift','SceneItemID.swift','PenTool.swift','PenColor.swift']
        (d/'Crdt.swift').write_text(method((lines/'RMBinaryReader.swift').read_text(),'struct CrdtID:'))
        subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),*[str(lines/f) for f in types],str(d/'Crdt.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
        want=subprocess.run([str(d/'oracle')],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(got)==len(want)==len(rows)
    for i,(g,w) in enumerate(zip(got,want)):
        gb=b''.join(struct.pack('!d',float(x)) for x in g.split());wb=b''.join(struct.pack('!d',float(x)) for x in w.split())
        assert gb==wb,(i,rows[i],g,w)
    print(f'{len(rows)} placement cases match Swift exactly, including signed zero and Float rounding.')
if __name__=='__main__':main()
