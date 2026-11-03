/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"

int main(void) {
    rmr_sample points[] = {{{0, 0}, 10}, {{100, 0}, 20}};
    rmr_path p = {0};
    rmr_limits l = rmr_limits_default();
    l.maximum_elements = 1;
    CHECK(rmr_outline(points, 2, RMR_CAP_ROUND, RMR_JOIN_ROUND, &l, &p, nullptr) == RMR_ERR_LIMIT);
    CHECK(!p.elements);
    l = rmr_limits_default();
    l.maximum_points = 1;
    CHECK(rmr_outline(points, 2, RMR_CAP_ROUND, RMR_JOIN_ROUND, &l, &p, nullptr) == RMR_ERR_LIMIT);
    points[0].position.x = NAN;
    CHECK(rmr_outline(points, 2, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &p, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    points[0].position.x = 0;
    bool succeeded = false;
    for (size_t budget = 0; budget < 100; budget++) {
        rmr_test_fail_after(budget);
        rmr_status s = rmr_outline(points, 2, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &p, nullptr);
        rmr_test_fail_after(SIZE_MAX);
        if (s == RMR_OK) {
            succeeded = true;
            break;
        }
        CHECK(s == RMR_ERR_OUT_OF_MEMORY && !p.elements);
    }
    CHECK(succeeded);
    rmr_draw d = {.path = {p.elements, p.count}, .mode = RMR_FILL, .color = {0, 0, 0, 1}};
    rmr_plan *plan = nullptr;
    succeeded = false;
    for (size_t budget = 0; budget < 100; budget++) {
        rmr_test_fail_after(budget);
        rmr_status s =
            rmr_plan_create(&d, 1, (rmr_rect){-100, 0, 200, 200}, nullptr, &plan, nullptr);
        rmr_test_fail_after(SIZE_MAX);
        if (s == RMR_OK) {
            succeeded = true;
            break;
        }
        CHECK(s == RMR_ERR_OUT_OF_MEMORY && !plan);
    }
    CHECK(succeeded);
    rmr_plan_free(plan);
    rmr_path_free(&p);
    rmr_element bad = {.kind = RMR_LINE, .end = {1, 2}};
    d.path = (rmr_path_view){&bad, 1};
    plan = nullptr;
    CHECK(rmr_plan_create(&d, 1, (rmr_rect){0, 0, 10, 10}, nullptr, &plan, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    /* Bounded, deterministic arbitrary geometry with duplicates and acute turns. */
    uint32_t state = 0x524d52;
    for (size_t i = 0; i < 20000; i++) {
        rmr_sample samples[8];
        for (size_t j = 0; j < 8; j++) {
            state = state * 1664525 + 1013904223;
            samples[j] =
                (rmr_sample){{(double)(state % 2000) - 1000, (double)((state >> 16) % 2000) - 1000},
                             (double)(state % 200)};
        }
        OK(rmr_outline(samples, 8, (rmr_cap)(i % 3), (rmr_join)(i % 2), nullptr, &p, nullptr));
        rmr_path_free(&p);
    }
    puts("errors and 20000 geometry cases passed");
    return 0;
}
