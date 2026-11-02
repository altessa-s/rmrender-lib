/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Synthetic scene protocol for tests/compare_scene_swift.py; no file parsing API. */
#include <rmrender/apple.h>
#include <rmrender/rmlines.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    unsigned int count;
    while (scanf("%u", &count) == 1) {
        if (count > 64)
            return 2;
        rm_stroke strokes[64] = {0};
        rm_stroke_point points[64][128] = {0};
        for (unsigned int i = 0; i < count; i++) {
            int tool;
            unsigned int color, argb, source, author, n;
            unsigned long long counter;
            if (scanf("%d %u %u %u %u %llu %u", &tool, &color, &argb, &source, &author, &counter,
                      &n) != 7 ||
                n > 128 || author > 255)
                return 2;
            strokes[i] = (rm_stroke){.tool = (rm_pen_tool)tool,
                                     .color = (rm_pen_color)color,
                                     .points = points[i],
                                     .point_count = n,
                                     .has_argb = argb != 0,
                                     .argb = argb,
                                     .has_source_id = source != 0,
                                     .source_id = {(uint8_t)author, (uint64_t)counter}};
            for (unsigned int k = 0; k < n; k++)
                if (scanf("%f %f %f %f %f", &points[i][k].x, &points[i][k].y, &points[i][k].width,
                          &points[i][k].pressure, &points[i][k].speed) != 5)
                    return 2;
        }
        rmr_unit_raster raster = rmr_apple_unit_raster();
        rmr_plan *plan = nullptr;
        rmr_error error;
        rmr_status status = rmr_lines_plan_with_raster(strokes, count, (rmr_rect){-32, 0, 64, 64},
                                                       &raster, nullptr, &plan, &error);
        if (status != RMR_OK) {
            fprintf(stderr, "plan %s: %s\n", rmr_status_string(status), error.message);
            return 1;
        }
        rmr_bitmap bitmap = {0};
        status =
            rmr_apple_bitmap(plan, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, &error);
        if (status != RMR_OK) {
            fprintf(stderr, "render %s\n", error.message);
            return 1;
        }
        for (size_t i = 0; i < bitmap.stride * bitmap.height; i++)
            printf("%02x", bitmap.pixels[i]);
        putchar('\n');
        rmr_bitmap_free(&bitmap);
        rmr_plan_free(plan);
    }
    return 0;
}
