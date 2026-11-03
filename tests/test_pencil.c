/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"

static rmr_status unused(void *c, const rmr_unit_span *s, size_t n, rmr_rect b, size_t w, size_t h,
                         uint8_t *p, size_t stride) {
    (void)c;
    (void)s;
    (void)n;
    (void)b;
    (void)w;
    (void)h;
    (void)p;
    (void)stride;
    CHECK(false); /* Current capsules are portable and never call the backend. */
    return RMR_ERR_BACKEND;
}

int main(void) {
    rmr_grain_point points[] = {{-128, 32, 128, 0, 0}, {128, 32, 128, 0, 0}};
    rmr_rect box = {0, 0, 64, 64};
    rmr_mask mask = {0}, second = {0};
    rmr_unit_raster raster = {RMR_UNIT_RASTER_ABI, sizeof raster, nullptr, unused};

    for (unsigned grain = RMR_GRAIN_STAMP; grain <= RMR_GRAIN_SCRATCH; grain++) {
        OK(rmr_grain_mask(points, 2, grain, box, nullptr, nullptr, &mask, nullptr));
        OK(rmr_grain_mask(points, 2, grain, box, &raster, nullptr, &second, nullptr));
        CHECK(mask.width == 128 && mask.height == 128);
        CHECK(!memcmp(mask.bytes, second.bytes, mask.stride * mask.height));
        rmr_mask_free(&mask);
        rmr_mask_free(&second);
    }

    OK(rmr_grain_mask(points, 2, RMR_GRAIN_BRISTLE, box, nullptr, nullptr, &mask, nullptr));
    for (size_t i = 0; i < mask.stride * mask.height; i++)
        CHECK(mask.bytes[i] == 255);
    rmr_mask_free(&mask);

    points[0].pressure = points[1].pressure = 255;
    OK(rmr_grain_mask(points, 2, RMR_GRAIN_BRISTLE, box, nullptr, nullptr, &mask, nullptr));
    for (size_t i = 0; i < mask.stride * mask.height; i++)
        CHECK(mask.bytes[i] == 0);
    rmr_mask_free(&mask);

    for (size_t i = 0; i < 2; i++) {
        rmr_test_fail_after(i);
        CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_NOISE, box, nullptr, nullptr, &mask, nullptr) ==
              RMR_ERR_OUT_OF_MEMORY);
        CHECK(!mask.bytes);
    }

    rmr_test_fail_after(SIZE_MAX);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 1;
    CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_NOISE, box, nullptr, &limits, &mask, nullptr) ==
          RMR_ERR_LIMIT);
    CHECK(!mask.bytes);

    raster.abi_version = 999;
    CHECK(rmr_grain_mask(points, 2, RMR_GRAIN_NOISE, box, &raster, nullptr, &mask, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);

    CHECK(rmr_stamp_variant(NAN, 0) == 0 && rmr_stamp_variant(1e20f, 0) == 0);
    CHECK(rmr_grain_noise(-1, -2) >= 0 && rmr_grain_noise(-1, -2) < 1);

    puts("portable grain masks, pressure extremes, limits and allocations passed");
    return 0;
}
