/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmlines/parser.h>
#include <rmrender/linux_document.h>
#ifdef RMR_TEST_FONTCONFIG
#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>
#endif

#include "pdf_fixture.h"
#include "test.h"
#define DOCUMENT_PDF rmr_linux_document_pdf
#define PAGE_PLAN rmr_linux_page_plan
#define DOCUMENT_PDF_WITH_OPTIONS rmr_linux_document_pdf_with_options
#define DOCUMENT_PDF_PRESERVING_WITH_OPTIONS \
  rmr_linux_document_pdf_preserving_with_options
#define DOCUMENT_PDF_RECOMPOSED_WITH_OPTIONS \
  rmr_linux_document_pdf_recomposed_with_options
#define DOCUMENT_PDF_TO_OUTPUT rmr_linux_document_pdf_to_output
#define DOCUMENT_PDF_PRESERVING_TO_OUTPUT \
  rmr_linux_document_pdf_preserving_to_output
#define DOCUMENT_PDF_RECOMPOSED_TO_OUTPUT \
  rmr_linux_document_pdf_recomposed_to_output
#define DOCUMENT_SVG_WITH_OPTIONS rmr_linux_document_svg_with_options
#define FONTS rmr_linux_fonts
#define FONTS_DIRECTORY rmr_linux_fonts_create_directory
#define FONTS_FREE rmr_linux_fonts_free
#define GRAIN_GROUPS true
#include "archive_fixture.h"
#define DOC "11111111-1111-1111-1111-111111111111"
#define PAGE "22222222-2222-2222-2222-222222222222"
#define DEAD "33333333-3333-3333-3333-333333333333"
int main(int argc, char** argv) {
  CHECK(argc == 2);
  matching();
  many_files();
  dense_document();
  document_options();
  document_output();
  unreadable_pages(argv[1]);

  const char content[] =
      "{\"cPages\":{\"pages\":[{\"id\":\"" PAGE "\"},{\"id\":\"" DEAD
      "\",\"deleted\":{\"value\":1}}]}}";
  const char name1[] = DOC ".content", name2[] = DOC "/" DEAD ".rm";
  rmd_file files[] = {{{name1, sizeof name1 - 1},
                       {(const uint8_t*)content, sizeof content - 1}},
                      {{name2, sizeof name2 - 1},
                       {(const uint8_t*)RM_HEADER, RM_HEADER_LENGTH}}};
  rmd_buffer zip = {0};
  CHECK(rmd_pack(files, 2, &zip, nullptr) == RMD_OK);

  rmd_reader* reader = nullptr;
  CHECK(rmd_reader_open((rmd_bytes){zip.bytes, zip.size}, nullptr, &reader,
                        nullptr) == RMD_OK);
  rmd_archive archive = {0};
  CHECK(rmd_reader_unpack(reader, nullptr, nullptr, &archive, nullptr) ==
        RMD_OK);
  rmd_reader_close(reader);
  rmd_buffer_free(&zip);

  rmr_archive_document* document = nullptr;
  rmr_error error = {0};
  OK(rmr_archive_document_create(&archive, DOC, true, nullptr, &document,
                                 &error));
  CHECK(rmr_archive_document_count(document) == 1 &&
        !strcmp(rmr_archive_document_page(document, 0)->id, PAGE));

  rm_page* page = nullptr;
  OK(rmr_archive_document_parse_page(document, 0, &page, &error));
  CHECK(page->stroke_count == 0);
  rm_page_free(page);

  rmr_buffer output = {0};
  rmr_document_report report = {0};
  OK(rmr_linux_document_pdf(document, nullptr, nullptr, RMR_PAPER_A4, 0,
                            nullptr, nullptr, nullptr, &output, &report,
                            &error));
  CHECK(output.size > 100 && report.document_pages == 1 &&
        report.rendered_pages == 1 && report.skipped_blocks == 0);
  rmr_buffer_free(&output);
  rmr_archive_document_free(document);
  rmd_archive_free(&archive);

  fixture original = source(false);
  const char pdfname[] = DOC ".pdf";
  rmd_file pdf_file = {{pdfname, sizeof pdfname - 1},
                       {(const uint8_t*)original.bytes, original.n}};
  rmd_archive pdf_archive = {.files = &pdf_file, .count = 1};
  document = nullptr;
  OK(rmr_archive_document_create(&pdf_archive, DOC, true, nullptr, &document,
                                 &error));

  rmr_status status = rmr_linux_document_pdf_preserving(
      document, nullptr, nullptr, RMR_PAPER_A4, 0, nullptr, nullptr, nullptr,
      &output, &report, &error);
  OK(status);
  CHECK(output.size == original.n &&
        !memcmp(output.bytes, original.bytes, original.n));
  CHECK(report.document_pages == 2 && report.rendered_pages == 2);
  rmr_buffer_free(&output);

  CHECK(rmr_linux_document_pdf_preserving(
            document, nullptr, nullptr, RMR_PAPER_A4, 1, nullptr, nullptr,
            nullptr, &output, &report, &error) == RMR_ERR_UNSUPPORTED);

  rmr_archive_document_free(document);

  /* Failures keep the message of the check that tripped, including the
   * layout decoder's own, instead of a generic assembly failure. */
  char many[512] = "{\"pages\":[\"p\"";
  for (size_t i = 1; i < 100; i++) strcat(many, ",\"p\"");
  strcat(many, "]}");
  rmd_file many_file = {{name1, sizeof name1 - 1},
                        {(const uint8_t*)many, strlen(many)}};
  rmd_archive many_archive = {.files = &many_file, .count = 1};
  rmr_limits tight = rmr_limits_default();
  tight.maximum_elements = 64;
  document = nullptr;
  CHECK(rmr_archive_document_create(&many_archive, DOC, true, &tight, &document,
                                    &error) == RMR_ERR_LIMIT);
  CHECK(!document && !strcmp(error.message, "JSON work limit"));
  tight = rmr_limits_default();
  tight.maximum_draws = 0;
  CHECK(rmr_archive_document_create(&many_archive, DOC, true, &tight, &document,
                                    nullptr) == RMR_ERR_LIMIT);
  CHECK(rmr_archive_document_create(&many_archive, DOC, true, &tight, &document,
                                    &error) == RMR_ERR_LIMIT);
  CHECK(!document && !strcmp(error.message, "page count limit"));
#ifdef RMR_TEST_FONTCONFIG
  /* Standalone test owns the process: release the default Pango font map,
   * then Fontconfig's global caches, for LSan. */
  pango_cairo_font_map_set_default(nullptr);
  FcFini();
#endif
  return 0;
}
