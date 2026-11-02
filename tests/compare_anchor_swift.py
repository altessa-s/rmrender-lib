#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of the anchor policy against rmrender-swift, using
synthetic shaped-line caret metrics.
Native shaping/wrapping is deliberately outside this comparison.
"""
import argparse,random,subprocess,tempfile
from pathlib import Path

def method(source,needle):
 start=source.index(needle);pos=source.index('{',start)+1;depth=1
 while depth:
  if source[pos]=='{':depth+=1
  elif source[pos]=='}':depth-=1
  pos+=1
 return source[start:pos]

def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path);p.add_argument('--rmlines',type=Path);a=p.parse_args()
 source=(a.swift/'Sources/RMRender/TextGeometry.swift').read_text()
 swift='''import Foundation
import CoreFoundation
struct Typesetter { let rtl:Bool }
struct CTLine { let range:CFRange;let rtl:Bool }
func CTTypesetterCreateLine(_ t:Typesetter,_ range:CFRange)->CTLine { CTLine(range:range,rtl:t.rtl) }
func CTLineGetOffsetForStringIndex(_ line:CTLine,_ index:Int,_ secondary:UnsafeMutablePointer<Double>?)->Double { (line.rtl ? 100 : 10) + Double(index)*(line.rtl ? -2.5 : 2.5) }
func CTLineGetStringRange(_ line:CTLine)->CFRange { line.range }
struct TextGeometry {
 struct Line {let range:CFRange;let baseline:Double}
 struct Paragraph {let text:String;let typesetter:Typesetter?;let lines:[Line];let indent:Double;let baseline:Double;let isRightToLeft:Bool}
 let paragraphs:[Paragraph];let left:Double;let column:Double
'''
 swift+='\n'.join(method(source,n) for n in ['func pen(at anchor:','func margin(of paragraph:','private func paragraph(_ number:'])+'\n}\n'
 swift+='''while let line=readLine() {
 let tokens=line.split(separator:" ");let v=tokens.prefix(12).map { Double($0)! };let hex=String(tokens[12])
 var bytes:[UInt8]=[]
 if hex != "-" { var i=hex.startIndex;while i<hex.endIndex {let end=hex.index(i,offsetBy:2);bytes.append(UInt8(hex[i..<end],radix:16)!);i=end} }
 let text=String(bytes:bytes,encoding:.utf8)!;let rtl=v[4] != 0
 let lines=[TextGeometry.Line(range:CFRange(location:0,length:Int(v[10])),baseline:v[9]),TextGeometry.Line(range:CFRange(location:Int(v[10]),length:Int(v[11]-v[10])),baseline:v[9]+30)]
 let paragraph=TextGeometry.Paragraph(text:text,typesetter:v[5] != 0 ? Typesetter(rtl:rtl):nil,lines:lines,indent:v[8],baseline:v[9],isRightToLeft:rtl)
 let g=TextGeometry(paragraphs:[paragraph],left:v[6],column:v[7])
 let anchor=TextAnchor(paragraph:Int(v[0]),offset:Int(v[1]),originX:v[2] != 0 ? v[3]:nil)
 let pen=g.pen(at:anchor);print("\\(pen.x) \\(pen.y)")
}
'''
 rng=random.Random(603);rows=[]
 texts=['','abc','a📓\u0301b','مرحبا  ','אבג  ','日本語','e\u0301 cafe\u0301','👩\u200d💻x','a\nb','\x00x']
 for i in range(1600):
  text=texts[i%len(texts)];split=rng.randrange(len(text)+1)
  utf16=lambda s:len(s.encode('utf-16-le'))//2
  row=[rng.choice([-2147483648,-1,0,1,2147483647]),rng.choice([-2147483648,-1,0,1,2,3,4,len(text)+1,2147483647]),i%3==0,rng.uniform(-100,100),i%2,i%5!=0,rng.uniform(-500,0),rng.uniform(100,800),rng.uniform(0,50),rng.uniform(0,1000),utf16(text[:split]),utf16(text),text.encode().hex() or '-']
  rows.append(' '.join(str(int(x)) if isinstance(x,bool) else str(x) for x in row))
 data='\n'.join(rows)+'\n'
 got=subprocess.run([str(a.oracle.resolve())],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
 with tempfile.TemporaryDirectory(prefix='rmrender-anchor-') as temp:
  d=Path(temp);(d/'main.swift').write_text(swift)
  anchor=(a.rmlines or a.swift.parent/'rmlines-swift')/'Sources/RMLines/TextAnchor.swift'
  subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),str(anchor),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
  want=subprocess.run([str(d/'oracle')],input=data,text=True,capture_output=True,check=True).stdout.splitlines()
 assert len(got)==len(want)==len(rows)
 for i,(g,w) in enumerate(zip(got,want)):assert list(map(float,g.split()))==list(map(float,w.split())),(i,rows[i],g,w)
 print(f'{len(rows)} anchor-policy cases match Swift exactly with synthetic caret metrics.')
if __name__=='__main__':main()
