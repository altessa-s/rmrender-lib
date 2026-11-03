#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Build RMRender's patched PDFium SDK from pinned sources (Linux/macOS).

Requires git, Python 3.12+, Ninja, a native development toolchain and network.
Linux ARM64 hosts also need to execute Chromium's x86-64 Clang toolchain.
Downloads occur only when explicitly running this script, never from CMake.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import urllib.request

REVISION = '43d55e429cf72c21e9a1e60475b50feefa16fda9'
DEPOT = '816da11f80777dcf8c10bdaa84df18450dcba12a'
PACKAGING = '4bdb7acd45b39ef6a7fc2a5b4c83f0398e45c9a6'
PATCHES = {
    'shared_library.patch': '0fbc6207a5c9da8528914418598a494a3ed358ccb8ee91c8b3f8c1f95ab84057',
    'public_headers.patch': 'b21cedea243ced82977251000e356240de038712b8cf8721b3dda3d595c6affd',
    'clang_rt.patch': '45a4f7b5796c381bbf4cd74c54d613d63b8644df96ab3fa32ad7d9132b21f151',
    'mac/build.patch': '10293bdd32b9f989e2eaa8eea91647e2121dc6baad814dd524e8ffe2bbdbf5e2',
}
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--work', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
p.add_argument('--jobs', type=int, default=4)
a = p.parse_args()
if platform.system() not in ('Linux', 'Darwin') or a.jobs < 1:
    p.error('requires Linux/macOS and positive --jobs')
cpu = {'arm64': 'arm64', 'aarch64': 'arm64', 'x86_64': 'x64'}.get(platform.machine())
if not cpu:
    p.error('unsupported host architecture')
work, output = a.work.resolve(), a.output.resolve()
work.mkdir(parents=True, exist_ok=True)
depot, checkout = work / 'depot_tools', work / 'work'
source = checkout / 'pdfium'
checkout.mkdir(exist_ok=True)
ninja = shutil.which('ninja')
if not ninja:
    p.error('ninja is required')
env = os.environ.copy()
env.update(DEPOT_TOOLS_UPDATE='0', DEPOT_TOOLS_METRICS='0')
env['PATH'] = str(depot) + os.pathsep + env['PATH']

def run(cmd, cwd=None):
    print(' '.join(map(str, cmd)), flush=True)
    subprocess.run(list(map(str, cmd)), cwd=cwd, env=env, check=True)

def clone(url, revision, path):
    if not (path / '.git').exists():
        run(['git', 'init', path])
        run(['git', 'remote', 'add', 'origin', url], path)
        run(['git', 'fetch', '--depth=1', 'origin', revision], path)
        run(['git', 'checkout', '--detach', 'FETCH_HEAD'], path)
    head = subprocess.check_output(['git', '-C', str(path), 'rev-parse', 'HEAD'], text=True).strip()
    if head != revision:
        raise SystemExit(f'{path}: unexpected revision {head}; use a separate work directory')

def apply(path, cwd):
    # Reruns accept only an already applied, byte-identical patch.
    reverse = subprocess.run(['git', 'apply', '--reverse', '--check', str(path)],
                             cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if reverse.returncode:
        run(['git', 'apply', '--check', path], cwd)
        run(['git', 'apply', path], cwd)

clone('https://chromium.googlesource.com/chromium/tools/depot_tools.git', DEPOT, depot)
clone('https://pdfium.googlesource.com/pdfium.git', REVISION, source)
(checkout / '.gclient').write_text('solutions = ' + repr([{
    'name': 'pdfium', 'url': 'https://pdfium.googlesource.com/pdfium.git',
    'deps_file': 'DEPS', 'managed': False,
    'custom_deps': {'pdfium/third_party/clang-format/script': None},
    'custom_vars': {'checkout_configuration': 'minimal'},
}]) + '\n')
# Remote compilation is unused and its CIPD package is unavailable on ARM64 Linux.
deps = source / 'DEPS'
s = deps.read_text()
needle = "'buildtools/reclient': {\n"
if needle + "    'condition': 'False',\n" not in s:
    if s.count(needle) != 1:
        raise SystemExit('unexpected reclient dependency declaration')
    deps.write_text(s.replace(needle, needle + "    'condition': 'False',\n"))
patch_dir = work / 'patches'
patch_dir.mkdir(exist_ok=True)
patch_jobs = []
for name, digest in PATCHES.items():
    if name.startswith('mac/') and platform.system() != 'Darwin':
        continue
    path = patch_dir / name.replace('/', '-')
    if not path.exists():
        url = f'https://raw.githubusercontent.com/bblanchon/pdfium-binaries/{PACKAGING}/patches/{name}'
        with urllib.request.urlopen(url, timeout=120) as response:
            path.write_bytes(response.read())
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise SystemExit(f'{name}: checksum mismatch')
    patch_jobs.append((path, source / 'build' if name in ('clang_rt.patch', 'mac/build.patch') else source))
patch = Path(__file__).resolve().parent.parent / 'patches/pdfium/8125-actualtext-non-bmp.patch'
patch_jobs.append((patch, source))
# Restore only our exact patches before syncing dependencies; never reset a
# checkout or discard unrelated edits. This also makes interrupted runs resumable.
for path, cwd in patch_jobs:
    if cwd.exists():
        check = subprocess.run(['git', 'apply', '--reverse', '--check', str(path)],
                               cwd=cwd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if check.returncode == 0:
            run(['git', 'apply', '--reverse', path], cwd)
run(['gclient', 'sync', '--no-history', '--shallow', '--nohooks'], checkout)
run(['gclient', 'runhooks'], checkout)
for path, cwd in patch_jobs:
    apply(path, cwd)
llvm = work / 'llvm'
run([sys.executable, source / 'tools/clang/scripts/update.py', '--output-dir', llvm])
if platform.system() == 'Darwin':
    run([sys.executable, source / 'tools/clang/scripts/update.py', '--output-dir', llvm, '--package', 'objdump'])
os_name = 'mac' if platform.system() == 'Darwin' else 'linux'
build = source / 'out' / ('rmr-' + cpu)
build.mkdir(parents=True, exist_ok=True)
options = {
    'is_debug': False, 'pdf_is_standalone': True, 'pdf_use_partition_alloc': False,
    'target_cpu': cpu, 'target_os': os_name, 'pdf_enable_v8': False, 'pdf_enable_xfa': False,
    'treat_warnings_as_errors': False, 'is_component_build': False,
    'clang_use_chrome_plugins': False, 'clang_base_path': str(llvm),
    'use_remoteexec': False, 'use_siso': False, 'symbol_level': 0,
}
if os_name == 'linux':
    options['use_sysroot'] = False
(build / 'args.gn').write_text(''.join(f'{key} = {json.dumps(value)}\n' for key, value in options.items()))
gn = source / 'buildtools' / ('mac' if os_name == 'mac' else 'linux64') / 'gn'
run([gn, 'gen', build], source)
run([ninja, '-C', build, '-j', a.jobs, 'pdfium'])
(output / 'lib').mkdir(parents=True, exist_ok=True)
(output / 'include').mkdir(exist_ok=True)
lib = 'libpdfium.dylib' if os_name == 'mac' else 'libpdfium.so'
shutil.copy2(build / lib, output / 'lib' / lib)
for header in (source / 'public').glob('*.h'):
    shutil.copy2(header, output / 'include' / header.name)
if os_name == 'mac':
    run(['install_name_tool', '-id', '@rpath/libpdfium.dylib', output / 'lib' / lib])
    run(['codesign', '--force', '--sign', '-', output / 'lib' / lib])
# Preserve upstream notices, including bundled third-party dependencies.
for root in [source / 'LICENSE', source / 'third_party', source / 'buildtools/third_party']:
    files = [root] if root.is_file() else root.rglob('*')
    for file in files:
        if file.is_file() and file.name.upper().startswith(('LICENSE', 'COPYING', 'NOTICE')):
            target = output / 'licenses' / file.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
manifest = {'pdfium': REVISION, 'depot_tools': DEPOT, 'packaging': PACKAGING,
            'actualtext_patch_sha256': hashlib.sha256(patch.read_bytes()).hexdigest(),
            'library_sha256': hashlib.sha256((output / 'lib' / lib).read_bytes()).hexdigest(),
            'gn_args': options}
(output / 'rmrender-pdfium.json').write_text(json.dumps(manifest, indent=2) + '\n')
print('SDK installed in ' + str(output) + '; validate with RMRENDER_REQUIRE_PDFIUM_ACTUALTEXT=ON')
