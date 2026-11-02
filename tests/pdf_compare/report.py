# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Small standalone HTML evidence report and strict equivalence verdict."""
import html
import json
from pathlib import Path
import struct
import zlib


def png(path, data, width=320, height=480):
    def chunk(name, payload):
        return struct.pack('>I', len(payload))+name+payload+struct.pack('>I', zlib.crc32(name+payload))
    rows = b''.join(b'\0'+data[y*width*4:(y+1)*width*4] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
                     +chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND', b''))


def equivalence(rows):
    failures = []
    for row in rows:
        left, right, comparison = ('swift', 'c', 'swift_vs_c') if row['level'] == 'writer' else ('swift-auto', 'c-preserve', 'auto_vs_preserve')
        a, b = row['runs'][left], row['runs'][right]
        reasons = []
        if a['route'] != b['route']: reasons.append('route')
        if a.get('readable') != b.get('readable'): reasons.append('readability')
        if a.get('failureOutput') != b.get('failureOutput'): reasons.append('failure-output')
        if a['ok'] and b['ok']:
            match = row['comparisons'][comparison]
            for key in ['boxesEqual', 'displaySizesEqual', 'rotationsEqual', 'objectsEqual']:
                if not match.get(key): reasons.append(key)
            if any(m['meanAbs'] > 0.005 or m['changedFraction'] > 0.00001 for m in match.get('rasters', [])+match.get('cropRasters', [])):
                reasons.append('raster')
        if reasons: failures.append({'case': row['level']+'/'+row['case'], 'reasons': reasons})
    return {'equivalent': not failures, 'caseCount': len(rows), 'matchingCases': len(rows)-len(failures),
            'differentCases': failures, 'meaning': 'Swift auto vs C strict preserving at document level; route/policy differences count as differences.'}


def write_html(output, report):
    esc = html.escape
    parts = ['<!doctype html><html lang="en"><meta charset="utf-8"><title>RMRender PDF comparison</title>',
             '<style>body{font:15px system-ui;margin:28px;max-width:1200px;color:#222}table{border-collapse:collapse;width:100%}td,th{border:1px solid #bbb;padding:7px;text-align:left}pre{white-space:pre-wrap}summary{cursor:pointer;margin:12px 0}figure{display:inline-block;vertical-align:top;margin:8px}img{border:1px solid #aaa;width:240px;height:360px}small{color:#555}</style>',
             '<h1>RMRender PDF differential evidence</h1>',
             '<p><strong>This is not a full-equivalence pass.</strong> Reviewed expectations record both matches and known differences. Strict equivalence is evaluated separately.</p>',
             '<pre>'+esc(json.dumps(report['equivalence'], indent=2))+'</pre>',
             '<details><summary>Build provenance</summary><pre>'+esc(json.dumps(report['versions'], indent=2))+'</pre></details>',
             '<p>Thumbnails show the first page in MediaBox view. JSON metrics and linked PDFs cover every page; crop-view PNGs are linked separately.</p>',
             '<label for="difference-mode">Difference view: </label><select id="difference-mode"><option value="4">Absolute difference ×4</option><option value="16">Absolute difference ×16</option><option value="64">Absolute difference ×64</option><option value="mask">Changed pixels (any RGB difference)</option></select>',
             '<p>Black means identical RGB values. The mask marks every changed pixel in magenta, including differences within the accepted tolerance. Counts refer to the first MediaBox page only; this display does not change the equivalence verdict.</p>',
             '<p>Own synthetic fixtures. Object comparison is a documented semantic projection, not proof of every PDF feature. Signature dictionaries here are synthetic; cryptographic tests are separate.</p>']
    for row in report['cases']:
        directory = output/row['level']/row['case']
        parts += ['<h2>'+esc(row['level']+'/'+row['case'])+'</h2>', '<table><tr><th>Engine</th><th>Route</th><th>Readable</th><th>Original objects retained</th></tr>']
        for engine, run in row['runs'].items():
            filename = engine+('-second' if row['case'] == 'repeated' else '')+'.pdf'
            path = directory/filename
            label = '<a href="'+esc(str(path.relative_to(output)))+'">'+esc(engine)+'</a>' if run['ok'] else esc(engine)
            parts.append('<tr><td>'+label+'</td><td>'+esc(run['route'])+'</td><td>'+str(run.get('readable', '—'))+'</td><td>'+str(run.get('preservesObjects', '—'))+'</td></tr>')
        parts.append('</table>')
        for name, comparison in row['comparisons'].items():
            parts.append('<details><summary>'+esc(name)+'</summary><pre>'+esc(json.dumps(comparison, indent=2))+'</pre>')
            if row['level'] == 'writer':
                engines = name.split('_vs_')
            else:
                engines = {'auto_vs_preserve': ['swift-auto','c-preserve'], 'redraw_vs_redraw': ['swift-redraw','c-redraw'],
                           'swift_auto_vs_redraw': ['swift-auto','swift-redraw'], 'c_preserve_vs_redraw': ['c-preserve','c-redraw']}[name]
            image_sets = []
            for engine in engines:
                suffix = '-second' if row['case'] == 'repeated' and engine != 'expected' else ''
                folder = directory/(engine+suffix+'.inspect')
                source = folder/'page-0.rgba'
                if not source.exists(): continue
                target = folder/'page-0.png'; data = source.read_bytes(); png(target, data)
                image_sets.append(data)
                parts.append('<figure><figcaption>'+esc(engine)+' · MediaBox</figcaption><img loading="lazy" src="'+esc(str(target.relative_to(output)))+'"></figure>')
                crop = folder/'page-0-crop.rgba'
                if crop.exists():
                    png(folder/'page-0-crop.png', crop.read_bytes())
                    parts.append('<p><a href="'+esc(str((folder/'page-0-crop.png').relative_to(output)))+'">'+esc(engine)+' crop view</a></p>')
            if len(image_sets) == 2:
                a, b = image_sets
                assert len(a) == len(b), 'raster dimension mismatch'
                delta = [0 if i % 4 == 3 else abs(x-y) for i, (x,y) in enumerate(zip(a,b))]
                changed = sum(any(delta[i:i+3]) for i in range(0, len(delta), 4))
                paths = {}
                for mode in [4, 16, 64, 'mask']:
                    diff = bytearray(len(a))
                    for i in range(0, len(a), 4):
                        rgb = delta[i:i+3]
                        diff[i:i+4] = bytes(([255, 0, 255] if any(rgb) else [0, 0, 0]) + [255]) if mode == 'mask' else bytes([min(255, mode*v) for v in rgb] + [255])
                    target = directory/(name+('-diff.png' if mode == 4 else '-diff-'+str(mode)+'.png'))
                    png(target, diff)
                    paths[str(mode)] = esc(str(target.relative_to(output)))
                attributes = ' '.join('data-mode-'+mode+'="'+path+'"' for mode,path in paths.items())
                parts.append('<figure><figcaption class="difference-caption">Absolute difference ×4</figcaption><img class="difference" loading="lazy" '+attributes+' src="'+paths['4']+'" alt="Pixel difference"><p>'+str(changed)+' / '+str(len(a)//4)+' pixels changed; maximum RGB difference '+str(max(delta, default=0))+'/255.</p></figure>')
            parts.append('</details>')
    parts.append("""<script>
const control = document.getElementById('difference-mode');
function updateDifferenceView() {
    const mode = control.value;
    document.querySelectorAll('img.difference').forEach(image => {
        image.src = image.getAttribute('data-mode-' + mode);
    });
    document.querySelectorAll('.difference-caption').forEach(caption => {
        caption.textContent = control.options[control.selectedIndex].text;
    });
}
control.addEventListener('change', updateDifferenceView);
updateDifferenceView();
</script></html>""")
    (output/'report.html').write_text('\n'.join(parts))
