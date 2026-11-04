/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Windows filesystem helpers for the UTF-8 resource-path regression test. */
#ifndef RMR_TEST_WINDOWS_FILES_H
#define RMR_TEST_WINDOWS_FILES_H
#define WIN32_LEAN_AND_MEAN
#include <direct.h>
#include <io.h>
#include <limits.h>
#include <windows.h>

#include "test.h"

/* Creates a fresh directory under %TEMP% and stores its UTF-8 path in out,
 * which holds capacity bytes. */
static void test_windows_temp_directory(char* out, size_t capacity) {
  wchar_t base[MAX_PATH], path[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, base);
  CHECK(n && n < MAX_PATH);
  CHECK(GetTempFileNameW(base, L"rmr", 0, path));
  CHECK(DeleteFileW(path));
  CHECK(CreateDirectoryW(path, nullptr));
  CHECK(capacity <= INT_MAX);
  CHECK(WideCharToMultiByte(CP_UTF8, 0, path, -1, out, (int)capacity, nullptr,
                            nullptr));
}

/* mkdir for a UTF-8 path; Windows has no permission mode. */
static int test_windows_mkdir(const char* path, unsigned mode) {
  (void)mode;
  wchar_t wide[512];
  CHECK(
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 512));
  return _wmkdir(wide);
}

/* unlink for a UTF-8 path. */
static int test_windows_unlink(const char* path) {
  wchar_t wide[512];
  CHECK(
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 512));
  return _wunlink(wide);
}

/* rmdir for a UTF-8 path. */
static int test_windows_rmdir(const char* path) {
  wchar_t wide[512];
  CHECK(
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 512));
  return _wrmdir(wide);
}

/* The POSIX calls the test uses, routed through the UTF-8 helpers. */
#define mkdir test_windows_mkdir
#define unlink test_windows_unlink
#define rmdir test_windows_rmdir
#endif
