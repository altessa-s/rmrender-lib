/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef FONTCONFIG_FIXTURE_H
#define FONTCONFIG_FIXTURE_H
#include <glib.h>
#include <glib/gstdio.h>

#include "test.h"

/* The pinned POSIX stack uses bundled fonts for default Pango text too, so
 * host font packages cannot change glyphs or introduce hex boxes. Windows
 * exercises the installed Fontconfig configuration and its cache paths. */
static char* test_fontconfig_directory(const char* directory) {
#ifdef _WIN32
  (void)directory;
  return nullptr;
#else
  char* root = g_dir_make_tmp("rmr-fontconfig-XXXXXX", nullptr);
  CHECK(root);
  char* absolute = g_canonicalize_filename(directory, nullptr);
  CHECK(g_file_test(absolute, G_FILE_TEST_IS_DIR));
  char* fonts = g_markup_escape_text(absolute, -1);
  char* cache = g_markup_escape_text(root, -1);
  char* xml = g_strdup_printf(
      "<fontconfig><dir>%s</dir><cachedir>%s</cachedir></fontconfig>", fonts,
      cache);
  char* path = g_build_filename(root, "fonts.conf", nullptr);
  CHECK(g_file_set_contents(path, xml, -1, nullptr));
  CHECK(g_setenv("FONTCONFIG_FILE", path, true));
  g_free(path);
  g_free(xml);
  g_free(cache);
  g_free(fonts);
  g_free(absolute);
  return root;
#endif
}

/* Called after Pango and Fontconfig release their maps and mapped caches. */
static void test_fontconfig_free(char* root) {
  if (!root) return;
  GDir* dir = g_dir_open(root, 0, nullptr);
  CHECK(dir);
  for (const char* name; (name = g_dir_read_name(dir));) {
    char* path = g_build_filename(root, name, nullptr);
    CHECK(g_unlink(path) == 0);
    g_free(path);
  }
  g_dir_close(dir);
  CHECK(g_rmdir(root) == 0);
  g_free(root);
}
#endif
