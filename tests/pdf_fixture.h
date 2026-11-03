/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Own synthetic PDF fixtures; no device assets. */
#ifndef PDF_FIXTURE_H
#define PDF_FIXTURE_H
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char bytes[32768];
    size_t n, offsets[32];
    unsigned gen[32];
} fixture;

static void add(fixture *f, const char *format, ...) {
    va_list a;
    va_start(a, format);
    int n = vsnprintf(f->bytes + f->n, sizeof f->bytes - f->n, format, a);
    va_end(a);
    assert(n >= 0 && (size_t)n < sizeof f->bytes - f->n);
    f->n += (size_t)n;
}

static void obj(fixture *f, unsigned id, unsigned gen, const char *body) {
    f->offsets[id] = f->n;
    f->gen[id] = gen;
    add(f, "%u %u obj\n%s\nendobj\n", id, gen, body);
}

static void stream(fixture *f, unsigned id, const char *body) {
    f->offsets[id] = f->n;
    add(f, "%u 0 obj\n<< /Length %zu >>\nstream\n%s\nendstream\nendobj\n", id, strlen(body), body);
}

static fixture source(bool certified) {
    fixture f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, 0,
        certified ? "<< /Type /Catalog /Pages 2 0 R /Perms << /DocMDP 11 0 R >> >>"
                  : "<< /Type /Catalog /Pages 2 0 R /AcroForm 7 0 R /Metadata 12 0 R /Outlines 13 "
                    "0 R /Names << /EmbeddedFiles 15 0 R >> /Custom (keep\\(this\\)) >>");
    obj(&f, 2, 0,
        "<< /Type /Pages /Kids [3 7 R 6 0 R] /Count 2 /MediaBox [0 0 200 200] /Resources 5 0 R >>");
    obj(&f, 3, 7,
        "<< /Type /Page /Parent 2 0 R /Contents 16 0 R /Annots [8 0 R 9 0 R] /CropBox [0 0 200 "
        "200] /Custom <00ff12> >>");
    stream(&f, 4, "0 0 1 rg 0 0 200 200 re f\n");
    obj(&f, 5, 0, "<< /ExtGState << /RMGs0 << /ca 0.2 >> >> >>");
    obj(&f, 6, 0, "<< /Type /Page /Parent 2 0 R /Rotate 90 /Contents 4 0 R >>");
    obj(&f, 7, 0, "<< /Fields [8 0 R 10 0 R] /NeedAppearances true >>");
    obj(&f, 8, 0,
        "<< /Type /Annot /Subtype /Widget /FT /Tx /T (Name) /V (Alice) /Rect [10 150 100 180] /P 3 "
        "7 R >>");
    obj(&f, 9, 0,
        "<< /Type /Annot /Subtype /Text /Contents (A note) /Rect [10 20 30 40] /P 3 7 R >>");
    obj(&f, 10, 0, "<< /FT /Sig /T (Signature) /V 11 0 R >>");
    /* Synthetic signature dictionary tests object preservation, not cryptography. */
    obj(&f, 11, 0, "<< /Type /Sig /ByteRange [0 10 20 30] /Contents <abcdef001122> >>");
    stream(&f, 12, "<metadata>unchanged</metadata>");
    obj(&f, 13, 0, "<< /Type /Outlines /First 14 0 R /Last 14 0 R /Count 1 >>");
    obj(&f, 14, 0, "<< /Title (Chapter) /Parent 13 0 R /Dest [3 7 R /XYZ 10 100 null] >>");
    obj(&f, 15, 0,
        "<< /Names [(file.txt) << /Type /Filespec /F (file.txt) /EF << /F 12 0 R >> >>] >>");
    obj(&f, 16, 0, "[4 0 R]");
    size_t x = f.n;
    add(&f, "xref\n0 17\n0000000000 65535 f \n");
    for (unsigned i = 1; i < 17; i++)
        add(&f, "%010zu %05u n \n", f.offsets[i], f.gen[i]);
    add(&f, "trailer\n<< /Size 17 /Root 1 0 R /ID [<0102><0304>] >>\nstartxref\n%zu\n%%%%EOF\n", x);
    return f;
}

#endif
