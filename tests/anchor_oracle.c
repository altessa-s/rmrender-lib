/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <rmrender/rmlines.h>
#include <stdio.h>
#include <string.h>
typedef struct {
    const rmr_anchor_paragraph *paragraphs;
} context;
static rmr_status measure(void *opaque, size_t paragraph, size_t line, size_t index,
                          rmr_caret_offsets *out) {
    context *c = opaque;
    const rmr_anchor_paragraph *p = &c->paragraphs[paragraph];
    double base = p->right_to_left ? 100 : 10, scale = p->right_to_left ? -2.5 : 2.5;
    *out = (rmr_caret_offsets){base + (double)index * scale,
                               base + (double)p->lines[line].location * scale};
    return RMR_OK;
}
int main(void) {
    int paragraph, offset;
    unsigned has_x, rtl, typesetter;
    double x, left, column, indent, baseline;
    size_t split, total;
    char hex[4096];
    while (scanf("%d %d %u %lf %u %u %lf %lf %lf %lf %zu %zu %4095s", &paragraph, &offset, &has_x,
                 &x, &rtl, &typesetter, &left, &column, &indent, &baseline, &split, &total,
                 hex) == 13) {
        char text[2048];
        size_t size = !strcmp(hex, "-") ? 0 : strlen(hex) / 2;
        for (size_t i = 0; i < size; i++) {
            unsigned byte;
            if (sscanf(hex + i * 2, "%2x", &byte) != 1)
                return 1;
            text[i] = (char)byte;
        }
        rmr_anchor_line lines[] = {{0, split, baseline}, {split, total - split, baseline + 30}};
        rmr_anchor_paragraph p = {.text = {text, size},
                                  .lines = lines,
                                  .line_count = 2,
                                  .indent = indent,
                                  .baseline = baseline,
                                  .right_to_left = rtl != 0,
                                  .has_typesetter = typesetter != 0};
        context c = {&p};
        rmr_caret_provider caret = {RMR_CARET_PROVIDER_ABI, sizeof caret, &c, measure};
        rmr_anchor_layout layout = {left, column, &p, 1, &caret};
        rm_text_anchor anchor = {paragraph, offset, has_x != 0, x};
        rmr_point out;
        if (rmr_lines_anchor_pen(&layout, &anchor, nullptr, &out, nullptr) != RMR_OK)
            return 2;
        printf("%.17g %.17g\n", out.x, out.y);
    }
    return 0;
}
