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

enum { WRITERS = 8, ROUNDS = 4 };

/* Writers released together to cache the same bytes in one directory. */
typedef struct {
  GMutex lock;
  GCond start;
  int waiting;
  const char* directory;
  const uint8_t* bytes;
  size_t size;
} race;

/* One writer's result: the path, whether it then read back the expected
 * bytes, why not, and the file's identity right after. */
typedef struct {
  race* race;
  const uint8_t* expected;
  char* path;
  bool intact;
  char failure[256];
  uint64_t inode;
} writer;

/* Waits for every writer, caches the bytes and reads the file straight back,
 * as a font map does: through the C runtime, which on Windows does not share
 * deletion and so fails while a concurrent rename still holds the file. */
static void* cache_concurrently(void* data) {
  writer* w = data;
  race* r = w->race;
  g_mutex_lock(&r->lock);
  if (++r->waiting == WRITERS) g_cond_broadcast(&r->start);
  while (r->waiting < WRITERS) g_cond_wait(&r->start, &r->lock);
  g_mutex_unlock(&r->lock);

  GError* error = nullptr;
  w->path = rmr_font_cache_file(r->directory, r->bytes, r->size, &error);
  GStatBuf status;
  if (w->path && g_stat(w->path, &status) == 0)
    w->inode = (uint64_t)status.st_ino;
  char* contents = nullptr;
  gsize length = 0;
  if (w->path && g_file_get_contents(w->path, &contents, &length, &error)) {
    w->intact = length == r->size && !memcmp(contents, w->expected, r->size);
    if (!w->intact)
      snprintf(w->failure, sizeof w->failure, "read %zu other bytes",
               (size_t)length);
  }
  if (error) snprintf(w->failure, sizeof w->failure, "%s", error->message);
  g_clear_error(&error);
  g_free(contents);
  return nullptr;
}

/* Runs WRITERS concurrent writers of bytes into directory, where a file
 * holding expected may already be, and checks that all of them get the one
 * file, which holds expected, was never replaced and left no temporary file
 * behind. */
static void race_writers(const char* directory, const uint8_t* bytes,
                         const uint8_t* expected, size_t size) {
  race r = {.directory = directory, .bytes = bytes, .size = size};
  g_mutex_init(&r.lock);
  g_cond_init(&r.start);
  writer writers[WRITERS];
  GThread* threads[WRITERS];
  for (int i = 0; i < WRITERS; i++) {
    writers[i] = (writer){.race = &r, .expected = expected};
    threads[i] = g_thread_new("font-cache", cache_concurrently, &writers[i]);
  }
  for (int i = 0; i < WRITERS; i++) g_thread_join(threads[i]);

  GStatBuf status;
  CHECK(writers[0].path && g_stat(writers[0].path, &status) == 0);
  for (int i = 0; i < WRITERS; i++) {
    if (!writers[i].path || !writers[i].intact)
      fprintf(stderr, "writer %d: %s\n", i, writers[i].failure);
    CHECK(writers[i].path && !strcmp(writers[i].path, writers[0].path));
    CHECK(writers[i].intact);
    CHECK(writers[i].inode == (uint64_t)status.st_ino);
  }
  CHECK(holds(writers[0].path, expected, size));
  CHECK(entries(directory) == 1);
  for (int i = 0; i < WRITERS; i++) g_free(writers[i].path);
  g_cond_clear(&r.start);
  g_mutex_clear(&r.lock);
}

/* Concurrent writers on a cold directory and on one whose same-sized file
 * holds other bytes, which must be kept. */
static void concurrency(const char* root) {
  size_t size = 1 << 20;
  uint8_t* bytes = g_malloc(size);
  uint8_t* other = g_malloc(size);
  for (size_t i = 0; i < size; i++) {
    bytes[i] = (uint8_t)(i * 31 + (i >> 11));
    other[i] = (uint8_t)~bytes[i];
  }

  for (int round = 0; round < ROUNDS; round++) {
    char name[32];
    snprintf(name, sizeof name, "race-%d", round);
    char* directory = g_build_filename(root, name, nullptr);
    char* cache = rmr_font_cache_directory(directory);
    CHECK(cache);
    race_writers(cache, bytes, bytes, size);

    /* The name is taken: the existing file is kept, not replaced. */
    char* path = rmr_font_cache_file(cache, bytes, size, nullptr);
    CHECK(path);
    put(path, other, size);
    race_writers(cache, bytes, other, size);

    CHECK(g_unlink(path) == 0 && g_rmdir(cache) == 0);
    g_free(path);
    g_free(cache);
    g_free(directory);
  }
  g_free(other);
  g_free(bytes);
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
  char* path = rmr_font_cache_file(cache, first, sizeof first, nullptr);
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
  char* again = rmr_font_cache_file(cache, first, sizeof first, nullptr);
  CHECK(again && !strcmp(again, path) && holds(again, other, sizeof other));
  g_free(again);

  /* A file of the wrong size is replaced. */
  char* replaced = rmr_font_cache_file(cache, second, sizeof second, nullptr);
  CHECK(replaced && holds(replaced, second, sizeof second));
  put(replaced, second, sizeof second / 2);
  g_free(replaced);
  replaced = rmr_font_cache_file(cache, second, sizeof second, nullptr);
  CHECK(replaced && holds(replaced, second, sizeof second));

  /* No temporary files are left beside the two fonts. */
  CHECK(entries(cache) == 2);

  GError* error = nullptr;
  CHECK(!rmr_font_cache_file(nullptr, first, sizeof first, &error) && error);
  g_clear_error(&error);
  CHECK(!rmr_font_cache_file(cache, nullptr, 1, nullptr));
  CHECK(!rmr_font_cache_file(cache, first, 0, nullptr));

  CHECK(g_unlink(path) == 0 && g_unlink(replaced) == 0);
  concurrency(root);
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

  puts(
      "font cache naming, reuse, replacement, concurrency and directories "
      "passed");
  return 0;
}
