/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "rmr_internal.h"
#include "test.h"
#include <rmrender/grain.h>
static rmr_status untouched_begin(void *context, rmr_rect viewport) {
    (void)context;
    (void)viewport;
    CHECK(false);
    return RMR_OK;
}
static rmr_status untouched_draw(void *context, const rmr_draw *draw) {
    (void)context;
    (void)draw;
    CHECK(false);
    return RMR_OK;
}
static rmr_status untouched_end(void *context) {
    (void)context;
    CHECK(false);
    return RMR_OK;
}
static void untouched_abort(void *context) {
    (void)context;
    CHECK(false);
}
int main(void) {
    rmr_grain_point points[] = {{0, 0, 12, 128, 10}, {40, 10, 20, 210, 60}, {40, 10, 10, 255, 0}};
    rmr_grain_point minimum = {.x = 0.35f, .width = 0.7};
    rmr_rect edge;
    OK(rmr_grain_box(&minimum, 1, nullptr, &edge, nullptr));
    CHECK(edge.x == -2); /* Keep Double width for bounds, Float only in raster. */
    rmr_rect box;
    OK(rmr_grain_box(points, 3, nullptr, &box, nullptr));
    CHECK(box.x == -7 && box.y == -7 && box.width == 58 && box.height == 28);
    rmr_mask mask = {0}, again = {0};
    OK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, box, nullptr, nullptr, &mask, nullptr));
    OK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, box, nullptr, nullptr, &again, nullptr));
    CHECK(mask.width == 116 && mask.height == 56 && mask.stride == 15);
    CHECK(memcmp(mask.bytes, again.bytes, mask.stride * mask.height) == 0);
    for (size_t row = 0; row < mask.height; row++)
        CHECK((mask.bytes[row * mask.stride + 14] & 15) == 15);
    rmr_mask_free(&again);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_plan_bytes = 100;
    CHECK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, box, nullptr, &limits, &again, nullptr) ==
              RMR_ERR_LIMIT &&
          !again.bytes);
    CHECK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, (rmr_rect){0, 0, 16385, 10}, nullptr, nullptr,
                         &again, nullptr) == RMR_ERR_LIMIT);
    CHECK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, (rmr_rect){0.5, 0, 20, 20}, nullptr, nullptr,
                         &again, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    points[0].x = NAN;
    CHECK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, box, nullptr, nullptr, &again, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    points[0].x = 0;
    for (size_t fail = 0; fail < 2; fail++) {
        rmr_test_fail_after(fail);
        CHECK(rmr_grain_mask(points, 3, RMR_GRAIN_STREAK, box, nullptr, nullptr, &again, nullptr) ==
                  RMR_ERR_OUT_OF_MEMORY &&
              !again.bytes);
    }
    rmr_test_fail_after(SIZE_MAX);
    rmr_element path[] = {{.kind = RMR_MOVE, .end = {0, 0}},
                          {.kind = RMR_LINE, .end = {40, 0}},
                          {.kind = RMR_LINE, .end = {40, 20}},
                          {.kind = RMR_CLOSE}};
    rmr_draw draw = {.path = {path, 4},
                     .color = {0, 0, 0, 1},
                     .mask = {mask.bytes, mask.width, mask.height, mask.stride, mask.box}};
    rmr_plan *plan = nullptr;
    OK(rmr_plan_create(&draw, 1, (rmr_rect){-20, -20, 100, 100}, nullptr, &plan, nullptr));
    rmr_backend backend = {RMR_BACKEND_ABI, sizeof backend, RMR_CAP_FILL,  nullptr,
                           untouched_begin, untouched_draw, untouched_end, untouched_abort};
    CHECK(rmr_plan_execute(plan, &backend, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED);
    for (size_t fail = 0; fail < 4; fail++) {
        rmr_plan *failed = nullptr;
        rmr_test_fail_after(fail);
        CHECK(rmr_plan_create(&draw, 1, (rmr_rect){-20, -20, 100, 100}, nullptr, &failed,
                              nullptr) == RMR_ERR_OUT_OF_MEMORY &&
              !failed);
    }
    rmr_test_fail_after(SIZE_MAX);
    rmr_plan *failed = nullptr;
    draw.mask.stride = SIZE_MAX;
    CHECK(rmr_plan_create(&draw, 1, (rmr_rect){-20, -20, 100, 100}, nullptr, &failed, nullptr) ==
              RMR_ERR_LIMIT &&
          !failed);
    draw.mask.stride = mask.stride;
    draw.mode = RMR_STROKE;
    draw.line_width = 1;
    CHECK(rmr_plan_create(&draw, 1, (rmr_rect){-20, -20, 100, 100}, nullptr, &failed, nullptr) ==
              RMR_ERR_INVALID_ARGUMENT &&
          !failed);
    const rmr_draw *owned = rmr_plan_draw(plan, 0);
    CHECK(owned->mask.bytes != mask.bytes &&
          memcmp(owned->mask.bytes, mask.bytes, mask.stride * mask.height) == 0);
    rmr_mask_free(&mask);
    CHECK(owned->mask.width == 116);
    rmr_plan_free(plan);
    puts("grain mask passed");
    return 0;
}
