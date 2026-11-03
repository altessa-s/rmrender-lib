/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"
#include <rmrender/grain.h>

int main(void) {
    rmr_grain_point points[] = {{-128, 32, 128, 128, 0}, {128, 32, 128, 203, 0}};
    rmr_mask mask = {0};
    rmr_rect box = {0, 0, 64, 64};
    OK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, box, nullptr, nullptr, &mask, nullptr));
    size_t paper = 0;
    for (size_t y = 0; y < 64; y++)
        for (size_t x = 0; x < 64; x++)
            paper += (mask.bytes[y * 2 * mask.stride + x * 2 / 8] >> (7 - (x * 2 % 8))) & 1u;
    CHECK(paper > 0 && paper < 368); /* Broad lines close most scratches. */
    rmr_mask_free(&mask);
    points[0].pressure = points[1].pressure = 255;
    OK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, box, nullptr, nullptr, &mask, nullptr));
    for (size_t i = 0; i < mask.stride * mask.height; i++)
        CHECK(mask.bytes[i] == 0);
    rmr_mask_free(&mask);
    points[1].pressure = NAN;
    OK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, box, nullptr, nullptr, &mask, nullptr));
    paper = 0;
    for (size_t y = 0; y < 64; y++)
        for (size_t x = 0; x < 64; x++)
            paper += (mask.bytes[y * 2 * mask.stride + x * 2 / 8] >> (7 - (x * 2 % 8))) & 1u;
    CHECK(paper > 0 && paper < 368);
    rmr_mask_free(&mask);
    rmr_test_fail_after(0);
    CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, box, nullptr, nullptr, &mask, nullptr) ==
              RMR_ERR_OUT_OF_MEMORY &&
          !mask.bytes);
    rmr_test_fail_after(SIZE_MAX);
    rmr_limits l = rmr_limits_default();
    l.maximum_plan_bytes = 1;
    CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, box, nullptr, &l, &mask, nullptr) ==
              RMR_ERR_LIMIT &&
          !mask.bytes);
    CHECK(rmr_grain_mask(nullptr, 2, RMR_GRAIN_SCRATCH, box, nullptr, nullptr, &mask, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, (rmr_rect){0, 0, 16385, 1}, nullptr, nullptr,
                         &mask, nullptr) == RMR_ERR_LIMIT);
    CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_SCRATCH, (rmr_rect){0.5, 0, 64, 64}, nullptr, nullptr,
                         &mask, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    OK(rmr_grain_mask(points, 1, RMR_GRAIN_SCRATCH, box, nullptr, nullptr, &mask, nullptr));
    for (size_t i = 0; i < mask.stride * mask.height; i++)
        CHECK(mask.bytes[i] == 255);
    rmr_mask_free(&mask);
    puts("scratch grain passed");
    return 0;
}
