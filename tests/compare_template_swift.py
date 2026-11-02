#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional cross-check of generated vector templates against the rmrender-swift
schema, expression evaluator and TemplateRenderer.
Only Calibration's two template constants and the logging/error declarations are
supplied by the harness. No shipped templates or device assets are used.
"""
import argparse,base64,json,random,subprocess,tempfile
from pathlib import Path
MAIN=r'''
import Foundation
import CoreGraphics
while let line=readLine() {
 let input=try! JSONSerialization.jsonObject(with:Data(line.utf8)) as! [String:Any]
 let template=try! JSONDecoder().decode(Template.self,from:JSONSerialization.data(withJSONObject:input["template"]!))
 let canvas=input["canvas"] as! [Double],extent=input["extent"] as! [Double]
 let w=Int(extent[0]),h=Int(extent[1]);var bytes=[UInt8](repeating:0,count:w*h*4)
 bytes.withUnsafeMutableBytes { data in
  let cg=CGContext(data:data.baseAddress,width:w,height:h,bitsPerComponent:8,bytesPerRow:w*4,space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue|CGBitmapInfo.byteOrder32Big.rawValue)!
  cg.translateBy(x:0,y:CGFloat(h)-canvas[1])
  TemplateRenderer.draw(template,in:cg,size:CGSize(width:canvas[0],height:canvas[1]),extent:CGSize(width:extent[0],height:extent[1]))
 }
 print(Data(bytes).base64EncodedString())
}
'''
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--swift',required=True,type=Path);p.add_argument('--oracle',required=True,type=Path);p.add_argument('--json',action='store_true',help='Use JSON decoder oracle');p.add_argument('--text',action='store_true',help='Include native template labels (requires --json)');a=p.parse_args();
 if a.text and not a.json:p.error('--text requires --json')
 rng=random.Random(10000)
 cases=[];protocol=[]
 paths=[['M',0,0,'L','parentWidth','parentHeight'],['M',0,0,'L','parentWidth',0,'L','parentWidth','parentHeight','L',0,'parentHeight','Z'],
  ['M',0,'parentHeight / 2','C','parentWidth / 4',0,'parentWidth * .75','parentHeight','parentWidth','parentHeight / 2'],
  ['M',0,'parentHeight','L','parentWidth / 2',0,'L','parentWidth','parentHeight','Z']]
 for i in range(160):
  shape=i%4;nested=(i//4)%2;rk=rng.randrange(4);ck=rng.randrange(4)
  rows=rng.choice([-2,1,2.9,4]);cols=rng.choice([0,1,2.9,3])
  x=rng.randrange(-8,20)/4;y=rng.randrange(-8,40)/4;w=rng.choice([8,12,16,20,32]);h=rng.choice([8,12,16,24,32])
  hf=(i//8)%2;hw=(i//16)%2;hc=(i//32)%2;width=rng.choice([.5,1,2,3.5])
  fc=[rng.choice([0,64,128,255]) for _ in range(4)];sc=[rng.choice([0,64,128,255]) for _ in range(4)]
  cw,ch=rng.choice([(64,64),(64,96),(96,64)]);ew,eh=rng.choice([(cw,ch),(cw+32,ch+32)])
  path=dict(type='path',data=paths[shape]);
  if hf:path['fillColor']='#'+''.join(f'{c:02x}' for c in fc)
  if hc:path['strokeColor']='#'+''.join(f'{c:02x}' for c in sc)
  if hw:path['strokeWidth']='pen'
  child=dict(type='group',boundingBox=dict(x=2,y=3,width='parentWidth / 2',height='parentHeight / 2'),children=[path]) if nested else path
  repeat={}
  if rk:repeat['rows']=rows if rk==1 else 'down' if rk==2 else 'up'
  if ck:repeat['columns']=cols if ck==1 else 'right' if ck==2 else 'left'
  root=dict(type='group',boundingBox=dict(x='offset + paperOriginX',y=y,width='cellWidth',height='cellHeight'),repeat=repeat,children=[child])
  template=dict(name='Own synthetic',constants=[dict(cellWidth=w),dict(cellHeight=h),dict(base=width/2),dict(pen='base * 2'),dict(offset=x)],items=[root])
  if a.json:
   template.update(name='Own Линии 📓',orientation='landscape' if cw>ch else 'portrait',categories=['Own','Lines'])
   if hf and i%3==0:path['fillColor']=rng.choice(['BLACK','white','Gray','grey','lightgray','LightGrey'])
   if hc and i%5==0:path['strokeColor']=rng.choice(['black','WHITE','gray','lightgrey','123456'])
   if rk==2 and i%3==0:repeat['rows']='infinite'
  if a.text:
   labels=['Sunday','WEEK NUMBER:','Линия','café','e\u0301','مرحبا','שלום','日本語','📓','', '   ', 'A\nB']
   text=dict(type='text',text=labels[i%len(labels)],position=dict(x='(parentWidth - textWidth) / 2',y='parentHeight / 2 + 4'))
   if i%4:text['fontSize']=rng.choice([6,8,10,12.5])
   if nested:child['children'].append(text)
   else:root['children'].append(text)
  cases.append(dict(template=template,canvas=[cw,ch],extent=[ew,eh]))
  values=[shape,nested,rk,ck,rows,cols,x,y,w,h,hf,hw,hc,width,*[c/255 for c in fc],*[c/255 for c in sc],cw,ch,ew,eh]
  protocol.append(' '.join(map(str,values)))
 if a.json:protocol=[' '.join(map(str,c['canvas']+c['extent']))+' '+json.dumps(c['template']) for c in cases]
 got=subprocess.run([str(a.oracle.resolve())],input='\n'.join(protocol)+'\n',text=True,capture_output=True,check=True).stdout.splitlines()
 src=a.swift/'Sources/RMRender';cal=(src/'Calibration.swift').read_text()
 constants='\n'.join(line.strip() for line in cal.splitlines() if 'let templateStroke' in line)
 support='import CoreGraphics\nenum Calibration {\n'+constants+'\n}\nenum RenderError: Error { case malformedPage(String), unsupportedFormat(String) }\nenum RMRenderLog { static func templateError(_ text:String) {} }\n'
 with tempfile.TemporaryDirectory(prefix='rmrender-template-') as temp:
  d=Path(temp);(d/'main.swift').write_text(MAIN);(d/'Support.swift').write_text(support)
  args=['swiftc','-O','-module-cache-path',str(d/'cache')]
  for name in ['RMLines','RMMarks']:
   framework=a.swift/f'ThirdParty/{name}/{name}.xcframework/macos-arm64_x86_64'
   args+=['-F',str(framework),'-framework',name,'-Xlinker','-rpath','-Xlinker',str(framework)]
  args += [str(src/f) for f in ['TemplateSchema.swift','TemplateExpression.swift','TemplateRenderer.swift']]+[str(d/'Support.swift'),str(d/'main.swift'),'-o',str(d/'oracle')]
  subprocess.run(args,check=True)
  want=subprocess.run([str(d/'oracle')],input=''.join(json.dumps(c)+'\n' for c in cases),text=True,capture_output=True,check=True).stdout.splitlines()
 assert len(got)==len(want)==len(cases)
 maximum=0;different=0
 for i,(g,w) in enumerate(zip(got,want)):
  x=bytes.fromhex(g);y=base64.b64decode(w);assert len(x)==len(y)==cases[i]['extent'][0]*cases[i]['extent'][1]*4
  delta=[abs(a-b) for a,b in zip(x,y)];peak=max(delta);mean=sum(delta)/len(delta)
  maximum=max(maximum,peak);different+=int(x!=y)
  assert peak<=1 and mean<=.001,(i,peak,mean,cases[i])
 print(f'{len(cases)} vector template comparisons passed: {different} non-identical; maximum channel difference {maximum}.')
if __name__=='__main__':main()
