/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Test-only generated page protocol. */
#include <rmrender/rmlines.h>
#include <stdio.h>
int main(void) {
    for (;;) {
        double w, h, margin;
        unsigned edge;
        size_t ns, ni;
        int n = scanf("%lf %lf %u %lf %zu %zu", &w, &h, &edge, &margin, &ns, &ni);
        if (n == EOF)
            return 0;
        if (n != 6 || ns > 16 || ni > 16)
            return 1;
        rm_stroke strokes[16] = {0};
        rm_stroke_point points[16][16] = {0};
        rm_page_image images[16] = {0};
        for (size_t i = 0; i < ns; i++) {
            int tool;
            size_t count;
            if (scanf("%d %zu", &tool, &count) != 2 || count > 16)
                return 2;
            strokes[i] =
                (rm_stroke){.tool = (rm_pen_tool)tool, .point_count = count, .points = points[i]};
            for (size_t k = 0; k < count; k++) {
                rm_stroke_point *p = &points[i][k];
                if (scanf("%f %f %f %f", &p->x, &p->y, &p->width, &p->pressure) != 4)
                    return 3;
            }
        }
        for (size_t i = 0; i < ni; i++)
            for (size_t k = 0; k < 4; k++)
                if (scanf("%lf %lf", &images[i].vertices[k].x, &images[i].vertices[k].y) != 2)
                    return 4;
        rm_page page = {
            .strokes = strokes, .stroke_count = ns, .images = images, .image_count = ni};
        rmr_rect b;
        rmr_error e = {0};
        if (rmr_lines_content_bounds(&page, w, h, (rmr_ink_edge)edge, margin, nullptr, &b, &e) !=
            RMR_OK) {
            fprintf(stderr, "%s\n", e.message);
            return 5;
        }
        printf("%.17g %.17g %.17g %.17g\n", b.x, b.y, b.width, b.height);
    }
}
