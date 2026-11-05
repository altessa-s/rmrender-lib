/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* UTF-8 paths from the command line. The Windows CRT passes argv in the ANSI
 * code page, which loses characters outside it, and fopen expects that code
 * page; tests take paths from the UTF-16 command line and open them through
 * the wide CRT instead. POSIX passes everything through unchanged. */
#ifndef RMR_TEST_PATHS_H
#define RMR_TEST_PATHS_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* After windows.h, whose types it uses. */
#include <shellapi.h>
#endif
#include <stdio.h>
#include <stdlib.h>

#include "test.h"

#ifdef _WIN32
/* Returns a malloc'd UTF-8 copy of the UTF-16 text. */
static char* test_utf8(const wchar_t* text) {
  int n =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  CHECK(n > 0);
  char* out = malloc((size_t)n);
  CHECK(out && WideCharToMultiByte(CP_UTF8, 0, text, -1, out, n, nullptr,
                                   nullptr) == n);
  return out;
}

/* Returns a malloc'd UTF-16 copy of the UTF-8 text. */
static wchar_t* test_wide(const char* text) {
  int n =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
  CHECK(n > 0);
  wchar_t* out = malloc((size_t)n * sizeof *out);
  CHECK(out && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out,
                                   n) == n);
  return out;
}
#endif

/* The program arguments in UTF-8: argv itself on POSIX, otherwise copies
 * decoded from the UTF-16 command line that live until the process exits. */
static char** test_arguments(int argc, char** argv) {
#ifdef _WIN32
  int count = 0;
  wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
  CHECK(wide && count == argc);
  char** utf8 = calloc((size_t)argc + 1, sizeof *utf8);
  CHECK(utf8);
  for (int i = 0; i < argc; i++) utf8[i] = test_utf8(wide[i]);
  LocalFree(wide);
  (void)argv;
  return utf8;
#else
  (void)argc;
  return argv;
#endif
}

/* fopen for a UTF-8 path. */
static FILE* test_fopen(const char* path, const char* mode) {
#ifdef _WIN32
  wchar_t* wide_path = test_wide(path);
  wchar_t* wide_mode = test_wide(mode);
  FILE* file = _wfopen(wide_path, wide_mode);
  free(wide_mode);
  free(wide_path);
  return file;
#else
  return fopen(path, mode);
#endif
}

#endif
