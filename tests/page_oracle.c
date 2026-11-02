/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <rmrender/rmlines.h>
#include <stdio.h>
int main(void) {
    double a, b, c, d, cw, ch, x, y, w, h;
    unsigned ha, hb, landscape;
    while (scanf("%u %lf %lf %u %lf %lf %lf %lf %u %lf %lf %lf %lf", &ha, &a, &b, &hb, &c, &d, &cw,
                 &ch, &landscape, &x, &y, &w, &h) == 13) {
        rm_page first = {.has_paper_size = ha != 0, .paper_size = {a, b}},
                second = {.has_paper_size = hb != 0, .paper_size = {c, d}};
        const rm_page *pages[] = {&first, &second};
        rmr_point canvas, extent;
        if (rmr_lines_document_canvas(pages, 2, (rmr_point){cw, ch}, landscape != 0, nullptr,
                                      &canvas, nullptr) != RMR_OK)
            return 1;
        if (rmr_template_page_extent(canvas, (rmr_rect){x, y, w, h}, &extent, nullptr) != RMR_OK)
            return 2;
        printf("%.17g %.17g %.17g %.17g\n", canvas.x, canvas.y, extent.x, extent.y);
    }
    return 0;
}
