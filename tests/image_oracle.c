/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Test-only generated placement protocol; no document files. */
#include "image_fixture.h"
#include <rmrender/apple.h>
#include <rmrender/rmlines.h>
#include <stdio.h>
static rmr_status resolve(void *context, rm_str file, rmr_asset_view *out) {
    (void)context;
    (void)file;
    *out = (rmr_asset_view){image_png, sizeof image_png};
    return RMR_OK;
}
int main(void) {
    for (;;) {
        rm_page_image image = {0};
        for (size_t k = 0; k < 4; k++) {
            rm_image_vertex *v = &image.vertices[k];
            int n = scanf("%lf %lf %lf %lf", &v->x, &v->y, &v->u, &v->v);
            if (n == EOF && !k)
                return 0;
            if (n != 4)
                return 1;
        }
        rm_page page = {.images = &image, .image_count = 1};
        rmr_plan *plan = nullptr;
        rmr_error error = {0};
        if (rmr_lines_scene_plan(&page, (rmr_rect){-32, 0, 64, 64}, resolve, nullptr, nullptr,
                                 nullptr, &plan, &error) != RMR_OK) {
            fprintf(stderr, "%s\n", error.message);
            return 2;
        }
        rmr_bitmap b = {0};
        if (rmr_apple_bitmap(plan, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, &error) !=
            RMR_OK)
            return 3;
        for (size_t i = 0; i < b.stride * b.height; i++)
            printf("%02x", b.pixels[i]);
        putchar('\n');
        rmr_bitmap_free(&b);
        rmr_plan_free(plan);
    }
}
