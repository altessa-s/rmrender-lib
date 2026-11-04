#ifdef __linux__
#define _POSIX_C_SOURCE 200809L
#endif

/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <rmrender/rmlines.h>
#include <sys/stat.h>
#ifdef _WIN32
#include "windows_files.h"
#else
#include <unistd.h>
#endif

#include "test.h"

static void write_template(const char* directory, const char* name,
                           const char* json) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.template", directory, name);
#ifdef _WIN32
  wchar_t wide[512];
  CHECK(
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide, 512));
  FILE* file = _wfopen(wide, L"wb");
#else
  FILE* file = fopen(path, "wb");
#endif
  CHECK(file);
  CHECK(fwrite(json, 1, strlen(json), file) == strlen(json));
  CHECK(fclose(file) == 0);
}

int main(void) {
#ifdef _WIN32
  char root[256];
  test_windows_temp_directory(root, sizeof root);
#else
  char root[] = "/tmp/rmr-template-XXXXXX";
  CHECK(mkdtemp(root));
#endif

  char custom[256], bundled[256];
  snprintf(custom, sizeof custom, "%s/\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82", root);
  snprintf(bundled, sizeof bundled, "%s/bundled", root);
  CHECK(mkdir(custom, 0700) == 0 && mkdir(bundled, 0700) == 0);

  write_template(custom, "Paper", "{\"name\":\"Own\",\"items\":[]}");
  write_template(bundled, "Paper",
                 "{\"name\":\"Own\",\"items\":[{\"type\":\"path\",\"data\":["
                 "\"M\",0,0,\"L\",10,10]}]}");
  write_template(bundled, "Fallback", "{\"name\":\"Own\",\"items\":[]}");

  const char* directories[] = {custom, bundled};
  rmr_template_store* store = nullptr;
  OK(rmr_template_store_create_layered(directories, 2, nullptr, &store,
                                       nullptr));
  const rmr_template* model = nullptr;
  OK(rmr_template_store_resolve(store, (rm_str){"Paper", 5}, &model));
  CHECK(model && model->count == 0);
  OK(rmr_template_store_resolve(store, (rm_str){"Fallback", 8}, &model));
  CHECK(model && model->count == 0);
  OK(rmr_template_store_resolve(store, (rm_str){"Missing", 7}, &model));
  CHECK(!model);

  CHECK(rmr_template_store_resolve(store, (rm_str){"../Paper", 8}, &model) ==
        RMR_ERR_INVALID_ARGUMENT);

  /* Names Win32 would not open as plain files are rejected there; POSIX
   * looks them up like any other missing template. */
  const char* reserved[] = {"CON",    "nul",  "Aux",    "prn.x",       "Com1",
                            "LPT9.x", "com0", "AUX .x", "COM\xC2\xB9", "a:b",
                            "a<b",    "a>b",  "a\"b",   "a|b",         "a?b",
                            "a*b",    "a\tb", "Paper.", "Paper "};
  for (size_t i = 0; i < sizeof reserved / sizeof *reserved; i++) {
    rm_str name = {reserved[i], strlen(reserved[i])};
#ifdef _WIN32
    CHECK(rmr_template_store_resolve(store, name, &model) ==
          RMR_ERR_INVALID_ARGUMENT);
#else
    OK(rmr_template_store_resolve(store, name, &model));
    CHECK(!model);
#endif
  }
  /* Device-like names that are plain files everywhere. */
  const char* plain[] = {"CONSOLE", "COM10",     "Null",
                         "LPT",     "con-paper", "aux_lines"};
  for (size_t i = 0; i < sizeof plain / sizeof *plain; i++) {
    OK(rmr_template_store_resolve(store, (rm_str){plain[i], strlen(plain[i])},
                                  &model));
    CHECK(!model);
  }
  write_template(custom, "Paper", "invalid");
  CHECK(rmr_template_store_resolve(store, (rm_str){"Paper", 5}, &model) !=
        RMR_OK);
  rmr_template_store_free(store);

  OK(rmr_template_store_create_layered(nullptr, 0, nullptr, &store, nullptr));
  OK(rmr_template_store_resolve(store, (rm_str){"Paper", 5}, &model));
  CHECK(!model);
  rmr_template_store_free(store);

  rmr_limits limits = rmr_limits_default();
  limits.maximum_plan_bytes = 1;
  store = (rmr_template_store*)(uintptr_t)1;
  CHECK(rmr_template_store_create_layered(directories, 2, &limits, &store,
                                          nullptr) == RMR_ERR_LIMIT);
  CHECK(store == (rmr_template_store*)(uintptr_t)1);

  const char* paths[] = {"\xD0\xA2\xD0\xB5\xD1\x81\xD1\x82/Paper",
                         "bundled/Paper", "bundled/Fallback"};
  for (size_t i = 0; i < 3; i++) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.template", root, paths[i]);
    CHECK(unlink(path) == 0);
  }
  CHECK(rmdir(custom) == 0 && rmdir(bundled) == 0 && rmdir(root) == 0);

  puts("template source precedence, fallback, blank source and limits passed");
  return 0;
}
