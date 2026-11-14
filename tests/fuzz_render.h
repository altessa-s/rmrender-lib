/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef RMR_FUZZ_RENDER_H
#define RMR_FUZZ_RENDER_H
#include <stddef.h>
#include <stdint.h>

/* What one execution does with its input: an imported PDF exported in one
 * mode, a .rm page rendered as a bitmap, a PDF or an SVG, or an .rmdoc
 * archive exported as a PDF or as SVG pages. */
typedef enum {
  FUZZ_PDF,
  FUZZ_PAGE,
  FUZZ_ARCHIVE,
  FUZZ_OPERATIONS
} fuzz_operation;

/* Inputs above the nightly -max_len are ignored rather than parsed, so a
 * different command line cannot make one execution unexpectedly expensive. */
constexpr size_t FUZZ_MAXIMUM_INPUT = 1048576;

/* FNV-1a of the whole input: its remainder modulo FUZZ_OPERATIONS chooses
 * the operation, the quotient the variant, so every mutation draws both
 * afresh. */
uint32_t fuzz_selector(const uint8_t* data, size_t size);

/* Runs operation on data with the variant drawn from rest. Returns zero; a
 * violated API contract aborts. */
int fuzz_render(const uint8_t* data, size_t size, fuzz_operation operation,
                uint32_t rest);

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);
int LLVMFuzzerInitialize(int* argc, char*** argv);
#endif
