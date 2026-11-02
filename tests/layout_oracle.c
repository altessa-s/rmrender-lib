/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Own generated cases; fonts supplied explicitly by the test host. */
#include "test.h"
#include <rmrender/apple_text.h>
static uint8_t *loaded;
static rmr_status font(void *context, const char *name, rmr_asset_view *out) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.ttf", (const char *)context, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return RMR_ERR_UNSUPPORTED;
    CHECK(fseek(f, 0, SEEK_END) == 0);
    long size = ftell(f);
    CHECK(size > 0);
    rewind(f);
    free(loaded);
    loaded = malloc((size_t)size);
    CHECK(loaded);
    CHECK(fread(loaded, 1, (size_t)size, f) == (size_t)size);
    fclose(f);
    *out = (rmr_asset_view){loaded, (size_t)size};
    return RMR_OK;
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    rmr_apple_fonts *fonts = nullptr;
    OK(rmr_apple_fonts_create(font, argv[1], nullptr, &fonts, nullptr));
    free(loaded);
    loaded = nullptr;
    char row[16384];
    int offset = 0;
    unsigned raw, second, third, bold, italic;
    double width;
    while (fgets(row, sizeof row, stdin)) {
        CHECK(sscanf(row, "%u %u %u %u %u %lf%n", &raw, &second, &third, &bold, &italic, &width,
                     &offset) == 6);
        char *text = row + offset + 1;
        size_t length = strlen(text);
        if (length && text[length - 1] == '\n')
            text[--length] = 0;
        rm_face_run face = {0, 2, bold != 0, italic != 0};
        rm_paragraph p = {.text = {text, length},
                          .has_format = true,
                          .format = {.raw = (uint8_t)raw,
                                     .has_second = second != 0,
                                     .second = (uint8_t)second,
                                     .has_third = third != 0,
                                     .third = third},
                          .faces = &face,
                          .face_count = 1};
        rm_text_block block = {.origin_x = -250,
                               .origin_y = 100,
                               .paragraphs = &p,
                               .paragraph_count = 1,
                               .has_column_width = true,
                               .column_width = width};
        rmr_apple_layout *layout = nullptr;
        rmr_error error = {0};
        rmr_status status = rmr_apple_layout_create(&block, 1404, fonts, nullptr, &layout, &error);
        if (status != RMR_OK) {
            fprintf(stderr, "layout: %s\n", error.message);
            return 1;
        }
        const rmr_anchor_layout *a = rmr_apple_layout_anchors(layout);
        double bottom = 0;
        CHECK(rmr_apple_layout_bottom(layout, &bottom));
        printf("%.17g %.17g %zu", a->paragraphs[0].baseline, bottom, a->paragraphs[0].line_count);
        for (size_t i = 0; i < a->paragraphs[0].line_count; i++) {
            rmr_anchor_line line = a->paragraphs[0].lines[i];
            rmr_caret_offsets c;
            OK(a->caret->measure(a->caret->context, 0, i, line.location, &c));
            printf(" %zu %zu %.17g %.17g", line.location, line.length, line.baseline, c.at_start);
        }
        puts("");
        rmr_plan *plan = nullptr;
        OK(rmr_apple_layout_plan(layout, (rmr_rect){-300, 0, 600, 2000}, nullptr, &plan, &error));
        rmr_apple_layout_free(layout);
        rmr_bitmap bitmap = {0};
        OK(rmr_apple_bitmap(plan, .1, (rmr_color){1, 1, 1, 1}, nullptr, nullptr, nullptr, &bitmap,
                            &error));
        rmr_bitmap_free(&bitmap);
        rmr_plan_free(plan);
    }
    rmr_apple_fonts_free(fonts);
    return 0;
}
