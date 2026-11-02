/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#ifndef RMR_TEST_H
#define RMR_TEST_H
#include <math.h>
#include <rmrender/backend.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                                \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)
#define OK(x) CHECK((x) == RMR_OK)
static inline rmr_plan *rectangle(rmr_color color, rmr_blend blend) {
    rmr_element path[] = {{.kind = RMR_MOVE, .end = {-20, 5}},
                          {.kind = RMR_LINE, .end = {0, 5}},
                          {.kind = RMR_LINE, .end = {0, 25}},
                          {.kind = RMR_LINE, .end = {-20, 25}},
                          {.kind = RMR_CLOSE}};
    rmr_draw d = {.path = {path, 5}, .mode = RMR_FILL, .color = color, .blend = blend};
    rmr_plan *p = nullptr;
    OK(rmr_plan_create(&d, 1, (rmr_rect){-32, 0, 64, 64}, nullptr, &p, nullptr));
    return p;
}
#endif
