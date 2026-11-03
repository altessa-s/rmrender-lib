/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"
#include <rmrender/template.h>

static const char fixture[] =
    "{\"name\":\"Own \\u041b\\u0438\\u043d\\u0438\\u0438 \\ud83d\\udcd3\","
    "\"orientation\":\"landscape\",\"categories\":[\"Lines\",\"Own\"],"
    "\"constants\":[{\"step\":2e1},{\"w\":\"templateWidth - step\"}],"
    "\"items\":[{\"type\":\"group\",\"boundingBox\":{\"x\":10,\"y\":5,\"height\":\"step\"},"
    "\"repeat\":{\"rows\":\"down\"},\"children\":[{\"type\":\"path\","
    "\"data\":[\"M\",0,0,\"L\",\"w\",0],\"strokeWidth\":1,\"strokeColor\":\"#12345680\"}]}]}";

static rmr_status decode(const char *s, rmr_template_document **d) {
    return rmr_template_document_decode(s, strlen(s), nullptr, d, nullptr);
}

static void reject(const char *s, rmr_status status) {
    rmr_template_document *sentinel = (rmr_template_document *)(uintptr_t)1;
    CHECK(decode(s, &sentinel) == status);
    CHECK(sentinel == (rmr_template_document *)(uintptr_t)1);
}

int main(void) {
    rmr_template_document *d = nullptr;
    char borrowed[sizeof fixture];
    memcpy(borrowed, fixture, sizeof fixture);
    OK(decode(borrowed, &d));
    memset(borrowed, 0, sizeof borrowed);

    CHECK(!strcmp(rmr_template_document_name(d), "Own Линии 📓"));
    CHECK(!strcmp(rmr_template_document_orientation(d), "landscape"));
    CHECK(rmr_template_document_category_count(d) == 2);
    CHECK(!strcmp(rmr_template_document_category(d, 1), "Own"));
    CHECK(!rmr_template_document_category(d, 2));

    rmr_plan *p = nullptr;
    OK(rmr_template_plan(rmr_template_document_model(d), (rmr_point){100, 200},
                         (rmr_point){100, 300}, (rmr_rect){-50, 0, 100, 300}, nullptr, &p,
                         nullptr));
    rmr_template_document_free(d);
    CHECK(rmr_plan_count(p) == 15);
    const rmr_draw *draw = rmr_plan_draw(p, 14);
    CHECK(draw->path.elements[1].end.x == 40 && draw->path.elements[1].end.y == 285);
    CHECK(draw->color.red == 18.0 / 255 && draw->color.alpha == 128.0 / 255);
    rmr_plan_free(p);

    OK(decode("{\"name\":\"empty\",\"orientation\":null,\"constants\":null,\"items\":[],\"future\":"
              "{\"ok\":[true,false,null]}}",
              &d));
    CHECK(!rmr_template_document_orientation(d));
    rmr_template_document_free(d);

    const char *invalid[] = {
        "",
        "null",
        "[]",
        "{}",
        "{\"name\":2,\"items\":[]}",
        "{\"name\":\"x\",\"items\":[],}",
        "{\"name\":\"x\",\"items\":[]}x",
        "{\"name\":\"x\",\"name\":\"y\",\"items\":[]}",
        "{\"name\":\"x\",\"na\\u006de\":\"y\",\"items\":[]}",
        "{\"name\":\"\\ud800\",\"items\":[]}",
        "{\"name\":\"\\udc00\",\"items\":[]}",
        "{\"name\":\"\\ud800\\u0041\",\"items\":[]}",
        "{\"name\":\"\\u0000\",\"items\":[]}",
        "{\"name\":\"\\x\",\"items\":[]}",
        "{\"name\":\"\xc0\xaf\",\"items\":[]}",
        "{\"name\":\"x\",\"items\":[],\"n\":01}",
        "{\"name\":\"x\",\"items\":[],\"n\":1e999}",
        "{\"name\":\"x\",\"items\":[],\"n\":1.}",
        "{\"name\":\"x\",\"items\":[],\"n\":1e+}",
        "{\"name\":\"x\",\"items\":[],\"constants\":[{\"x\":1,\"y\":2}]}",
        "{\"name\":\"x\",\"items\":[],\"constants\":[{\"!\":1}]}",
        "{\"name\":\"x\",\"items\":[{\"type\":\"path\",\"data\":[\"M\",0]}]}",
        "{\"name\":\"x\",\"items\":[{\"type\":\"path\",\"data\":[\"L\",0,0]}]}",
        "{\"name\":\"x\",\"items\":[{\"type\":\"path\",\"data\":[],\"fillColor\":\"oops\"}]}",
        "{\"name\":\"x\",\"items\":[{\"type\":\"group\",\"children\":[],\"repeat\":false}]}",
        "{\"name\":\"x\",\"items\":[{\"type\":\"path\",\"data\":[\"M\",\"1 +\",0]}]}"};
    for (size_t i = 0; i < sizeof invalid / sizeof *invalid; i++)
        reject(invalid[i], RMR_ERR_INVALID_ARGUMENT);

    reject("{\"name\":\"x\",\"items\":[{\"type\":\"unknown\",\"text\":\"x\"}]}",
           RMR_ERR_UNSUPPORTED);
    reject("{\"name\":\"x\",\"items\":[{\"type\":\"path\",\"data\":[\"m\",0,0]}]}",
           RMR_ERR_UNSUPPORTED);

    /* Every truncated prefix must fail and preserve the output. */
    for (size_t i = 0; i < strlen(fixture); i++) {
        d = nullptr;
        CHECK(rmr_template_document_decode(fixture, i, nullptr, &d, nullptr) != RMR_OK);
        CHECK(!d);
    }

    size_t attempts = 0;
    for (; attempts < 1000; attempts++) {
        rmr_test_fail_after(attempts);
        d = nullptr;
        rmr_status s = decode(fixture, &d);
        if (s == RMR_OK) {
            rmr_template_document_free(d);
            break;
        }
        CHECK(s == RMR_ERR_OUT_OF_MEMORY && !d);
    }
    rmr_test_fail_after(SIZE_MAX);
    CHECK(attempts > 30 && attempts < 1000);

    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 5;
    d = nullptr;
    CHECK(rmr_template_document_decode(fixture, strlen(fixture), &limits, &d, nullptr) ==
              RMR_ERR_LIMIT &&
          !d);

    limits = rmr_limits_default();
    limits.maximum_plan_bytes = strlen(fixture);
    CHECK(rmr_template_document_decode(fixture, strlen(fixture), &limits, &d, nullptr) ==
              RMR_ERR_LIMIT &&
          !d);

    char deep[400];
    memset(deep, '[', 150);
    memset(deep + 150, ']', 150);
    deep[300] = 0;
    reject(deep, RMR_ERR_LIMIT);

    /* Deterministic mutation smoke: success is safe to destroy; failures never publish. */
    uint32_t state = 123;
    for (size_t i = 0; i < 3000; i++) {
        char mutated[sizeof fixture];
        memcpy(mutated, fixture, sizeof fixture);
        state = state * 1664525u + 1013904223u;
        size_t at = state % (sizeof fixture - 1);
        state = state * 1664525u + 1013904223u;
        mutated[at] = (char)(state >> 24);
        d = nullptr;
        rmr_status s =
            rmr_template_document_decode(mutated, sizeof fixture - 1, nullptr, &d, nullptr);
        if (s == RMR_OK)
            rmr_template_document_free(d);
        else
            CHECK(!d);
    }

    CHECK(!rmr_template_document_model(nullptr));
    rmr_template_document_free(nullptr);
    return 0;
}
