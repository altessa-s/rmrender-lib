/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Replays files through the fuzz target without libFuzzer, so the ordinary
 * suites keep every operation of the target building and running clean under
 * whatever sanitizers the configuration uses.
 *
 *   fuzz_replay pdf|page|archive|hash path...
 *
 * A named operation runs every file through it, each variant drawn from the
 * file's hash, as libFuzzer draws it; `hash` lets the hash choose the
 * operation too. Each file is replayed under several variants. */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fuzz_render.h"

static size_t executions;
static int operation = -1; /* a fuzz_operation, or -1 for the hash's */

/* Variants per file under a named operation: the hash's own, then others
 * that differ from it in every field, reaching the export modes, the output,
 * the refusals and the tight limit. */
enum { VARIANTS = 12 };

static bool replay_file(const char* path) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  uint8_t* data = nullptr;
  size_t size = 0, capacity = 0;
  for (;;) {
    if (size == capacity) {
      capacity = capacity ? capacity * 2 : 65536;
      uint8_t* grown = realloc(data, capacity);
      if (!grown) break;
      data = grown;
    }
    size_t read = fread(data + size, 1, capacity - size, f);
    size += read;
    if (!read) break;
  }
  bool ok = !ferror(f) && size < capacity;
  if (fclose(f)) ok = false;
  if (ok) {
    uint32_t h = fuzz_selector(data, size);
    if (operation < 0) {
      (void)LLVMFuzzerTestOneInput(data, size);
      executions++;
    } else
      for (uint32_t v = 0; v < VARIANTS; v++) {
        uint32_t rest = h / FUZZ_OPERATIONS ^ v * 2654435769u;
        (void)fuzz_render(data, size, (fuzz_operation)operation, rest);
        executions++;
      }
  }
  free(data);
  return ok;
}

/* Directories are read one level deep, as libFuzzer reads a corpus. */
static bool replay(const char* path) {
  DIR* directory = opendir(path);
  if (!directory) return errno == ENOTDIR && replay_file(path);
  bool ok = true;
  for (struct dirent* entry; ok && (entry = readdir(directory));) {
    if (entry->d_name[0] == '.') continue;
    size_t length = strlen(path) + strlen(entry->d_name) + 2;
    char* child = malloc(length);
    if (!child) {
      ok = false;
      break;
    }
    snprintf(child, length, "%s/%s", path, entry->d_name);
    DIR* nested = opendir(child);
    if (nested)
      ok = !closedir(nested);
    else
      ok = replay_file(child);
    if (!ok) fprintf(stderr, "cannot replay %s\n", child);
    free(child);
  }
  return !closedir(directory) && ok;
}

int main(int argc, char** argv) {
  static const char* const names[FUZZ_OPERATIONS] = {"pdf", "page", "archive"};
  if (argc < 3) {
    fprintf(stderr, "usage: %s pdf|page|archive|hash path...\n", argv[0]);
    return 2;
  }
  for (int i = 0; i < FUZZ_OPERATIONS; i++)
    if (!strcmp(argv[1], names[i])) operation = i;
  if (operation < 0 && strcmp(argv[1], "hash")) return 2;
  for (int i = 2; i < argc; i++)
    if (!replay(argv[i])) {
      fprintf(stderr, "cannot replay %s\n", argv[i]);
      return 1;
    }
  if (!executions) return 1;
  printf("%zu executions with %s\n", executions, argv[1]);
  return 0;
}
