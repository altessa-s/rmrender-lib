/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Writes the fuzz target's seed corpus from the test fixtures, so nothing
 * binary is committed: PDFs into <directory>/pdf, .rm pages into
 * <directory>/page and .rmdoc archives into <directory>/archive (the three
 * subdirectories must exist). Every seed is a few kilobytes at most.
 *
 *   fuzz_seeds <directory> */
#include <rmdoc/rmdoc.h>
#include <rmlines/parser.h>
#include <rmlines/writer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pdf_fixture.h"

#define DOCUMENT "44444444-4444-4444-4444-444444444444"

static int failures;

static void save(const char* directory, const char* kind, const char* name,
                 const void* bytes, size_t size) {
  char path[1024];
  snprintf(path, sizeof path, "%s/%s/%s", directory, kind, name);
  FILE* f = fopen(path, "wb");
  bool ok = f && fwrite(bytes, 1, size, f) == size;
  if (f && fclose(f)) ok = false;
  if (!ok) {
    fprintf(stderr, "cannot write %s\n", path);
    failures++;
  }
}

/* A page of one stroke of m points for each of the n tools; freed with
 * rm_bytes_free. */
static rmd_bytes strokes(const rm_pen_tool* tools, size_t n, size_t m) {
  rm_draft_point* points = calloc(n * m, sizeof *points);
  rm_draft_stroke* drafts = calloc(n, sizeof *drafts);
  if (!points || !drafts) {
    free(points);
    free(drafts);
    failures++;
    return (rmd_bytes){0};
  }
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < m; j++)
      points[i * m + j] = rm_draft_point_make(
          (float)(-500 + 1000.0 * (double)j / (double)m),
          (float)(100 + 150.0 * (double)i + (j % 2 ? 20 : 0)));
    drafts[i] = rm_draft_stroke_make(
        tools[i], i % 2 ? RM_COLOR_BLUE : RM_COLOR_BLACK, points + i * m, m);
  }
  rm_page_draft draft = {.sheet = {1404, 1872, 1404, 1872},
                         .column = rm_draft_column_of(RM_COLUMN_MEDIUM),
                         .strokes = drafts,
                         .stroke_count = n};
  uint8_t* bytes = nullptr;
  size_t size = 0;
  if (rm_write_page(&draft, &bytes, &size, nullptr) != RM_OK) failures++;
  free(points);
  free(drafts);
  return (rmd_bytes){bytes, size};
}

static void archive(const char* directory, const char* name,
                    const char* content, const rmd_bytes* pages,
                    const char* const* ids, size_t count, const fixture* pdf) {
  rmd_file files[8];
  char names[8][96];
  size_t n = 0;
  snprintf(names[n], sizeof names[n], DOCUMENT ".content");
  files[n] = (rmd_file){{names[n], strlen(names[n])},
                        {(const uint8_t*)content, strlen(content)}};
  n++;
  if (pdf) {
    snprintf(names[n], sizeof names[n], DOCUMENT ".pdf");
    files[n] = (rmd_file){{names[n], strlen(names[n])},
                          {(const uint8_t*)pdf->bytes, pdf->n}};
    n++;
  }
  for (size_t i = 0; i < count && n < 8; i++, n++) {
    snprintf(names[n], sizeof names[n], DOCUMENT "/%s.rm", ids[i]);
    files[n] = (rmd_file){{names[n], strlen(names[n])}, pages[i]};
  }
  rmd_buffer zip = {0};
  if (rmd_pack(files, n, &zip, nullptr) != RMD_OK) {
    failures++;
    return;
  }
  save(directory, "archive", name, zip.bytes, zip.size);
  rmd_buffer_free(&zip);
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <directory>\n", argv[0]);
    return 2;
  }
  const char* directory = argv[1];

  fixture plain = source(false), certified = source(true);
  save(directory, "pdf", "outlined.pdf", plain.bytes, plain.n);
  save(directory, "pdf", "certified.pdf", certified.bytes, certified.n);

  static const rm_pen_tool tools[] = {
      RM_TOOL_PAINTBRUSH_2,  RM_TOOL_MECHANICAL_PENCIL_2,
      RM_TOOL_PENCIL_2,      RM_TOOL_BALLPOINT_2,
      RM_TOOL_MARKER_2,      RM_TOOL_FINELINER_2,
      RM_TOOL_HIGHLIGHTER_2, RM_TOOL_CALIGRAPHY,
      RM_TOOL_SHADER,        RM_TOOL_ERASER,
      RM_TOOL_ERASER_AREA};
  constexpr size_t N = sizeof tools / sizeof *tools;
  rmd_bytes every = strokes(tools, N, 6), one = strokes(tools + 3, 1, 4);
  rmd_bytes empty = {(const uint8_t*)RM_HEADER, RM_HEADER_LENGTH};
  save(directory, "page", "every-tool.rm", every.bytes, every.size);
  save(directory, "page", "one-stroke.rm", one.bytes, one.size);
  save(directory, "page", "empty.rm", empty.bytes, empty.size);

  const char* ids[] = {"A", "B", "C"};
  rmd_bytes notebook[] = {every, one, {(const uint8_t*)"not a page", 10}};
  archive(directory, "notebook.rmdoc",
          "{\"cPages\":{\"pages\":[{\"id\":\"A\",\"template\":{\"value\":"
          "\"Grid\"}},{\"id\":\"B\"},{\"id\":\"C\"}]}}",
          notebook, ids, 3, nullptr);
  archive(directory, "imported.rmdoc",
          "{\"cPages\":{\"pages\":[{\"id\":\"A\",\"redir\":{\"value\":0}},"
          "{\"id\":\"B\",\"redir\":{\"value\":1}}]}}",
          notebook, ids, 2, &plain);

  rm_bytes_free((uint8_t*)(uintptr_t)every.bytes);
  rm_bytes_free((uint8_t*)(uintptr_t)one.bytes);
  return failures ? 1 : 0;
}
