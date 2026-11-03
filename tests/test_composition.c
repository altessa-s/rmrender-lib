/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"

static rmr_status begin(void *ctx, rmr_rect v) {
    (void)v;
    (*(int *)ctx)++;
    return RMR_OK;
}

static rmr_status draw(void *ctx, const rmr_draw *d) {
    (void)ctx;
    (void)d;
    return RMR_OK;
}

static rmr_status end(void *ctx) {
    (void)ctx;
    return RMR_OK;
}

static void abort_draw(void *ctx) { (void)ctx; }

int main(void) {
    rmr_plan *base = rectangle((rmr_color){0, 0, 0, 1}, RMR_BLEND_NORMAL);
    rmr_element cut[] = {{.kind = RMR_MOVE, .end = {-2, 0}},
                         {.kind = RMR_LINE, .end = {2, 0}},
                         {.kind = RMR_LINE, .end = {2, 64}},
                         {.kind = RMR_LINE, .end = {-2, 64}},
                         {.kind = RMR_CLOSE}};
    rmr_path_view view = {cut, 5};
    rmr_draw d = *rmr_plan_draw(base, 0);
    d.erasers = &view;
    d.eraser_count = 1;
    d.group = 1;
    d.group_alpha = .25;
    d.group_blend = RMR_BLEND_DARKEN;

    rmr_plan *plan = nullptr;
    rmr_rect viewport = rmr_plan_viewport(base);
    OK(rmr_plan_create(&d, 1, viewport, nullptr, &plan, nullptr));

    cut[0].end.x = -3;
    CHECK(rmr_plan_draw(plan, 0)->erasers[0].elements[0].end.x == -2);

    int called = 0;
    rmr_backend backend = {
        RMR_BACKEND_ABI, sizeof backend, RMR_CAP_FILL | RMR_CAP_DARKEN, &called, begin, draw, end,
        abort_draw};
    CHECK(rmr_plan_execute(plan, &backend, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED &&
          !called);
    backend.capabilities |= RMR_CAP_GROUP;
    CHECK(rmr_plan_execute(plan, &backend, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED &&
          !called);
    backend.capabilities |= RMR_CAP_ERASE;
    OK(rmr_plan_execute(plan, &backend, nullptr, nullptr, nullptr));
    CHECK(called == 1);

    rmr_plan_free(plan);
    plan = nullptr;
    for (size_t failure = 0; failure < 4; failure++) {
        rmr_test_fail_after(failure);
        CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &plan, nullptr) == RMR_ERR_OUT_OF_MEMORY &&
              !plan);
    }

    rmr_test_fail_after(SIZE_MAX);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 9;
    CHECK(rmr_plan_create(&d, 1, viewport, &limits, &plan, nullptr) == RMR_ERR_LIMIT && !plan);

    d.erasers = nullptr;
    CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &plan, nullptr) == RMR_ERR_INVALID_ARGUMENT);

    d.erasers = &view;
    d.color.alpha = .5;
    CHECK(rmr_plan_create(&d, 1, viewport, nullptr, &plan, nullptr) == RMR_ERR_INVALID_ARGUMENT);

    d.color.alpha = 1;
    rmr_draw pair[] = {d, d};
    pair[1].group_alpha = .5;
    CHECK(rmr_plan_create(pair, 2, viewport, nullptr, &plan, nullptr) == RMR_ERR_INVALID_ARGUMENT);

    pair[1].group = 2;
    OK(rmr_plan_create(pair, 2, viewport, nullptr, &plan, nullptr));

    rmr_plan_free(plan);
    rmr_plan_free(base);
    puts("composition plan passed");
    return 0;
}
