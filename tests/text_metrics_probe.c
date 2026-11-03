/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Independent diagnostic output for Apple/Linux comparison. */
#include "text_metrics_cases.h"
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
#include <rmrender/apple_text.h>
#define F rmr_apple_fonts
#define create rmr_apple_fonts_create_directory
#define width rmr_apple_text_width
#define release rmr_apple_fonts_free
#define L rmr_apple_layout
#define layout_create rmr_apple_layout_create
#define anchors rmr_apple_layout_anchors
#define line_get rmr_apple_layout_line
#define layout_free rmr_apple_layout_free
#else
#include <fontconfig/fontconfig.h>
#include <rmrender/linux_text.h>
#define F rmr_linux_fonts
#define create rmr_linux_fonts_create_directory
#define width rmr_linux_text_width
#define release rmr_linux_fonts_free
#define L rmr_linux_layout
#define layout_create rmr_linux_layout_create
#define anchors rmr_linux_layout_anchors
#define line_get rmr_linux_layout_line
#define layout_free rmr_linux_layout_free
#endif
int main(int argc, char **argv) {
    F *fonts = 0;
    rmr_error e = {0};
    if (argc != 2 || create(argv[1], 0, &fonts, &e))
        return 1;
    for (size_t size_index = 0; size_index < 4; size_index++)
        for (size_t i = 0; i < 9; i++)
            for (unsigned face = 0; face < 3; face++)
                for (unsigned bold = 0; bold < 2; bold++)
                    for (unsigned italic = 0; italic < 2; italic++) {
                        double w;
                        if (width((rm_str){metric_texts[i], strlen(metric_texts[i])},
                                  metric_sizes[size_index], (rmr_type_face)face, bold != 0,
                                  italic != 0, false, fonts, nullptr, &w, &e))
                            return 2;
                        printf("%g %zu %u %u %u %.9f\n", metric_sizes[size_index], i, face, bold,
                               italic, w);
                    }
    const char *sample = "Hello world Hello world Hello world";
    rm_paragraph para = {.text = {sample, strlen(sample)}};
    rm_text_block block = {.origin_x = -100,
                           .origin_y = 10,
                           .paragraphs = &para,
                           .paragraph_count = 1,
                           .has_column_width = true,
                           .column_width = 200};
    L *l = 0;
    if (layout_create(&block, 200, fonts, 0, &l, &e))
        return 3;
    const rmr_anchor_layout *a = anchors(l);
    for (size_t i = 0; i < a->paragraphs[0].line_count; i++) {
        rmr_text_line line;
        if (line_get(l, 0, i, &line, &e))
            return 4;
        printf("LINE %zu %zu %.9f %.9f %.9f\n", line.from, line.to, line.baseline, line.start,
               line.end);
    }
    for (size_t i = 0; i <= 11; i++) {
        rmr_caret_offsets c;
        if (a->caret->measure(a->caret->context, 0, 0, i, &c))
            return 5;
        printf("CARET %zu %.9f %.9f\n", i, c.at_index, c.at_start);
    }
    layout_free(l);
    release(fonts);
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
    FcFini();
#endif
    return 0;
}
