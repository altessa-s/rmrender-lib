/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "test.h"
#include <rmrender/rmlines.h>
#include <sys/stat.h>
#include <unistd.h>
static void write_template(const char *directory, const char *name, const char *json) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.template", directory, name);
    FILE *file = fopen(path, "wb");
    CHECK(file);
    CHECK(fwrite(json, 1, strlen(json), file) == strlen(json));
    CHECK(fclose(file) == 0);
}
int main(void) {
    char root[] = "/tmp/rmr-template-XXXXXX";
    CHECK(mkdtemp(root));
    char custom[256], bundled[256];
    snprintf(custom, sizeof custom, "%s/custom", root);
    snprintf(bundled, sizeof bundled, "%s/bundled", root);
    CHECK(mkdir(custom, 0700) == 0 && mkdir(bundled, 0700) == 0);
    write_template(custom, "Paper", "{\"name\":\"Own\",\"items\":[]}");
    write_template(
        bundled, "Paper",
        "{\"name\":\"Own\",\"items\":[{\"type\":\"path\",\"data\":[\"M\",0,0,\"L\",10,10]}]}");
    write_template(bundled, "Fallback", "{\"name\":\"Own\",\"items\":[]}");
    const char *directories[] = {custom, bundled};
    rmr_template_store *store = nullptr;
    OK(rmr_template_store_create_layered(directories, 2, nullptr, &store, nullptr));
    const rmr_template *model = nullptr;
    OK(rmr_template_store_resolve(store, (rm_str){"Paper", 5}, &model));
    CHECK(model && model->count == 0);
    OK(rmr_template_store_resolve(store, (rm_str){"Fallback", 8}, &model));
    CHECK(model && model->count == 0);
    OK(rmr_template_store_resolve(store, (rm_str){"Missing", 7}, &model));
    CHECK(!model);
    CHECK(rmr_template_store_resolve(store, (rm_str){"../Paper", 8}, &model) ==
          RMR_ERR_INVALID_ARGUMENT);
    write_template(custom, "Paper", "invalid");
    CHECK(rmr_template_store_resolve(store, (rm_str){"Paper", 5}, &model) != RMR_OK);
    rmr_template_store_free(store);
    OK(rmr_template_store_create_layered(nullptr, 0, nullptr, &store, nullptr));
    OK(rmr_template_store_resolve(store, (rm_str){"Paper", 5}, &model));
    CHECK(!model);
    rmr_template_store_free(store);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_plan_bytes = 1;
    store = (rmr_template_store *)(uintptr_t)1;
    CHECK(rmr_template_store_create_layered(directories, 2, &limits, &store, nullptr) ==
          RMR_ERR_LIMIT);
    CHECK(store == (rmr_template_store *)(uintptr_t)1);
    const char *paths[] = {"custom/Paper", "bundled/Paper", "bundled/Fallback"};
    for (size_t i = 0; i < 3; i++) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.template", root, paths[i]);
        CHECK(unlink(path) == 0);
    }
    CHECK(rmdir(custom) == 0 && rmdir(bundled) == 0 && rmdir(root) == 0);
    puts("template source precedence, fallback, blank source and limits passed");
    return 0;
}
