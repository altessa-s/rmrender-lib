/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"

static rmr_status begin(void *context, rmr_rect v) {
    (void)v;
    ++*(int *)context;
    return RMR_OK;
}

static rmr_status draw(void *c, const rmr_draw *d) {
    (void)c;
    (void)d;
    return RMR_OK;
}

static rmr_status end(void *c) {
    (void)c;
    return RMR_OK;
}

static void abort_draw(void *c) { (void)c; }

int main(void) {
    rmr_plan *base = rectangle((rmr_color){0, 0, 0, 1}, RMR_BLEND_NORMAL);
    rmr_draw d = *rmr_plan_draw(base, 0);
    uint8_t bytes[] = {1, 2, 3, 4};
    d.mode = RMR_IMAGE;
    d.image = (rmr_image_view){bytes, sizeof bytes, {64, 0, 0, 64, -32, 0}};
    rmr_rect viewport = rmr_plan_viewport(base);
    rmr_plan *p = nullptr;
    OK(rmr_plan_create(&d, 1, viewport, nullptr, &p, nullptr));
    bytes[0] = 9;
    CHECK(rmr_plan_draw(p, 0)->image.bytes[0] == 1);
    int called = 0;
    rmr_backend b = {
        RMR_BACKEND_ABI, sizeof b, RMR_CAP_FILL | RMR_CAP_STROKE, &called, begin, draw, end,
        abort_draw};
    CHECK(rmr_plan_execute(p, &b, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED && !called);
    b.capabilities = RMR_CAP_IMAGE;
    OK(rmr_plan_execute(p, &b, nullptr, nullptr, nullptr));
    CHECK(called == 1);
    rmr_limits limits = rmr_limits_default();
    size_t required = rmr_plan_bytes(p);
    CHECK(required > sizeof d + sizeof bytes);
    rmr_plan_free(p);
    p = nullptr;
    limits.maximum_plan_bytes = required - 1;
    CHECK(rmr_plan_create(&d, 1, viewport, &limits, &p, nullptr) == RMR_ERR_LIMIT && !p);
    limits.maximum_plan_bytes = required;
    OK(rmr_plan_create(&d, 1, viewport, &limits, &p, nullptr));
    rmr_plan_free(p);
    p = nullptr;
    for (size_t i = 0; i < 4; i++) {
        rmr_test_fail_after(i);
        CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &p, nullptr) == RMR_ERR_OUT_OF_MEMORY &&
              !p);
    }
    rmr_test_fail_after(SIZE_MAX);
    d.image.transform.a = INFINITY;
    CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &p, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    d.image.transform.a = 64;
    d.group = 1;
    CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &p, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    d.group = 0;
    d.image.bytes = nullptr;
    CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &p, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    d.image.bytes = bytes;
    d.mode = RMR_FILL;
    CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &p, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    rmr_plan_free(base);
    puts("image ownership, limits and capability checks passed");
}
