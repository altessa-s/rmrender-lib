/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <fpdf_text.h>
#include <fpdfview.h>

#include "pdf_fixture.h"
#include "test.h"

/* Standalone process: this test owns PDFium initialization. Extraction uses
 * Unicode scalars; FPDFText_GetText's legacy UCS-2 API cannot represent these.
 */
static const struct {
  const char* hex;
  unsigned codepoints[8];
  size_t count;
} cases[] = {
    {"FEFF00680065006C006C006F", {0x68, 0x65, 0x6c, 0x6c, 0x6f}, 5},
    {"FEFFD83DDC4DD83CDFFD", {0x1f44d, 0x1f3fd}, 2},
    {"FEFF0041D83DDC4DD83CDFFD0042", {0x41, 0x1f44d, 0x1f3fd, 0x42}, 4},
    {"FEFFD83DDC69200DD83DDCBB", {0x1f469, 0x200d, 0x1f4bb}, 3},
    {"FEFF2708FE0F", {0x2708, 0xfe0f}, 2},
    {"FEFFD83DDE00", {0x1f600}, 1},
};

static fixture actual_text(const char* hex) {
  fixture f = {0};
  add(&f, "%%PDF-1.7\n");
  obj(&f, 1, 0, "<< /Type /Catalog /Pages 2 0 R >>");
  obj(&f, 2, 0, "<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  obj(&f, 3, 0,
      "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Resources << "
      "/Font << /F1 4 0 R >> "
      ">> /Contents 5 0 R >>");
  obj(&f, 4, 0, "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");

  char content[512];
  snprintf(
      content, sizeof content,
      "/Span << /ActualText <%s> >> BDC BT /F1 12 Tf 10 100 Td (A) Tj ET EMC",
      hex);
  stream(&f, 5, content);

  size_t x = f.n;
  add(&f, "xref\n0 6\n0000000000 65535 f \n");
  for (unsigned i = 1; i < 6; i++) add(&f, "%010zu 00000 n \n", f.offsets[i]);
  add(&f, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", x);

  return f;
}

int main(void) {
  (void)source;
  FPDF_InitLibrary();

  bool missing = false;
  for (size_t i = 0; i < sizeof cases / sizeof *cases; i++) {
    fixture f = actual_text(cases[i].hex);
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(f.bytes, f.n, nullptr);
    CHECK(doc);
    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    CHECK(page);
    FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
    CHECK(text);
    int count = FPDFText_CountChars(text);
    bool correct = count == (int)cases[i].count;
    for (int k = 0; correct && k < count; k++)
      correct = FPDFText_GetUnicode(text, k) == cases[i].codepoints[k];
    if (!correct) {
      /* Accept as a known missing capability only the exact upstream
       * behavior: BMP characters survive, supplementary scalars vanish. */
      int k = 0;
      for (size_t j = 0; j < cases[i].count; j++)
        if (cases[i].codepoints[j] < 0x10000) {
          CHECK(k < count &&
                FPDFText_GetUnicode(text, k) == cases[i].codepoints[j]);
          k++;
        }
      CHECK(k == count);
      missing = true;
    }
    FPDFText_ClosePage(text);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
  }

  FPDF_DestroyLibrary();
  if (missing) {
    fputs(
        "PDFium drops non-BMP ActualText; rebuild with "
        "patches/pdfium/8125-actualtext-non-bmp.patch\n",
        stderr);
    return 77;
  }
  return 0;
}
