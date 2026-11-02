#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Build an offline, static Apple SDK containing RMRender and its C dependencies.

Requires Xcode, CMake, Ninja and sibling rmlines-lib/rmdoc-lib/rmmarks-lib
checkouts. No downloading, signing, Linux or Windows build is performed.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def run(*args):
    subprocess.run([str(a) for a in args], check=True)


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dependencies', type=Path, default=root.parent)
    parser.add_argument('--output', type=Path, default=root / 'swift/Artifacts/CRMRender.xcframework')
    parser.add_argument('--build', type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error(f'output already exists: {output}; choose a fresh path')
    dependencies = [args.dependencies.resolve() / n for n in ('rmlines-lib', 'rmdoc-lib', 'rmmarks-lib')]
    for path in dependencies:
        if not (path / 'CMakeLists.txt').is_file():
            parser.error(f'missing dependency checkout: {path}')
    build = args.build.resolve() if args.build else Path(tempfile.mkdtemp(prefix='rmrender-apple-'))
    build.mkdir(parents=True, exist_ok=True)
    source = build / 'driver'
    source.mkdir(exist_ok=True)
    # Bracket arguments preserve spaces without introducing CMake expansion.
    lines = ['cmake_minimum_required(VERSION 3.21)', 'project(CRMRender LANGUAGES C)']
    for prefix in ('RMLINES', 'RMDOC', 'RMMARKS', 'RMRENDER'):
        lines += [f'set({prefix}_BUILD_TESTS OFF CACHE BOOL "" FORCE)', f'set({prefix}_WERROR ON CACHE BOOL "" FORCE)']
    for option in ('APPLE', 'RMLINES', 'RMDOC', 'RMMARKS'):
        lines += [f'set(RMRENDER_BUILD_{option} ON CACHE BOOL "" FORCE)']
    for i, path in enumerate([*dependencies, root]):
        if ']=]' in str(path):
            parser.error('unsupported CMake path')
        lines += [f'add_subdirectory([=[{path}]=] package{i})']
    (source / 'CMakeLists.txt').write_text('\n'.join(lines) + '\n')
    version_header = (root / 'include/rmrender/rmrender.h').read_text()
    version = '.'.join(re.search(r'#define RMRENDER_VERSION_' + part + r' (\d+)', version_header)[1] for part in ('MAJOR', 'MINOR', 'PATCH'))
    frameworks = []
    for name, sdk, arch, system, deployment in (
        ('macos', 'macosx', 'arm64;x86_64', 'Darwin', '15.0'),
        ('ios', 'iphoneos', 'arm64', 'iOS', '18.0'),
        ('simulator', 'iphonesimulator', 'arm64;x86_64', 'iOS', '18.0'),
    ):
        native = build / name
        run('cmake', '-S', source, '-B', native, '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release',
            '-DBUILD_SHARED_LIBS=OFF', f'-DCMAKE_SYSTEM_NAME={system}', f'-DCMAKE_OSX_SYSROOT={sdk}',
            f'-DCMAKE_OSX_ARCHITECTURES={arch}', f'-DCMAKE_OSX_DEPLOYMENT_TARGET={deployment}')
        run('cmake', '--build', native, '--parallel')
        framework = build / (name + '-framework') / 'CRMRender.framework'
        headers = framework / 'Headers'
        modules = framework / 'Modules'
        headers.mkdir(parents=True, exist_ok=True)
        modules.mkdir(exist_ok=True)
        for path in [root, *dependencies]:
            for header in (path / 'include').rglob('*.h'):
                relative = header.relative_to(path / 'include')
                target = headers / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                def include(match):
                    return '#include "' + os.path.relpath(headers / match[1], target.parent) + '"'
                target.write_text(re.sub(r'#include\s*[<"]((?:rmrender|rmlines|rmdoc|rmmarks)/[^>"]+)[>"]', include, header.read_text()))
        shim = (root / 'swift/Sources/CRMRender/shim.h').read_text()
        shim = re.sub(r'<((?:rmrender|rmlines|rmdoc|rmmarks)/[^>]+)>', r'"\1"', shim)
        (headers / 'CRMRender.h').write_text(shim)
        (modules / 'module.modulemap').write_text('framework module CRMRender {\n  umbrella header "CRMRender.h"\n  export *\n  module * { export * }\n  link framework "CoreGraphics"\n  link framework "CoreText"\n  link framework "CoreFoundation"\n  link "z"\n}\n')
        libraries = sorted(native.glob('package*/lib*.a'))
        if len(libraries) != 10:
            raise RuntimeError(f'expected 10 libraries, found {libraries}')
        run('xcrun', 'libtool', '-static', '-o', framework / 'CRMRender', *libraries)
        licenses = framework / 'Resources/Licenses'
        for package in [root, *dependencies]:
            for relative in ['LICENSE', 'COPYRIGHT', 'UNICODE-LICENSE.txt', 'resources/UNICODE-LICENSE.txt', 'vendor/miniz/LICENSE.txt']:
                original = package / relative
                if original.is_file():
                    destination = licenses / ('rmrender-lib' if package == root else package.name) / relative
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(original, destination)
        import plistlib
        with (framework / 'Info.plist').open('wb') as f:
            plistlib.dump({'CFBundleIdentifier': 'com.altessa.CRMRender', 'CFBundleName': 'CRMRender',
                          'CFBundleExecutable': 'CRMRender', 'CFBundlePackageType': 'FMWK',
                          'CFBundleShortVersionString': version, 'CFBundleVersion': '1',
                          'MinimumOSVersion': deployment}, f)
        frameworks += ['-framework', framework]
    output.parent.mkdir(parents=True, exist_ok=True)
    run('xcodebuild', '-create-xcframework', *frameworks, '-output', output)
    # Resource ownership stays explicit, including open-font licence files.
    print(f'Built {output}\nResources: {root / "resources"}\nBuild: {build}')


if __name__ == '__main__':
    main()
