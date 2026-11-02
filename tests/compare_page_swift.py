#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of canvas/extent cases against policy functions taken
from rmrender-swift."""
import argparse,math,random,subprocess,tempfile
from pathlib import Path

def block(source,start,opening='{',closing='}'):
    first=source.index(opening,start);depth=1;end=first+1
    while depth:
        if source[end]==opening:depth+=1
        elif source[end]==closing:depth-=1
        end+=1
    return source[start:end]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',type=Path,required=True);p.add_argument('--oracle',type=Path,required=True);a=p.parse_args()
    src=a.swift/'Sources/RMRender';page=(src/'PageRenderer.swift').read_text();notebook=(src/'NotebookRenderer.swift').read_text()
    plausible=block(page,page.index('static func plausibleCanvas('))
    canvas=block(notebook,notebook.index('static func canvas('))
    constants='\n'.join(line.strip() for line in page.splitlines() if 'static let defaultPageSize =' in line or 'static let maximumCanvasSide:' in line)
    start=page.index('extent: CGSize(')+len('extent: ');extent=block(page,start,'(',')')
    swift='import Foundation\nimport CoreGraphics\nenum PageRenderer {\n'+constants+'\n'+plausible+'\n}\nenum NotebookRenderer {\n'+canvas+'\n}\n'
    swift+='''while let line=readLine() {
 let v=line.split(separator:" ").map { Double($0)! }
 let candidates=[v[0] != 0 ? CGSize(width:v[1],height:v[2]) : nil, v[3] != 0 ? CGSize(width:v[4],height:v[5]) : nil]
 let declared=candidates.compactMap { PageRenderer.plausibleCanvas($0) }.first ?? PageRenderer.plausibleCanvas(CGSize(width:v[6],height:v[7]))
 let pageSize=NotebookRenderer.canvas(declared:declared,isLandscape:v[8] != 0)
 let bounds=CGRect(x:v[9]+pageSize.width/2,y:pageSize.height-(v[10]+v[12]),width:v[11],height:v[12])
 let maximumCanvasSide=PageRenderer.maximumCanvasSide
 let extent='''+extent+'''
 print("\\(pageSize.width) \\(pageSize.height) \\(extent.width) \\(extent.height)")
}
'''
    rng=random.Random(601);rows=[]
    sizes=[(0,0),(-1,200),(float('nan'),300),(float('inf'),100),(100001,10),(100000,100000),(1404,1872),(2160,1620),(820,1170),(990.8701741838819,1170.1),(1234.56,789.12),(1,1)]
    def valid(s):return all(math.isfinite(x) and 0<x<=100000 for x in s)
    for i in range(1200):
        one=rng.choice(sizes);two=rng.choice(sizes);content=rng.choice(sizes);ha=rng.randrange(2);hb=rng.randrange(2);land=rng.randrange(2)
        size=next((s for enabled,s in [(ha,one),(hb,two),(True,content)] if enabled and valid(s)),(1404,1872))
        if land and size[1]>size[0]:size=size[::-1]
        left,top,right,bottom=[rng.choice([0,0,0,20,1e6,rng.random()*1000]) for _ in range(4)]
        x=-size[0]/2-left;y=-top;w=size[0]+left+right;h=size[1]+top+bottom
        rows.append([ha,*one,hb,*two,*content,land,x,y,w,h])
    data=''.join(' '.join(map(str,row))+'\n' for row in rows)
    got=subprocess.run([str(a.oracle.resolve())],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
    with tempfile.TemporaryDirectory(prefix='rmrender-page-') as temp:
        d=Path(temp);(d/'main.swift').write_text(swift)
        subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
        want=subprocess.run([str(d/'oracle')],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
    assert len(got)==len(want)==len(rows)
    maximum=0
    for i,(g,w) in enumerate(zip(got,want)):
        delta=max(abs(x-y) for x,y in zip(map(float,g.split()),map(float,w.split())))
        maximum=max(maximum,delta);assert delta<=1e-9,(i,rows[i],g,w)
    print(f'{len(rows)} canvas/extent comparisons passed; maximum difference {maximum:g}.')
if __name__=='__main__':main()
