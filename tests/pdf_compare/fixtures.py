# Copyright 2026 ALTESSA SOLUTIONS INC.
# Use of this source code is governed by an MIT-style license that can be
# found in the LICENSE file.
"""Deterministic synthetic PDF fixtures, built from PDF objects we own."""
import struct
import zlib

INK = b'q /TestAlpha gs 1 0 0 rg 25 40 70 30 re f Q\n'


def stream(data, extra=b'', length=None):
    count = str(len(data)).encode() if length is None else length
    return b'<< /Length '+count+b' '+extra+b' >>\nstream\n'+data+b'\nendstream'


def encode(objects, generations=None, trailer=b'', xref_stream=False, hybrid=False, forward=False, packed=False, flate=False, predictor=False, malformed=None):
    generations = generations or {}
    objects = dict(objects)
    compressed = {}
    if packed:
        container = max(objects) + 1
        header, payload = b'', b''
        for index, number in enumerate([1, 2, 3]):
            header += f'{number} {len(payload)} '.encode()
            payload += objects.pop(number) + b' '
            compressed[number] = (container, 99 if malformed == 'object_index' else index)
        objects[container] = stream(zlib.compress(header + payload), f'/Type /ObjStm /N 3 /First {len(header)} /Filter /FlateDecode'.encode())
    result = bytearray(b'%PDF-1.7\n% own fixture\n')
    offsets = {}
    for number, body in sorted(objects.items()):
        offsets[number] = len(result)
        result += f'{number} {generations.get(number, 0)} obj\n'.encode()+body+b'\nendobj\n'
    if xref_stream or hybrid:
        number = max(objects)+1
        offsets[number] = len(result)
        entries = bytearray()
        for i in range(number+1):
            if i in compressed:
                entries += struct.pack('>BIH', 2, *compressed[i])
            else:
                entries += struct.pack('>BIH', 1 if i in offsets else 0, offsets.get(i, 0), generations.get(i, 0) if i else 65535)
        options = b''
        if malformed == 'xref_truncated': entries = entries[:-1]
        if predictor:
            previous, predicted = bytes(7), bytearray()
            for i in range(0, len(entries), 7):
                row = entries[i:i+7]
                predicted += bytes([7 if malformed == 'predictor' else 2]) + bytes((a-b) % 256 for a,b in zip(row, previous))
                previous = row
            entries = predicted
            options = b' /DecodeParms << /Predictor 12 /Columns 7 >>'
        if flate:
            entries = zlib.compress(entries)
            options += b' /Filter /FlateDecode'
        body = stream(entries, f'/Type /XRef /Size {number+1} /Root 1 0 R /W [1 4 2]'.encode() + options)
        result += f'{number} 0 obj\n'.encode()+body+b'\nendobj\n'
        if xref_stream:
            return bytes(result+f'startxref\n{offsets[number]}\n%%EOF\n'.encode())
        trailer += f' /XRefStm {offsets[number]}'.encode()
    first = None
    if forward:
        first = len(result)
        result += b'xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size '+str(max(offsets)+1).encode()+b' /Root 1 0 R /Prev 0000000000 >>\n'
        spot = result.index(b'/Prev ')+6
        result[spot:spot+10] = f'{len(result):010d}'.encode()
    cross = len(result)
    result += f'xref\n0 {max(offsets)+1}\n'.encode()
    for i in range(max(offsets)+1):
        result += f'{offsets.get(i, 0):010d} {generations.get(i, 0) if i else 65535:05d} {"n" if i in offsets else "f"} \n'.encode()
    result += b'trailer\n<< /Size '+str(max(offsets)+1).encode()+b' /Root 1 0 R /ID [<001122><334455>] '+trailer+b' >>\n'
    return bytes(result+f'startxref\n{first if first is not None else cross}\n%%EOF\n'.encode())


def revision(data, rows=b'0 1\n0000000000 65535 f \n', extra=b''):
    previous = int(data.rsplit(b'startxref', 1)[1].split()[0])
    return data+b'xref\n'+rows+b'trailer\n<< /Size 24 /Prev '+str(previous).encode()+b' '+extra+b' >>\nstartxref\n'+str(len(data)).encode()+b'\n%%EOF\n'


def source(kind='basic', expected_marks=0, expand=False):
    gen = 7 if kind == 'generation' else 0
    page_ref = f'3 {gen} R'.encode()
    paint = b'q /OriginalState gs 0.92 0.95 0.98 rg 0 0 200 300 re f 0 0 0 RG 2 w 10 20 m 180 270 l S BT /F0 10 Tf 15 280 Td (Own source) Tj ET Q\n'
    if kind == 'resource_collision':
        paint = paint.replace(b'OriginalState', b'TestAlpha')
    fields = b'/MediaBox [0 0 200 300] /Resources 5 0 R'
    first_extra = b''
    if kind in ['rotated', 'cropped_rotated']: first_extra += b' /Rotate 90'
    if kind == 'rotated_180': first_extra += b' /Rotate 180'
    if kind == 'rotated_270': first_extra += b' /Rotate 270'
    if kind == 'rotated_negative': first_extra += b' /Rotate -90'
    if kind == 'rotated_origin': first_extra += b' /Rotate 90 /MediaBox [20 30 220 330]'
    if kind in ['cropped', 'cropped_rotated', 'cropped_expand']: first_extra += b' /CropBox [10 15 190 285]'
    if kind == 'cropped_inherited': fields += b' /CropBox [10 15 190 285]'
    if kind == 'cropped_origin': first_extra += b' /MediaBox [20 30 220 330] /CropBox [30 45 210 315]'
    if kind == 'origin': first_extra += b' /MediaBox [20 30 220 330]'
    if kind == 'userunit': first_extra += b' /UserUnit 2'
    if kind == 'userunit_one': first_extra += b' /UserUnit 1.0'
    if kind == 'userunit_zero': first_extra += b' /UserUnit 0'
    if kind == 'userunit_null': first_extra += b' /UserUnit null'
    content = b'/Contents 4 0 R'
    if kind == 'content_array': content = b'/Contents 22 0 R'
    if kind == 'no_content': content = b''
    if kind == 'null_content': content = b'/Contents null'
    root_extra = b' /Perms << /DocMDP 12 0 R >>' if kind == 'certified' else b''
    objects = {
        1: b'<< /Type /Catalog /Pages 2 0 R /AcroForm 7 0 R /Metadata 13 0 R /Outlines 14 0 R /Names << /EmbeddedFiles 16 0 R >> /Custom (keep\\(this\\)\\000)'+root_extra+b' >>',
        2: b'<< /Type /Pages /Kids ['+page_ref+b' 6 0 R] /Count 2 '+fields+b' >>',
        3: b'<< /Type /Page /Parent 2 0 R '+content+b' /Annots [8 0 R 9 0 R 10 0 R] /Custom <00ff1223>'+first_extra+b' >>',
        4: stream(paint, length=b'23 0 R' if kind == 'indirect_length' else None),
        5: b'<< /Font << /F0 20 0 R >> /ExtGState << /OriginalState << /ca 1 /CA 1 >> /TestAlpha << /ca 1 /CA 1 >> >> >>',
        6: b'<< /Type /Page /Parent 2 0 R /Contents 21 0 R >>',
        7: b'<< /Fields [8 0 R 11 0 R] /NeedAppearances true >>',
        8: b'<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (Alice) /Rect [10 150 100 180] /P '+page_ref+b' >>',
        9: b'<< /Type /Annot /Subtype /Text /Contents (A note) /Rect [10 20 30 40] /P '+page_ref+b' >>',
        10: b'<< /Type /Annot /Subtype /Link /Rect [100 250 180 275] /A << /S /URI /URI (https://example.test/own) >> >>',
        11: b'<< /FT /Sig /T (Synthetic signature dictionary) /V 12 0 R >>',
        12: b'<< /Type /Sig /ByteRange [0 10 20 30] /Contents <abcdef001122> >>',
        13: stream(b'<metadata>own unchanged bytes</metadata>', b'/Type /Metadata /Subtype /XML'),
        14: b'<< /Type /Outlines /First 15 0 R /Last 15 0 R /Count 1 >>',
        15: b'<< /Title (Chapter) /Parent 14 0 R /Dest ['+page_ref+b' /XYZ 10 100 null] >>',
        16: b'<< /Names [(own.txt) 17 0 R] >>',
        17: b'<< /Type /Filespec /F (own.txt) /EF << /F 18 0 R >> >>',
        18: stream(b'Our attached file\x00\xff', b'/Type /EmbeddedFile'),
        20: b'<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>',
        21: stream(b'q 0.9 1 0.9 rg 0 0 200 300 re f 0 0 0 RG 3 w 10 280 m 180 20 l S Q\n'),
        22: b'[4 0 R]',
        23: str(len(paint)).encode(),
    }
    if kind == 'page_cycle': objects[2] = b'<< /Type /Pages /Kids [2 0 R] /Count 1 >>'
    if kind == 'duplicate_key': objects[3] = objects[3][:-2]+b' /Contents 21 0 R >>'
    if kind == 'escaped_key': objects[3] = objects[3].replace(b'/Contents', b'/Cont#65nts')
    if kind == 'wrong_generation': objects[2] = objects[2].replace(b'3 0 R', b'3 7 R')
    if expected_marks:
        # Independent oracle: one fresh content stream, direct resources; no
        # parser/import/reference rewrite and no incremental-writer code.
        base = b'' if kind in ['no_content', 'null_content'] else paint
        objects[4] = stream(base+INK.replace(b'TestAlpha', b'ExpectedAlpha')*expected_marks)
        objects[3] = objects[3].replace(content, b'/Contents 4 0 R') if content else objects[3][:-2]+b' /Contents 4 0 R >>'
        objects[5] = objects[5].replace(b'/ExtGState <<', b'/ExtGState << /ExpectedAlpha << /ca 0.65 /CA 0.65 /BM /Multiply >>')
        if expand:
            objects[3] = objects[3][:-2]+b' /MediaBox [-20 -10 220 330] '+(b'' if kind == 'cropped_expand' else b'/CropBox [-20 -10 220 330] ')+b'/ArtBox [0 0 200 300] >>'
    data = encode(objects, {3: gen}, trailer=b'/Encrypt null' if kind == 'encrypt_marker' else b'',
                  xref_stream=kind in ['xref_stream', 'xref_flate', 'xref_predictor', 'object_stream', 'xref_truncated', 'bad_predictor', 'bad_object_index'],
                  hybrid=kind in ['hybrid', 'hybrid_object_stream'], forward=kind == 'forward_prev',
                  packed=kind in ['object_stream', 'hybrid_object_stream', 'bad_object_index'],
                  flate=kind in ['xref_flate', 'xref_predictor', 'object_stream', 'hybrid_object_stream', 'bad_predictor', 'bad_object_index'],
                  predictor=kind in ['xref_predictor', 'bad_predictor'],
                  malformed={'xref_truncated': 'xref_truncated', 'bad_predictor': 'predictor', 'bad_object_index': 'object_index'}.get(kind))
    if kind == 'previous_revision': data = revision(data)
    if kind == 'freed_content': data = revision(data, b'4 1\n0000000000 00001 f \n')
    if kind == 'prev_cycle':
        end = len(data)
        data += b'xref\n0 1\n0000000000 65535 f \ntrailer\n<< /Size 24 /Prev '+str(end).encode()+b' >>\nstartxref\n'+str(end).encode()+b'\n%%EOF\n'
    if kind == 'truncated': data = data[:data.index(b'xref')]
    return data


def overlay():
    return encode({1: b'<< /Type /Catalog /Pages 2 0 R >>',
                   2: b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',
                   3: b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 300] /Contents 4 0 R /Resources << /ExtGState << /TestAlpha << /ca 0.65 /CA 0.65 /BM /Multiply >> >> >> >>',
                   4: stream(INK)})
