#!/usr/bin/env python3
# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Own approval-signature fixture: verify the signed bytes after incremental updates.
No assertion of trust, certification permissions, or signing the new revision.
Usage: python3 tests/verify_pdf_signature.py --writer build/test_pdf_incremental
Requires OpenSSL with the cms command; no network or third-party PDF fixtures.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def run(*args):
    subprocess.run([str(a) for a in args], check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--writer', type=Path, required=True)
    parser.add_argument('--openssl', default='openssl')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='rmr-signature-') as directory:
        root = Path(directory)
        run(args.openssl, 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-keyout', root/'key.pem',
            '-out', root/'cert.pem', '-subj', '/CN=RMRender synthetic fixture', '-days', '1')
        data = bytearray(b'%PDF-1.7\n')
        offsets = [0]
        paint = b'0 0 1 rg 0 0 200 200 re f\n'
        bodies = [
            b'<< /Type /Catalog /Pages 2 0 R /AcroForm << /Fields [5 0 R] >> >>',
            b'<< /Type /Pages /Count 1 /Kids [3 0 R] >>',
            b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << >> /Contents 4 0 R >>',
            b'<< /Length '+str(len(paint)).encode()+b' >>\nstream\n'+paint+b'\nendstream',
            b'<< /FT /Sig /T (Approval) /V 6 0 R >>',
            b'<< /Type /Sig /Filter /Adobe.PPKLite /SubFilter /adbe.pkcs7.detached '
            b'/ByteRange [0 0000000000 0000000000 0000000000] /Contents <'+b'0'*16384+b'> >>'
        ]
        for i, body in enumerate(bodies, 1):
            offsets.append(len(data))
            data += f'{i} 0 obj\n'.encode()+body+b'\nendobj\n'
        cross = len(data)
        data += b'xref\n0 7\n0000000000 65535 f \n'
        for offset in offsets[1:]:
            data += f'{offset:010d} 00000 n \n'.encode()
        data += f'trailer\n<< /Size 7 /Root 1 0 R >>\nstartxref\n{cross}\n%%EOF\n'.encode()
        start = data.index(b'/Contents <')+len(b'/Contents ')
        end = start+16386
        data = data.replace(b'[0 0000000000 0000000000 0000000000]',
                            f'[0 {start:010d} {end:010d} {len(data)-end:010d}]'.encode())
        signed = data[:start]+data[end:]
        (root/'signed.bin').write_bytes(signed)
        run(args.openssl, 'cms', '-sign', '-binary', '-in', root/'signed.bin', '-signer', root/'cert.pem',
            '-inkey', root/'key.pem', '-outform', 'DER', '-out', root/'signature.der')
        signature = (root/'signature.der').read_bytes()
        assert len(signature)*2 <= 16384
        data[start+1:end-1] = signature.hex().encode().ljust(16384, b'0')
        original = bytes(data)
        (root/'original.pdf').write_bytes(original)
        current = root/'original.pdf'
        for iteration in range(2):
            output = root/f'update-{iteration}.pdf'
            run(args.writer, current, output)
            updated = output.read_bytes()
            assert updated.startswith(current.read_bytes())
            assert updated[:len(original)] == original
            assert updated[:start]+updated[end:len(original)] == signed
            (root/'verify.bin').write_bytes(updated[:start]+updated[end:len(original)])
            run(args.openssl, 'cms', '-verify', '-binary', '-inform', 'DER', '-in', root/'signature.der',
                '-content', root/'verify.bin', '-noverify', '-out', root/'verified.bin')
            assert (root/'verified.bin').read_bytes() == signed
            current = output
        print('Original approval signature cryptographically verifies after two C incremental updates.')


if __name__ == '__main__':
    main()
