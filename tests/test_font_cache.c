/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* The content-addressed font cache the Cairo backend uses on Windows. */
#include <glib.h>
#include <glib/gstdio.h>

#include "../backends/linux/rmr_font_cache.h"
#include "test.h"

/* Whether path holds only ASCII bytes. */
static bool ascii(const char* path) {
  for (const char* c = path; *c; c++)
    if ((unsigned char)*c >= 0x80) return false;
  return true;
}

/* Whether the file at path holds exactly size bytes equal to bytes. */
static bool holds(const char* path, const uint8_t* bytes, size_t size) {
  char* data = nullptr;
  gsize length = 0;
  bool same = g_file_get_contents(path, &data, &length, nullptr) &&
              length == size && !memcmp(data, bytes, size);
  g_free(data);
  return same;
}

/* Writes size bytes to path, replacing it. */
static void put(const char* path, const uint8_t* bytes, size_t size) {
  CHECK(g_file_set_contents(path, (const gchar*)bytes, (gssize)size, nullptr));
}

/* The number of entries in directory. */
static size_t entries(const char* directory) {
  GDir* dir = g_dir_open(directory, 0, nullptr);
  CHECK(dir);
  size_t count = 0;
  while (g_dir_read_name(dir)) count++;
  g_dir_close(dir);
  return count;
}

int main(void) {
  char* root = g_dir_make_tmp("rmr-font-cache-XXXXXX", nullptr);
  CHECK(root);
  char* fonts = g_build_filename(root, "rmrender", "fonts", nullptr);

  /* Missing parents are created; an ASCII path is used as is. */
  char* cache = rmr_font_cache_directory(fonts);
  CHECK(cache && ascii(cache));
  CHECK(g_file_test(fonts, G_FILE_TEST_IS_DIR));
  if (ascii(fonts)) CHECK(!strcmp(cache, fonts));
  CHECK(!rmr_font_cache_directory(nullptr));

  /* Only Windows has an ASCII alias for a non-ASCII directory. */
  char* cyrillic =
      g_build_filename(root, "\xD0\xA8\xD1\x80\xD0\xB8\xD1\x84", nullptr);
  char* alias = rmr_font_cache_directory(cyrillic);
  CHECK(g_file_test(cyrillic, G_FILE_TEST_IS_DIR));
#ifdef _WIN32
  CHECK(!alias || ascii(alias));
#else
  CHECK(!alias);
#endif
  g_free(alias);

  uint8_t first[4096], second[1000], other[4096];
  for (size_t i = 0; i < sizeof first; i++) first[i] = (uint8_t)(i * 7 + 1);
  for (size_t i = 0; i < sizeof second; i++) second[i] = (uint8_t)(i ^ 0x5A);
  memset(other, 0xEE, sizeof other);

  /* The file is named by the SHA-256 of its bytes. */
  char* path = rmr_font_cache_file(cache, first, sizeof first);
  CHECK(path && holds(path, first, sizeof first));
  char* digest =
      g_compute_checksum_for_data(G_CHECKSUM_SHA256, first, sizeof first);
  char* name = g_strconcat(digest, ".ttf", nullptr);
  char* base = g_path_get_basename(path);
  CHECK(!strcmp(base, name));
  g_free(base);
  g_free(name);
  g_free(digest);

  /* A present file of the right size is reused, never rewritten. */
  put(path, other, sizeof other);
  char* again = rmr_font_cache_file(cache, first, sizeof first);
  CHECK(again && !strcmp(again, path) && holds(again, other, sizeof other));
  g_free(again);

  /* A file of the wrong size is replaced. */
  char* replaced = rmr_font_cache_file(cache, second, sizeof second);
  CHECK(replaced && holds(replaced, second, sizeof second));
  put(replaced, second, sizeof second / 2);
  g_free(replaced);
  replaced = rmr_font_cache_file(cache, second, sizeof second);
  CHECK(replaced && holds(replaced, second, sizeof second));

  /* No temporary files are left beside the two fonts. */
  CHECK(entries(cache) == 2);

  CHECK(!rmr_font_cache_file(nullptr, first, sizeof first));
  CHECK(!rmr_font_cache_file(cache, nullptr, 1));
  CHECK(!rmr_font_cache_file(cache, first, 0));

  CHECK(g_unlink(path) == 0 && g_unlink(replaced) == 0);
  CHECK(g_rmdir(cyrillic) == 0 && g_rmdir(fonts) == 0);
  char* parent = g_path_get_dirname(fonts);
  CHECK(g_rmdir(parent) == 0 && g_rmdir(root) == 0);
  g_free(parent);
  g_free(replaced);
  g_free(path);
  g_free(cyrillic);
  g_free(cache);
  g_free(fonts);
  g_free(root);

  puts("font cache naming, reuse, replacement and directories passed");
  return 0;
}
