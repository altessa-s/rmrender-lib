/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_font_cache.h"

#include <errno.h>
#include <glib.h>
#include <glib/gstdio.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

/* Whether path holds only ASCII bytes. */
static bool ascii(const char* path) {
  for (const char* c = path; *c; c++)
    if ((unsigned char)*c >= 0x80) return false;
  return true;
}

#ifdef _WIN32
/* The 8.3 alias of the existing UTF-8 path in UTF-8, or nullptr when the
 * volume has none. */
static char* short_path(const char* path) {
  gunichar2* wide = g_utf8_to_utf16(path, -1, nullptr, nullptr, nullptr);
  if (!wide) return nullptr;
  char* result = nullptr;
  DWORD n = GetShortPathNameW((const wchar_t*)wide, nullptr, 0);
  wchar_t* alias = n ? g_try_new(wchar_t, n) : nullptr;
  if (alias) {
    DWORD m = GetShortPathNameW((const wchar_t*)wide, alias, n);
    if (m && m < n)
      result = g_utf16_to_utf8((const gunichar2*)alias, -1, nullptr, nullptr,
                               nullptr);
  }
  g_free(alias);
  g_free(wide);
  return result;
}
#endif

char* rmr_font_cache_directory(const char* directory) {
  if (!directory || g_mkdir_with_parents(directory, 0700) != 0) return nullptr;
  if (ascii(directory)) return g_strdup(directory);
#ifdef _WIN32
  char* alias = short_path(directory);
  if (alias && ascii(alias)) return alias;
  g_free(alias);
#endif
  return nullptr;
}

/* Whether path is a regular file of exactly size bytes. */
static bool present(const char* path, size_t size) {
  GStatBuf status;
  return g_file_test(path, G_FILE_TEST_IS_REGULAR) &&
         g_stat(path, &status) == 0 && status.st_size >= 0 &&
         (uint64_t)status.st_size == (uint64_t)size;
}

/* The outcome of giving a file a name that may already be taken. */
typedef enum { PLACED, TAKEN, FAILED } placement;

#ifdef _WIN32
/* The calling process's id. */
static unsigned long process(void) { return GetCurrentProcessId(); }

/* Moves the file from to the name to unless that name exists. */
static placement place(const char* from, const char* to) {
  gunichar2* source = g_utf8_to_utf16(from, -1, nullptr, nullptr, nullptr);
  gunichar2* target = g_utf8_to_utf16(to, -1, nullptr, nullptr, nullptr);
  placement result = FAILED;
  if (source && target) {
    if (MoveFileExW((const wchar_t*)source, (const wchar_t*)target,
                    MOVEFILE_WRITE_THROUGH))
      result = PLACED;
    else {
      DWORD error = GetLastError();
      if (error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS)
        result = TAKEN;
    }
  }
  g_free(target);
  g_free(source);
  return result;
}
#else
/* The calling process's id. */
static unsigned long process(void) { return (unsigned long)getpid(); }

/* Links the name to to the file from unless that name exists; unlike rename,
 * link never replaces. The caller removes from. */
static placement place(const char* from, const char* to) {
  if (link(from, to) == 0) return PLACED;
  return errno == EEXIST ? TAKEN : FAILED;
}
#endif

char* rmr_font_cache_file(const char* directory, const uint8_t* bytes,
                          size_t size) {
  if (!directory || !bytes || !size || size > G_MAXSSIZE) return nullptr;
  char* digest = g_compute_checksum_for_data(G_CHECKSUM_SHA256, bytes, size);
  if (!digest) return nullptr;
  char* name = g_strconcat(digest, ".ttf", nullptr);
  char* path = g_build_filename(directory, name, nullptr);
  g_free(name);
  g_free(digest);
  if (present(path, size)) return path;

  /* A file under the final name is complete and never replaced: Windows fails
   * to open a file while it is being replaced, and to replace one that is
   * open. The bytes go to a temporary file unique to this call (the process
   * id separates forks sharing a random state), which takes the final name
   * only if no other writer got there first. GLib opens, writes and closes
   * it, so no descriptor crosses C runtimes. */
  char* temporary =
      g_strdup_printf("%s.%lu-%08x%08x.tmp", path, process(),
                      (unsigned)g_random_int(), (unsigned)g_random_int());
  placement placed = FAILED;
  if (g_file_set_contents_full(temporary, (const gchar*)bytes, (gssize)size,
                               G_FILE_SET_CONTENTS_DURABLE, 0600, nullptr)) {
    placed = place(temporary, path);
    /* Placed files never change size, so a wrong-sized one predates this
     * protocol or was damaged outside it: replace it once. */
    if (placed == TAKEN && !present(path, size) && g_unlink(path) == 0)
      placed = place(temporary, path);
  }
  /* Gone already where placing moved it. */
  g_unlink(temporary);
  g_free(temporary);

  /* A name another writer took holds a complete file that nobody replaces or
   * deletes, so one check suffices and no retry is needed. */
  if (placed == PLACED || (placed == TAKEN && present(path, size))) return path;
  g_free(path);
  return nullptr;
}
