#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Build a checksum-pinned software rendering stack in an explicit prefix.

Requires Meson >= 1.11, Ninja, make, a C/C++ compiler, pkg-config, and development
headers for zlib, libpng, Brotli, Expat, libffi, PCRE2 and JPEG, plus gperf. macOS
also requires gettext headers/libraries in CFLAGS, CXXFLAGS and LDFLAGS. No system install.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import shutil
import subprocess
import tarfile
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--prefix', required=True, type=Path)
parser.add_argument('--work', required=True, type=Path)
parser.add_argument('--source-cache', type=Path)
parser.add_argument('--jobs', type=int, default=4)
args = parser.parse_args()
if args.jobs < 1:
    parser.error('--jobs must be positive')
prefix, work = args.prefix.resolve(), args.work.resolve()
prefix.mkdir(parents=True, exist_ok=True)
work.mkdir(parents=True, exist_ok=True)
pins = json.loads(Path(__file__).with_name('render-stack.json').read_text())
options = {
    'gdk-pixbuf': ['auto_features=disabled', 'png=enabled', 'jpeg=enabled',
                   'builtin_loaders=all', 'introspection=disabled', 'documentation=false',
                   'man=false', 'tests=false', 'installed_tests=false', 'gio_sniffing=false'],
    'glib': ['tests=false', 'introspection=disabled', 'documentation=false',
             'man-pages=disabled', 'nls=disabled', 'libmount=disabled', 'selinux=disabled',
             'sysprof=disabled', 'dtrace=disabled', 'systemtap=disabled'],
    'freetype': ['harfbuzz=disabled', 'brotli=enabled', 'png=enabled', 'zlib=system',
                 'bzip2=disabled', 'tests=disabled'],
    'fribidi': ['docs=false', 'bin=false', 'tests=false'],
    'pixman': ['auto_features=disabled', 'tests=disabled', 'demos=disabled', 'tls=enabled'],
    'fontconfig': ['doc=disabled', 'tests=disabled', 'tests-external-fonts=disabled',
                   'tools=disabled', 'cache-build=disabled', 'nls=disabled', 'xml-backend=expat'],
    'harfbuzz': ['auto_features=disabled', 'glib=enabled', 'freetype=enabled',
                 'tests=disabled', 'docs=disabled', 'utilities=disabled',
                 'introspection=disabled', 'gpu=disabled'],
    'cairo': ['auto_features=disabled', 'fontconfig=enabled', 'freetype=enabled',
              'png=enabled', 'zlib=enabled', 'glib=enabled', 'tests=disabled'],
    'pango': ['auto_features=disabled', 'fontconfig=enabled', 'freetype=enabled',
              'cairo=enabled', 'libthai=enabled', 'introspection=disabled',
              'documentation=false', 'build-testsuite=false', 'build-examples=false'],
}
order = ['glib', 'freetype', 'fribidi', 'pixman', 'fontconfig', 'harfbuzz',
         'cairo', 'libdatrie', 'libthai', 'pango', 'gdk-pixbuf']
env = os.environ.copy()
env['PKG_CONFIG_PATH'] = str(prefix / 'lib/pkgconfig') + os.pathsep + env.get('PKG_CONFIG_PATH', '')
if platform.system() == 'Darwin':
    # Do not discover optional system Cairo backends (e.g. Homebrew Quartz)
    # alongside our software-only Cairo. Allow only native prerequisite PCs.
    pc_dirs = []
    for package in ['zlib', 'libpng', 'libbrotlidec', 'expat', 'libffi', 'libpcre2-8', 'libjpeg']:
        directory = subprocess.check_output(['pkg-config', '--variable=pcfiledir', package], text=True).strip()
        if directory:
            pc_dirs.append(str(Path(directory).resolve()))
    env['PKG_CONFIG_LIBDIR'] = os.pathsep.join(dict.fromkeys(pc_dirs))
env['PATH'] = str(prefix / 'bin') + os.pathsep + env['PATH']
env['LD_LIBRARY_PATH'] = str(prefix / 'lib') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
for key in ['CFLAGS', 'CXXFLAGS']:
    env[key] = env.get(key, '') + ' -ffp-contract=off'

def run(command, cwd=None):
    print(' '.join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=cwd, env=env, check=True)

def prepare(name):
    spec = pins[name]
    archive = work / (name + '-' + spec['version'] + '.archive')
    if not archive.exists() and args.source_cache:
        for candidate in args.source_cache.glob(name + '-' + spec['version'] + '.tar.*'):
            if hashlib.sha256(candidate.read_bytes()).hexdigest() == spec['sha256']:
                shutil.copyfile(candidate, archive)
                break
    if not archive.exists():
        with urllib.request.urlopen(spec['url'], timeout=60) as response:
            data = response.read(128 * 1024 * 1024 + 1)
        if hashlib.sha256(data).hexdigest() != spec['sha256']:
            raise SystemExit(name + ': archive checksum mismatch')
        archive.write_bytes(data)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != spec['sha256']:
        raise SystemExit(name + ': cached archive checksum mismatch')
    source = work / (name + '-' + spec['version'])
    if not source.exists():
        with tarfile.open(archive, 'r:*') as package:
            package.extractall(work, filter='data')
    return source

for name in order:
    source = prepare(name)
    build = work / ('build-' + name)
    if name in ['libdatrie', 'libthai']:
        build.mkdir(exist_ok=True)
        if not (build / 'Makefile').exists():
            run([source / 'configure', '--prefix=' + str(prefix), '--disable-static'], cwd=build)
        run(['make', '-j' + str(args.jobs)], cwd=build)
        run(['make', 'install'], cwd=build)
    else:
        configure = ['meson', 'setup']
        if (build / 'build.ninja').exists():
            configure.append('--reconfigure')
        run(configure + [build, source, '--prefix=' + str(prefix), '--libdir=lib',
             '--buildtype=release', '--wrap-mode=nodownload', '-Ddefault_library=shared'] +
            ['-D' + option for option in options[name]])
        run(['ninja', '-C', build, '-j', args.jobs])
        run(['meson', 'install', '-C', build, '--no-rebuild'])
(prefix / 'rmrender-stack.json').write_text(json.dumps(pins, indent=2) + '\n')
print('Rendering stack installed in ' + str(prefix))
