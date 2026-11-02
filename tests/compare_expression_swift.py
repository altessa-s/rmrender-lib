#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of generated ASCII expressions against rmrender-swift
TemplateExpression.swift."""
import argparse,math,random,subprocess,tempfile
from pathlib import Path
MAIN=r'''
import Foundation
let environment:[String:Double] = ["x":7,"y":-3,"templateWidth":1404,"templateHeight":1872,
 "parentWidth":640,"parentHeight":320,"zero":0,"nan":Double.nan,"inf":Double.infinity]
while let line=readLine() {
 do { print("ok \(try TemplateExpression(line).value(in:environment))") }
 catch { print("error") }
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path);a=p.parse_args()
 rng=random.Random(65536)
 cases=['templateWidth < 900 || templateHeight < 900 ? 40 : 80','0 ? 1 : 1 ? 2 : 3',
  '1 ? 0 ? 2 : 3 : 4','1 / zero','-1 / -zero','1 ? 2 : missing','0 && missing','1 || missing',
  'nan == nan','nan != nan','nan ? 3 : 4','inf - inf','inf / inf','inf * zero','1.25 + .5',
  '1.','00.5','2 < 3 < 4','1e3','1..2','!1','','(1 + 2','1 ? 2','1 2','1 &&','a(2)', '9'*500]
 def expression(depth):
  if not depth or rng.randrange(4)==0:return rng.choice(['x','y','templateWidth','parentHeight','zero',str(rng.randrange(10000)/100)])
  kind=rng.randrange(4)
  if kind==0:return rng.choice(['+','-'])+'('+expression(depth-1)+')'
  if kind==1:return '('+expression(depth-1)+' ? '+expression(depth-1)+' : '+expression(depth-1)+')'
  return '('+expression(depth-1)+rng.choice(['+','-','*','/','<=','>=','<','>','==','!=','&&','||'])+expression(depth-1)+')'
 cases += [expression(4) for _ in range(3000)]
 # Deterministic malformed token/parenthesis/operator mutations, not just valid syntax.
 for _ in range(500):
  s=expression(3);at=rng.randrange(len(s)+1);cases.append(s[:at]+rng.choice(['@',')','(',':','?','!',' & ',' 2 '])+s[at:])
 payload='\n'.join(cases)+'\n'
 got=subprocess.run([str(a.oracle.resolve())],input=payload,text=True,capture_output=True,check=True).stdout.splitlines()
 with tempfile.TemporaryDirectory(prefix='rmrender-expression-') as temp:
  d=Path(temp);(d/'main.swift').write_text(MAIN)
  (d/'Support.swift').write_text('enum RenderError: Error { case malformedPage(String) }\nenum TemplateValue { case number(Double), expression(String) }\n')
  framework=a.swift/'ThirdParty/RMLines/RMLines.xcframework/macos-arm64_x86_64'
  subprocess.run(['swiftc','-O','-module-cache-path',str(d/'cache'),'-F',str(framework),'-framework','RMLines','-Xlinker','-rpath','-Xlinker',str(framework),str(a.swift/'Sources/RMRender/TemplateExpression.swift'),str(d/'Support.swift'),str(d/'main.swift'),'-o',str(d/'oracle')],check=True)
  want=subprocess.run([str(d/'oracle')],input=payload,text=True,capture_output=True,check=True).stdout.splitlines()
 assert len(got)==len(want)==len(cases)
 errors=0
 for i,(g,w) in enumerate(zip(got,want)):
  if g=='error' or w=='error':assert g==w,(i,cases[i],g,w);errors+=1;continue
  x=float(g[3:]);y=float(w[3:]);assert x==y or (math.isnan(x) and math.isnan(y)),(i,cases[i],g,w)
  if x==0:assert math.copysign(1,x)==math.copysign(1,y),(i,cases[i],g,w)
 print(f'{len(cases)} expression comparisons passed exactly, including {errors} matching errors.')
if __name__=='__main__':main()
