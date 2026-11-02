#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Optional PDF cross-check against the rmrender-swift sources (macOS).
Build pdf_compare_oracle with Apple/RMLines/RMDoc enabled. All PDFs/.rm files are
our generated fixtures. Artifacts/report remain in --output for investigation.
No PDF parser, renderer or routing code is substituted in the Swift sources.
"""
import argparse
import hashlib
import json
import os
import platform
import datetime
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).parent/'pdf_compare'))
from fixtures import INK, overlay, source
from report import equivalence, write_html

DOC = '11111111-1111-1111-1111-111111111111'
IDS = ['22222222-2222-2222-2222-222222222222', '33333333-3333-3333-3333-333333333333',
       '44444444-4444-4444-4444-444444444444']
WRITER_CASES = ['basic', 'content_array', 'indirect_length', 'no_content', 'null_content',
                'escaped_key', 'generation', 'rotated', 'cropped', 'origin', 'resource_collision',
                'previous_revision', 'forward_prev', 'freed_content', 'wrong_generation',
                'xref_stream', 'hybrid', 'encrypt_marker', 'certified', 'userunit',
                'duplicate_key', 'page_cycle', 'prev_cycle', 'truncated',
                'cropped_expand', 'userunit_one', 'userunit_zero', 'userunit_null',
                'rotated_180', 'rotated_270', 'rotated_negative', 'rotated_origin', 'xref_flate', 'xref_predictor', 'object_stream', 'hybrid_object_stream', 'xref_truncated', 'bad_predictor', 'bad_object_index']
DOCUMENT_CASES = [(kind, kind, 'plain', 'normal', 0) for kind in ['xref_flate', 'xref_predictor', 'object_stream', 'hybrid_object_stream']] + [
    ('plain', 'basic', 'plain', 'normal', 0),
    ('blank', 'basic', 'blank', 'normal', 0),
    ('pencil', 'basic', 'pencil', 'normal', 0),
    ('stamp', 'basic', 'stamp', 'normal', 0),
    ('bristle', 'basic', 'bristle', 'normal', 0),
    ('streak', 'basic', 'streak', 'normal', 0),
    ('scratch', 'basic', 'scratch', 'normal', 0),
    ('marker', 'basic', 'marker', 'normal', 0),
    ('overflow', 'basic', 'overflow', 'normal', 0),
    ('typed_text', 'basic', 'text', 'normal', 0),
    ('text_rotated', 'rotated', 'text', 'normal', 0),
    ('image_origin', 'origin', 'image', 'normal', 0),
    ('eraser_rotated', 'rotated', 'eraser', 'normal', 0),
    ('cropped_image', 'cropped', 'image', 'normal', 0),
    ('image', 'basic', 'image', 'normal', 0),
    ('eraser', 'basic', 'eraser', 'normal', 0),
    ('rotated', 'rotated', 'plain', 'normal', 0),
    ('rotated_180', 'rotated_180', 'plain', 'normal', 0),
    ('rotated_270', 'rotated_270', 'plain', 'normal', 0),
    ('rotated_negative', 'rotated_negative', 'plain', 'normal', 0),
    ('rotated_origin', 'rotated_origin', 'plain', 'normal', 0),
    ('rotated_overflow', 'rotated', 'overflow', 'normal', 0),
    ('cropped_rotated_overflow', 'cropped_rotated', 'overflow', 'normal', 0),
    ('cropped', 'cropped', 'plain', 'normal', 0),
    ('cropped_overflow', 'cropped', 'overflow', 'normal', 0),
    ('cropped_rotated', 'cropped_rotated', 'plain', 'normal', 0),
    ('cropped_origin', 'cropped_origin', 'plain', 'normal', 0),
    ('cropped_inherited', 'cropped_inherited', 'plain', 'normal', 0),
    ('cropped_text', 'cropped', 'text', 'normal', 0),
    ('cropped_hidden', 'cropped', 'plain', 'hide', 0),
    ('userunit', 'userunit', 'plain', 'normal', 0),
    ('userunit_one', 'userunit_one', 'plain', 'normal', 0),
    ('userunit_zero', 'userunit_zero', 'plain', 'normal', 0),
    ('userunit_null', 'userunit_null', 'plain', 'normal', 0),
    ('origin', 'origin', 'plain', 'normal', 0),
    ('generation', 'generation', 'plain', 'normal', 0),
    ('xref_stream', 'xref_stream', 'plain', 'normal', 0),
    ('hybrid', 'hybrid', 'plain', 'normal', 0),
    ('indirect_length', 'indirect_length', 'plain', 'normal', 0),
    ('certified', 'certified', 'plain', 'normal', 0),
    ('reordered', 'basic', 'plain', 'reverse', 0),
    ('inserted', 'basic', 'plain', 'insert', 0),
    ('removed', 'basic', 'plain', 'remove', 0),
    ('page_cap', 'basic', 'plain', 'normal', 1),
    ('hidden_source', 'basic', 'plain', 'hide', 0),
    ('missing_image', 'basic', 'image', 'missing-image', 0),
    ('corrupt_image', 'basic', 'image', 'corrupt-image', 0),
    ('own_template', 'basic', 'plain', 'template', 0),
]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def execute(command, env=None, timeout=90):
    return subprocess.run([str(x) for x in command], env=env, capture_output=True, text=True, timeout=timeout)


def json_run(command, log, env=None):
    completed = execute(command, env)
    log.write_text(completed.stdout+'\nSTDERR\n'+completed.stderr)
    if completed.returncode:
        raise RuntimeError(f'oracle crashed/exited {completed.returncode}: {log}')
    lines = [line for line in completed.stdout.splitlines() if line.startswith('{')]
    if not lines:
        raise RuntimeError(f'oracle did not return JSON: {log}')
    return json.loads(lines[-1])


def build_reference(args, output):
    root = args.swift/'Sources/RMRender'
    harness = Path(__file__).parent/'pdf_compare/Oracle.swift'
    sources = sorted(root.glob('*.swift'))
    compiler = execute(['xcrun', 'swiftc', '--version']).stdout.strip()
    fingerprint = hashlib.sha256((compiler+''.join(str(p)+sha(p) for p in [*sources, harness])
                                 +sha(args.rmlines_framework/'RMLines.framework/RMLines')
                                 +sha(args.rmmarks_framework/'RMMarks.framework/RMMarks')).encode()).hexdigest()
    binary = output/'swift-oracle'
    stamp = output/'reference.sha256'
    if not binary.exists() or not stamp.exists() or stamp.read_text() != fingerprint:
        cmd = ['xcrun', 'swiftc', '-parse-as-library', '-module-name', 'RMRenderPDFOracle', '-swift-version', '6', '-O',
               '-module-cache-path', output/'module-cache', '-o', binary, *sources, harness]
        for path, name in [(args.rmlines_framework, 'RMLines'), (args.rmmarks_framework, 'RMMarks')]:
            cmd += ['-F', path, '-framework', name, '-Xlinker', '-rpath', '-Xlinker', path]
        completed = execute(cmd, timeout=300)
        (output/'swift-build.log').write_text(completed.stdout+completed.stderr)
        if completed.returncode:
            raise RuntimeError(f'Swift build failed: {output/"swift-build.log"}')
        stamp.write_text(fingerprint)
    resource_hash = hashlib.sha256(''.join(str(p.relative_to(root))+sha(p) for folder in ['Fonts','Templates','Grain'] for p in sorted((root/folder).rglob('*')) if p.is_file()).encode()).hexdigest()
    bundle = output/('Reference-'+resource_hash[:16]+'.bundle')
    bundle.mkdir(exist_ok=True)
    (bundle/'Info.plist').write_bytes(plistlib.dumps({'CFBundleIdentifier': 'test.rmrender.reference', 'CFBundleName': 'Reference'}))
    for folder in ['Fonts', 'Templates', 'Grain']:
        dest = bundle/folder
        if not dest.exists(): shutil.copytree(root/folder, dest)
    env = dict(os.environ, RMR_REFERENCE_BUNDLE=str(bundle))
    return binary, env, {'swiftCompiler': compiler, 'referenceSHA256': fingerprint,
                         'resourceSHA256': resource_hash, 'macOS': platform.mac_ver()[0], 'architecture': platform.machine(),
                         'swiftCommit': execute(['git', '-C', args.swift, 'rev-parse', 'HEAD']).stdout.strip(),
                         'cOracleSHA256': sha(args.oracle), 'rmmarksFrameworkSHA256': sha(args.rmmarks_framework/'RMMarks.framework/RMMarks'), 'rmlinesFrameworkSHA256': sha(args.rmlines_framework/'RMLines.framework/RMLines')}


def snapshot(binary, path, env):
    directory = path.with_suffix('.inspect')
    result = json_run([binary, 'inspect', path, directory], path.with_suffix('.inspect.log'), env)
    directory.mkdir(exist_ok=True)
    (directory/'semantic.json').write_text(json.dumps(result, indent=2, ensure_ascii=False))
    return result


def projection(snap):
    return {**{key: snap.get(key) for key in ['AcroForm', 'Names', 'Outlines', 'Metadata', 'Custom', 'Perms']},
            'pages': [{key: page.get(key) for key in ['annotations', 'custom']} for page in snap.get('pages', [])]}


def compare(left, right, left_path, right_path):
    if not left.get('ok') or not right.get('ok'):
        return {'readable': False}
    lp, rp = left['pages'], right['pages']
    box_delta = max([abs(a-b) for x,y in zip(lp,rp) for key in ['media','crop','art'] for a,b in zip(x[key],y[key])], default=0)
    equal_boxes = len(lp) == len(rp) and all(all(abs(a-b) < 1e-6 for a, b in zip(x[key], y[key]))
                        for x, y in zip(lp, rp) for key in ['media', 'crop', 'art'])
    metrics = []
    for i in range(min(len(lp), len(rp))):
        a = (left_path.with_suffix('.inspect')/f'page-{i}.rgba').read_bytes()
        b = (right_path.with_suffix('.inspect')/f'page-{i}.rgba').read_bytes()
        if len(a) != len(b): raise RuntimeError('raster dimension mismatch')
        delta = [abs(x-y) for k, (x, y) in enumerate(zip(a, b)) if k % 4 != 3]
        metrics.append({'meanAbs': round(sum(delta)/len(delta), 6), 'max': max(delta),
                        'changedFraction': round(sum(v > 8 for v in delta)/len(delta), 6)})
    crop_metrics = []
    for i in range(min(len(lp), len(rp))):
        a = (left_path.with_suffix('.inspect')/f'page-{i}-crop.rgba').read_bytes()
        b = (right_path.with_suffix('.inspect')/f'page-{i}-crop.rgba').read_bytes()
        delta = [abs(x-y) for k, (x, y) in enumerate(zip(a, b)) if k % 4 != 3]
        crop_metrics.append({'meanAbs': round(sum(delta)/len(delta), 6), 'max': max(delta),
                             'changedFraction': round(sum(v > 8 for v in delta)/len(delta), 6)})
    display_equal = len(lp) == len(rp) and all(all(abs(a-b) < 1e-6 for a,b in zip(x['displaySize'],y['displaySize'])) for x,y in zip(lp,rp))
    return {'boxMaxAbs': round(box_delta, 6), 'displaySizesEqual': display_equal, 'cropRasters': crop_metrics, 'readable': True, 'pageCounts': [len(lp), len(rp)], 'boxesEqual': equal_boxes,
            'rotationsEqual': [p['rotation'] for p in lp] == [p['rotation'] for p in rp],
            'objectsEqual': projection(left) == projection(right), 'rasters': metrics}


def run_output(command, path, original, binary, env):
    path.write_bytes(b'comparison sentinel\n')
    status = json_run(command, path.with_suffix('.run.log'), env)
    if status['ok']:
        data = path.read_bytes()
        status['route'] = 'copy' if data == original else 'incremental' if data.startswith(original) else 'redraw'
        status['snapshot'] = snapshot(binary, path, env)
        status['size'] = len(data)
        status['readable'] = status['snapshot'].get('ok', False)
        status['lastPage3Generation'] = None
        import re
        matches = list(re.finditer(rb'(?:^|\n)3 (\d+) obj\b', data))
        if matches: status['lastPage3Generation'] = int(matches[-1][1])
    else:
        status['route'] = 'error'
        status['failureOutput'] = 'absent' if not path.exists() else 'unchanged' if path.read_bytes() == b'comparison sentinel\n' else 'modified'
    return status


def pair(row, name, left, right, paths):
    if row['runs'][left]['ok'] and row['runs'][right]['ok']:
        row['comparisons'][name] = compare(row['runs'][left]['snapshot'], row['runs'][right]['snapshot'], paths[left], paths[right])


def writer_cases(args, binary, env, output):
    rows = []
    for kind in [*WRITER_CASES, 'expand', 'repeated']:
        directory = output/'writer'/kind; directory.mkdir(parents=True, exist_ok=True)
        original = source('basic' if kind in ['expand', 'repeated'] else kind)
        input = directory/'source.pdf'; input.write_bytes(original)
        ink = directory/'ink.txt'; ink.write_bytes(INK)
        form = directory/'overlay.pdf'; form.write_bytes(overlay())
        paths = {name: directory/f'{name}.pdf' for name in ['swift', 'c']}
        row = {'level': 'writer', 'case': kind, 'runs': {}, 'comparisons': {}}
        for name in paths:
            cmd = [binary if name == 'swift' else args.oracle, 'writer', input, ink if name == 'swift' else form,
                   paths[name], 'expand' if kind in ['expand', 'cropped_expand'] else 'normal']
            row['runs'][name] = run_output(cmd, paths[name], original, binary, env)
            if kind == 'repeated' and row['runs'][name]['ok']:
                first = paths[name]; second = directory/f'{name}-second.pdf'
                cmd[2] = first; cmd[4] = second
                row['runs'][name] = run_output(cmd, second, first.read_bytes(), binary, env); paths[name] = second
        pair(row, 'swift_vs_c', 'swift', 'c', paths)
        if kind in ['basic', 'content_array', 'indirect_length', 'no_content', 'null_content', 'escaped_key', 'generation', 'rotated', 'cropped', 'origin', 'resource_collision', 'previous_revision', 'forward_prev', 'expand', 'repeated', 'cropped_expand', 'userunit_one', 'rotated_180', 'rotated_270', 'rotated_negative', 'rotated_origin', 'xref_stream', 'hybrid', 'xref_flate', 'xref_predictor', 'object_stream', 'hybrid_object_stream']:
            expected_kind = 'basic' if kind in ['expand', 'repeated'] else kind
            expected_path = directory/'expected.pdf'
            expected_path.write_bytes(source(expected_kind, expected_marks=2 if kind == 'repeated' else 1, expand=kind in ['expand', 'cropped_expand']))
            expected = snapshot(binary, expected_path, env)
            for engine in ['swift', 'c']:
                if row['runs'][engine]['ok']:
                    row['comparisons'][engine+'_vs_expected'] = compare(row['runs'][engine]['snapshot'], expected, paths[engine], expected_path)
        if kind not in ['encrypt_marker', 'page_cycle', 'prev_cycle', 'truncated', 'freed_content', 'wrong_generation', 'xref_truncated', 'bad_predictor', 'bad_object_index']:
            before = snapshot(binary, input, env)
            for result in row['runs'].values():
                if result['ok']: result['preservesObjects'] = projection(before) == projection(result['snapshot'])
        rows.append(row)
        print('writer', kind, {k: v['route'] for k, v in row['runs'].items()}, flush=True)
    return rows


def document_cases(args, binary, env, output):
    rows = []
    for name, pdf_kind, marks, order, cap in DOCUMENT_CASES:
        directory = output/'document'/name; directory.mkdir(parents=True, exist_ok=True)
        original = source(pdf_kind)
        (directory/f'{DOC}.pdf').write_bytes(original)
        redirections = [0, 1]
        if order == 'reverse': redirections = [1, 0]
        if order == 'insert': redirections = [0, None, 1]
        if order == 'remove': redirections = [0]
        pages = []
        names = [f'{DOC}.pdf', f'{DOC}.content']
        for index, redir in enumerate(redirections):
            record = {'id': IDS[index], 'template': {'value': 'P Grid small' if order == 'template' else 'Blank'}}
            if redir is not None: record['redir'] = {'value': redir}
            pages.append(record)
            path = directory/DOC/f'{IDS[index]}.rm'; path.parent.mkdir(exist_ok=True)
            result = execute([args.oracle, 'page', marks if index == 0 else 'blank', path])
            if result.returncode: raise RuntimeError(result.stderr)
            if order == 'hide' and index == 0:
                import struct
                payload = b'\x3c'+struct.pack('<I', 5)+b'\x1f\x00\x00\x21\x00'
                path.write_bytes(path.read_bytes()+struct.pack('<I4B',len(payload),0,0,1,0x0d)+payload)
            names.append(f'{DOC}/{IDS[index]}.rm')
            if marks == 'image' and index == 0:
                attachment = directory/DOC/IDS[index]/'own.png'; attachment.parent.mkdir(exist_ok=True)
                result = execute([args.oracle, 'image', attachment])
                if result.returncode: raise RuntimeError(result.stderr)
                if order == 'corrupt-image': attachment.write_bytes(b'not a PNG')
                if order != 'missing-image': names.append(f'{DOC}/{IDS[index]}/own.png')
        (directory/f'{DOC}.content').write_text(json.dumps({'cPages': {'pages': pages}}))
        (directory/'manifest.txt').write_text('\n'.join(names)+'\n')
        runs = ['swift-auto', 'swift-redraw', 'c-preserve', 'c-redraw']
        paths = {run: directory/f'{run}.pdf' for run in runs}
        row = {'level': 'document', 'case': name, 'runs': {}, 'comparisons': {}}
        for run in runs:
            if run.startswith('swift'):
                cmd = [binary, 'document', directory, paths[run], 'auto' if run == 'swift-auto' else 'redraw', DOC, cap]
            else:
                cmd = [args.oracle, 'document', directory, paths[run], args.fonts,
                       'preserve' if run == 'c-preserve' else 'redraw', DOC, cap]
            row['runs'][run] = run_output(cmd, paths[run], original, binary, env)
        pair(row, 'auto_vs_preserve', 'swift-auto', 'c-preserve', paths)
        pair(row, 'redraw_vs_redraw', 'swift-redraw', 'c-redraw', paths)
        pair(row, 'swift_auto_vs_redraw', 'swift-auto', 'swift-redraw', paths)
        pair(row, 'c_preserve_vs_redraw', 'c-preserve', 'c-redraw', paths)
        before = snapshot(binary, directory/f'{DOC}.pdf', env)
        for result in row['runs'].values():
            if result['ok']: result['preservesObjects'] = projection(before) == projection(result['snapshot'])
        rows.append(row)
        print('document', name, {k: v['route'] for k, v in row['runs'].items()}, flush=True)
    return rows


def contract(rows):
    result = {}
    for row in rows:
        key = row['level']+'/'+row['case']
        result[key] = {'runs': {name: {k: v[k] for k in ['ok', 'status', 'route', 'readable', 'preservesObjects', 'lastPage3Generation', 'failureOutput'] if k in v}
                               for name, v in row['runs'].items()},
                       'comparisons': {name: {k: value[k] for k in ['readable', 'pageCounts', 'boxesEqual', 'displaySizesEqual', 'rotationsEqual', 'objectsEqual'] if k in value}
                                       for name, value in row['comparisons'].items()}}
    return result


def verify_document_policy(rows):
    """Independent fixture invariants, not inferred from the other renderer."""
    for row in rows:
        for engine, run in row["runs"].items():
            assert not run["ok"] or run.get("readable"), (row["level"], row["case"], engine, "unreadable successful output")
        if row['level'] != 'document':
            continue
        name = row['case']
        if name in ['userunit', 'userunit_zero', 'userunit_null', 'corrupt_image']:
            assert all(not run['ok'] for run in row['runs'].values()), name
        if name in ['userunit_one', 'missing_image']:
            assert all(run['ok'] for run in row['runs'].values()), name
        if 'rotated' in name:
            original_rotation = {'rotated_180': 180, 'rotated_270': 270, 'rotated_negative': -90}.get(name, 90)
            sizes = []
            for engine, run in row['runs'].items():
                assert run['ok'], (name, engine)
                page = run['snapshot']['pages'][0]
                preserving = run['route'] == 'incremental'
                assert page['rotation'] == (original_rotation if preserving else 0), (name, engine)
                sizes.append(page['displaySize'])
                if 'overflow' in name:
                    expected_art = [0, 0, 200, 300] if preserving else [0, 0, 300, 200]
                    if engine == 'c-redraw':
                        # Fresh C shifts the whole display frame to zero; the original
                        # sheet stays the same size in its translated ArtBox.
                        expected_art[:2] = page['art'][:2]
                        assert page['media'][:2] == [0, 0], (name, engine)
                    assert max(abs(a-b) for a,b in zip(page['art'], expected_art)) <= .0001, (name, engine)
            assert all(max(abs(a-b) for a,b in zip(size, sizes[0])) <= .0001 for size in sizes), name
        if not name.startswith('cropped'):
            continue
        for engine, run in row['runs'].items():
            if not run['ok']:
                continue
            page = run['snapshot']['pages'][0]
            crop = page['crop']
            if name == 'cropped_hidden':
                expected = page['media']
            elif name == 'cropped_overflow' and engine == 'c-redraw':
                expected = [page['art'][0]+10, page['art'][1]+15, 180, 270]
            elif name == 'cropped_rotated_overflow' and engine == 'c-redraw':
                expected = [page['art'][0]+15, page['art'][1]+10, 270, 180]
            elif name in ['cropped_rotated', 'cropped_rotated_overflow'] and run['route'] == 'redraw':
                expected = [15, 10, 270, 180]
            elif name == 'cropped_origin' and run['route'] == 'incremental':
                expected = [30, 45, 180, 270]
            else:
                expected = [10, 15, 180, 270]
            assert max(abs(a-b) for a,b in zip(crop, expected)) <= .0001, (name,engine,crop,expected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--swift', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    parser.add_argument('--fonts', type=Path, default=Path(__file__).resolve().parents[1]/'resources/fonts')
    parser.add_argument('--rmlines-framework', type=Path, required=True)
    parser.add_argument('--rmmarks-framework', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-equivalence', action='store_true', help='Fail if any tested route/structure/geometry/raster differs, including intentional contract differences.')
    parser.add_argument('--discover', action='store_true', help='Collect evidence; not a passing compatibility test.')
    args = parser.parse_args()
    args.output = args.output.resolve(); args.output.mkdir(parents=True, exist_ok=True)
    binary, env, versions = build_reference(args, args.output)
    rows = writer_cases(args, binary, env, args.output)+document_cases(args, binary, env, args.output)
    verify_document_policy(rows)
    report = {'generatedUTC': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'versions': versions, 'equivalence': equivalence(rows), 'cases': rows, 'note': 'Synthetic signature dictionaries here do not assert cryptographic validity; use verify_pdf_signature.py separately.'}
    (args.output/'report.json').write_text(json.dumps(report, indent=2, ensure_ascii=False))
    write_html(args.output, report)
    observed = contract(rows)
    (args.output/'observed-contract.json').write_text(json.dumps(observed, indent=2))
    if not args.discover:
        expectations = json.loads((Path(__file__).parent/'pdf_compare/expectations.json').read_text())
        if observed != expectations['contract']:
            changed = [key for key in sorted(set(observed)|set(expectations['contract'])) if observed.get(key) != expectations['contract'].get(key)]
            raise AssertionError('Changed PDF behavior (review report; do not blindly update expectations): '+', '.join(changed))
        failures = []
        for row in rows:
            for name, comparison in row['comparisons'].items():
                key = row['level']+'/'+row['case']+'/'+name
                budget = expectations['rasterBudgets'].get(key)
                if budget is None: raise AssertionError('Missing reviewed raster budget: '+key)
                if comparison.get('boxMaxAbs', 0) > budget['boxMaxAbs']:
                    failures.append((key, 'geometry', comparison['boxMaxAbs'], budget['boxMaxAbs']))
                for metric in comparison.get('rasters', [])+comparison.get('cropRasters', []):
                    if metric['meanAbs'] > budget['meanAbs'] or metric['changedFraction'] > budget['changedFraction'] or metric['max'] > budget['max']:
                        failures.append((key, metric, budget))
        if failures: raise AssertionError(f'Raster regression: {failures}')
        print(f'{len(rows)} reviewed differential regression cases passed; this does NOT mean full equivalence. {len(report["equivalence"]["differentCases"])} scenarios differ.')
    else:
        print(f'DISCOVERY ONLY: {len(rows)} cases recorded; no regression verdict.')
    if args.require_equivalence and not report['equivalence']['equivalent']:
        raise SystemExit('EQUIVALENCE CHECK FAILED: '+', '.join(item['case'] for item in report['equivalence']['differentCases']))


if __name__ == '__main__':
    main()
