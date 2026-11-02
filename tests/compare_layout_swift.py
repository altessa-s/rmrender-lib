#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of CoreText paragraph wrapping against rmrender-swift,
using the same open font files."""
import argparse,random,re,subprocess,tempfile
from pathlib import Path

def method(s,n):
 start=s.index(n);end=s.index('{',start)+1;depth=1
 while depth:
  if s[end]=='{':depth+=1
  elif s[end]=='}':depth-=1
  end+=1
 return s[start:end]

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',type=Path,required=True);p.add_argument('--oracle',type=Path,required=True);a=p.parse_args()
 root=a.swift/'Sources/RMRender';lines=a.swift.parent/'rmlines-swift/Sources/RMLines'
 rng=random.Random(700)
 texts=['The quick brown fox jumps over the lazy dog. café 123!','Привет мир. Ελληνικά γράμματα.','مرحبا بالعالم العربية','שלום עולם בעברית','日本語の文章です。漢字とかな。','한국어 문장을 여러 줄로 나눕니다','ภาษาไทย สวัสดี','नमस्ते दुनिया','a📓é xyz','123 שלום world שלום 123','']
 rows=[]
 for i in range(180):
  raw,second,third=rng.choice([(0,0,0),(1,0,0),(2,0,0),(3,0,0),(3,2,3),(4,0,0),(10,0,0),(11,0,0x21)])
  t=texts[i%len(texts)]
  if not t:t='  '
  rows.append(f'{raw} {second} {third} {i%2} {(i//2)%2} {rng.choice([100,250,500,900])} {t}')
 data='\n'.join(rows)+'\n'
 got=subprocess.run([str(a.oracle),str(root/'Fonts')],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
 with tempfile.TemporaryDirectory(prefix='rmr-layout-swift-') as tmp:
  d=Path(tmp);sources=[]
  for name in ['TextBlock','ParagraphFormat','ParagraphStyle','FaceRun','ListFamily','CheckboxState','TextAnchor']:
   source=(lines/(name+'.swift')).read_text();dest=d/(name+'.swift');dest.write_text(source);sources.append(str(dest))
  for name in ['TextGeometry','TypeFaces','ScriptClass','EmbeddedFonts']:
   source=(root/(name+'.swift')).read_text().replace('import RMLines','').replace('import RMMarks','')
   if name=='TextGeometry':
    for n in ['func placing(_ strokes:','func placing(_ images:']: source=source.replace(method(source,n),'')
   dest=d/(name+'.swift');dest.write_text(source);sources.append(str(dest))
  calibration=(root/'Calibration.swift').read_text()
  stretch=calibration[calibration.index('    static let fontStretch:'):calibration.index('// MARK: - List markers')]
  # Close of the original Calibration extension is included in this slice.
  metrics=method(calibration,'public enum TypeMetrics')
  dest=d/'Calibration.swift';dest.write_text('import Foundation\nenum Calibration {\n'+stretch+'\n'+metrics);sources.append(str(dest))
  (d/'main.swift').write_text('''import Foundation
import CoreText
struct FontBundle {func url(forResource n:String,withExtension e:String,subdirectory:String)->URL? {URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(n+"."+e)}}
extension Bundle {static var module:FontBundle {FontBundle()}}
enum RMRenderLog {static func pageError(_ s:String) {}}
enum PageRenderer {static let inkColour=CGColor(red:0,green:0,blue:0,alpha:1)}
while let row=readLine() {
 let t=row.split(separator:" ",maxSplits:6,omittingEmptySubsequences:false);let raw=UInt8(t[0])!,second=UInt8(t[1])!,third=UInt32(t[2])!;let text=String(t[6])
 let f=ParagraphFormat(raw:raw,second:second==0 ? nil:second,third:third==0 ? nil:third)
 let run=FaceRun(range:0..<min(2,text.unicodeScalars.count),bold:t[3]=="1",italic:t[4]=="1")
 let block=TextBlock(originX:-250,originY:100,lines:[text],formats:[0:f],faces:[0:[run]],columnWidth:Double(t[5])!)
 let g=TextGeometry(block,pageSize:CGSize(width:1404,height:1872));let p=g.paragraphs[0]
 var values=[String(p.baseline),String(g.frameBottom!),String(p.lines.count)]
 for line in p.lines {let native=CTTypesetterCreateLine(p.typesetter!,line.range);values += [String(line.range.location),String(line.range.length),String(line.baseline),String(Double(CTLineGetOffsetForStringIndex(native,line.range.location,nil)))]}
 print(values.joined(separator:" "))
}
''')
  subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),*sources,str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
  want=subprocess.run([str(d/'oracle'),str(root/'Fonts')],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
 assert len(got)==len(want)==len(rows),(len(got),len(want))
 failures=[]
 for i,(g,w) in enumerate(zip(got,want)):
  gv=list(map(float,g.split()));wv=list(map(float,w.split()))
  if not (len(gv)==len(wv) and all(abs(x-y)<1e-8 for x,y in zip(gv,wv))): failures.append((i,rows[i],g,w))
 for failure in failures[:8]: print(failure)
 assert not failures,len(failures)
 print(f'{len(rows)} native paragraph layouts match original Swift (1e-8 tolerance).')
if __name__=='__main__':main()
