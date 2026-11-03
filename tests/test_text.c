/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"
#include <rmrender/template.h>

static rmr_status measure(void *context, const char *utf8, size_t size, double font_size,
                          rmr_text_metrics *out) {
    (void)utf8;
    (void)font_size;
    ++*(int *)context;
    *out = (rmr_text_metrics){(double)size * 5, {0, -8, (double)size * 5, 10}};
    return RMR_OK;
}

static rmr_status bad_measure(void *context, const char *utf8, size_t size, double font_size,
                              rmr_text_metrics *out) {
    (void)context;
    (void)utf8;
    (void)size;
    (void)font_size;
    *out = (rmr_text_metrics){.width = NAN};
    return RMR_OK;
}

static rmr_status backend_begin(void *ctx, rmr_rect viewport) {
    (void)viewport;
    ++*(int *)ctx;
    return RMR_OK;
}

static rmr_status backend_draw(void *ctx, const rmr_draw *d) {
    (void)ctx;
    (void)d;
    return RMR_OK;
}

static rmr_status backend_end(void *ctx) {
    (void)ctx;
    return RMR_OK;
}

static void backend_abort(void *ctx) { (void)ctx; }

static rmr_status build(const rmr_template *t, const rmr_text_provider *provider, rmr_plan **out) {
    return rmr_template_plan_with_text(t, (rmr_point){100, 100}, (rmr_point){100, 100},
                                       (rmr_rect){-50, 0, 100, 100}, provider, nullptr, out,
                                       nullptr);
}

int main(void) {
    const char json[] =
        "{\"name\":\"Own\",\"constants\":[{\"textWidth\":100}],\"items\":[{\"type\":\"text\","
        "\"text\":\"Test\",\"fontSize\":\"textWidth / 10\",\"position\":{\"x\":\"(parentWidth - "
        "textWidth) / 2\",\"y\":30}},{\"type\":\"text\",\"text\":\"xy\",\"fontSize\":\"textWidth / "
        "10\",\"position\":{\"x\":\"textWidth\",\"y\":50}}]}";
    rmr_template_document *doc = nullptr;
    rmr_error untouched = {.code = RMR_ERR_BACKEND, .index = 123};
    OK(rmr_template_document_decode(json, strlen(json), nullptr, &doc, &untouched));
    CHECK(untouched.code == RMR_ERR_BACKEND && untouched.index == 123);
    const rmr_template *t = rmr_template_document_model(doc);
    rmr_plan *p = nullptr;
    CHECK(build(t, nullptr, &p) == RMR_ERR_UNSUPPORTED && !p);
    int calls = 0;
    rmr_text_provider provider = {RMR_TEXT_PROVIDER_ABI, sizeof provider, &calls, measure};
    OK(build(t, &provider, &p));
    CHECK(calls == 4);
    CHECK(rmr_plan_count(p) == 2);
    const rmr_draw *d = rmr_plan_draw(p, 0);
    CHECK(d->mode == RMR_TEXT && d->text.font_size == 10 && d->text.origin.x == -10 &&
          d->text.origin.y == 30);
    CHECK(rmr_plan_draw(p, 1)->text.font_size == 10 && rmr_plan_draw(p, 1)->text.origin.x == -40);
    rmr_rect bounds = rmr_plan_bounds(p);
    CHECK(bounds.x == -40 && bounds.y == 22 && bounds.width == 50 && bounds.height == 30);
    rmr_plan *copy = nullptr;
    OK(rmr_template_background_plan_with_text(nullptr, (rmr_point){0}, (rmr_point){0}, p, false,
                                              nullptr, nullptr, &copy, nullptr));
    rmr_template_document_free(doc);
    rmr_plan_free(p);
    CHECK(!memcmp(rmr_plan_draw(copy, 0)->text.utf8, "Test", 4));
    calls = 0;
    rmr_backend backend = {RMR_BACKEND_ABI, sizeof backend, 0,           &calls,
                           backend_begin,   backend_draw,   backend_end, backend_abort};
    CHECK(rmr_plan_execute(copy, &backend, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED &&
          calls == 0);
    backend.capabilities = RMR_CAP_TEXT;
    OK(rmr_plan_execute(copy, &backend, nullptr, nullptr, nullptr));
    CHECK(calls == 1);
    rmr_draw invalid = *rmr_plan_draw(copy, 0);
    invalid.text.utf8 = "\xc0\xaf";
    invalid.text.size = 2;
    p = nullptr;
    CHECK(rmr_plan_create(&invalid, 1, (rmr_rect){-50, 0, 100, 100}, nullptr, &p, nullptr) ==
              RMR_ERR_INVALID_ARGUMENT &&
          !p);
    invalid = *rmr_plan_draw(copy, 0);
    invalid.text.size = 65537;
    CHECK(rmr_plan_create(&invalid, 1, (rmr_rect){-50, 0, 100, 100}, nullptr, &p, nullptr) ==
          RMR_ERR_LIMIT);
    invalid = *rmr_plan_draw(copy, 0);
    invalid.group = 1;
    CHECK(rmr_plan_create(&invalid, 1, (rmr_rect){-50, 0, 100, 100}, nullptr, &p, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    rmr_plan_free(copy);
    rmr_template_item label = {.kind = RMR_TEMPLATE_TEXT, .text = {.utf8 = "A", .size = 1}};
    rmr_template model = {.items = &label, .count = 1};
    provider.abi_version = 0;
    CHECK(build(&model, &provider, &p) == RMR_ERR_INVALID_ARGUMENT);
    provider.abi_version = RMR_TEXT_PROVIDER_ABI;
    provider.measure = bad_measure;
    CHECK(build(&model, &provider, &p) == RMR_ERR_BACKEND);
    provider.measure = measure;
    label.text.has_font_size = true;
    label.text.font_size.number = 0;
    CHECK(build(&model, &provider, &p) == RMR_ERR_INVALID_ARGUMENT);
    label.text.has_font_size = false;
    size_t attempts = 0;
    for (; attempts < 100; attempts++) {
        rmr_test_fail_after(attempts);
        p = nullptr;
        rmr_status s = build(&model, &provider, &p);
        if (s == RMR_OK) {
            rmr_plan_free(p);
            break;
        }
        CHECK(s == RMR_ERR_OUT_OF_MEMORY && !p);
    }
    rmr_test_fail_after(SIZE_MAX);
    CHECK(attempts > 2 && attempts < 100);
    rmr_limits l = rmr_limits_default();
    l.maximum_plan_bytes = 1;
    p = nullptr;
    CHECK(rmr_template_plan_with_text(&model, (rmr_point){100, 100}, (rmr_point){100, 100},
                                      (rmr_rect){-50, 0, 100, 100}, &provider, &l, &p,
                                      nullptr) == RMR_ERR_LIMIT &&
          !p);
    /* Six built-ins plus 250 constants leave no extra binding slot. */
    char names[250][16];
    rmr_template_constant constants[250] = {0};
    for (size_t i = 0; i < 250; i++) {
        snprintf(names[i], sizeof names[i], "k%zu", i);
        constants[i] = (rmr_template_constant){names[i], strlen(names[i]), {.number = 1}};
    }
    model.constants = constants;
    model.constant_count = 250;
    p = nullptr;
    CHECK(build(&model, &provider, &p) == RMR_ERR_LIMIT && !p);
    constants[0].name = "textWidth";
    constants[0].length = 9;
    OK(build(&model, &provider, &p));
    rmr_plan_free(p);
    return 0;
}
