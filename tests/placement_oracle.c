/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <rmrender/rmlines.h>
#include <stdio.h>
static rmr_status resolve(void *context, const rm_text_anchor *anchor, rmr_point *out) {
    (void)anchor;
    *out = *(rmr_point *)context;
    return RMR_OK;
}
int main(void) {
    double x, y, dx, dy;
    unsigned anchored;
    while (scanf("%lf %lf %lf %lf %u", &x, &y, &dx, &dy, &anchored) == 5) {
        rm_stroke_point point = {.x = (float)x, .y = (float)y, .width = 8, .pressure = .5f};
        rm_stroke stroke = {.points = &point, .point_count = 1, .has_anchor = anchored != 0};
        rm_page_image image = {.vertices = {{x, y, 0, 0}, {x, y, 1, 0}, {x, y, 1, 1}, {x, y, 0, 1}},
                               .has_anchor = anchored != 0};
        rmr_point pen = {dx, dy};
        rmr_anchor_provider provider = {RMR_ANCHOR_PROVIDER_ABI, sizeof provider, &pen, resolve};
        rmr_placed_scene *s = nullptr;
        rmr_limits limits = rmr_limits_default();
        limits.maximum_coordinate = 1e12;
        if (rmr_lines_place_scene(&stroke, 1, &image, 1, &provider, &limits, &s, nullptr) != RMR_OK)
            return 1;
        const rm_stroke_point *p = rmr_placed_scene_strokes(s)[0].points;
        rm_image_vertex v = rmr_placed_scene_images(s)[0].vertices[0];
        printf("%.17g %.17g %.17g %.17g\n", (double)p->x, (double)p->y, v.x, v.y);
        rmr_placed_scene_free(s);
    }
    return 0;
}
