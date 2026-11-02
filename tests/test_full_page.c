/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "test.h"
#include <rmrender/apple_marks.h>
int main(int argc, char **argv) {
    CHECK(argc == 2);
    rmr_apple_fonts *fonts = nullptr;
    rmr_error error = {0};
    OK(rmr_apple_fonts_create_directory(argv[1], nullptr, &fonts, &error));
    const char *words = "مرحبا بالعالم العربية";
    rm_face_run run = {0, 2, false, false};
    rm_paragraph paragraph = {.text = {words, strlen(words)},
                              .has_format = true,
                              .format = {.raw = 10},
                              .faces = &run,
                              .face_count = 1};
    rm_text_block text = {.origin_x = -250,
                          .origin_y = 100,
                          .paragraphs = &paragraph,
                          .paragraph_count = 1,
                          .has_column_width = true,
                          .column_width = 100};
    rmr_apple_layout *layout = nullptr;
    OK(rmr_apple_layout_create(&text, 600, fonts, nullptr, &layout, &error));
    const rmr_anchor_layout *anchors = rmr_apple_layout_anchors(layout);
    CHECK(anchors->paragraphs[0].line_count == 7);
    rmr_caret_offsets offsets;
    OK(anchors->caret->measure(anchors->caret->context, 0, 0, 0, &offsets));
    CHECK(fabs(offsets.at_start - 28.192) < 1e-8);
    rm_text_anchor anchor = {.paragraph = 0, .offset = 3};
    rmr_point pen;
    OK(rmr_lines_anchor_pen(anchors, &anchor, nullptr, &pen, &error));
    CHECK(fabs(pen.y - 209.15) < 1e-8);
    rmr_apple_layout_free(layout);
    rm_stroke_point points[] = {{.x = 0, .y = 0, .width = 40}, {.x = 50, .y = 0, .width = 40}};
    rm_stroke stroke = {.tool = RM_TOOL_HIGHLIGHTER_2,
                        .color = RM_COLOR_YELLOW,
                        .points = points,
                        .point_count = 2,
                        .has_anchor = true,
                        .anchor = anchor,
                        .id = {1, 2}};
    rm_rect rect = {-100, 20, 60, 15};
    rm_highlight highlight = {.rectangles = &rect,
                              .rectangle_count = 1,
                              .color = RM_COLOR_YELLOW,
                              .has_text = true,
                              .text = {"own words", 9},
                              .id = {2, 3}};
    rm_page page = {.has_text = true,
                    .text = text,
                    .strokes = &stroke,
                    .stroke_count = 1,
                    .highlights = &highlight,
                    .highlight_count = 1};
    rmr_plan *plan = nullptr;
    OK(rmr_apple_page_plan(&page, (rmr_point){600, 400}, RMR_EDGE_RIBBON, 0, nullptr, fonts,
                           nullptr, &plan, &error));
    CHECK(rmr_plan_count(plan) >= 3);
    rmr_rect viewport = rmr_plan_viewport(plan);
    CHECK(viewport.height == 400);
    rmr_plan_free(plan);
    plan = nullptr;
    OK(rmr_apple_page_plan(&page, (rmr_point){600, 400}, RMR_EDGE_UNCUT, 0, nullptr, fonts, nullptr,
                           &plan, &error));
    viewport = rmr_plan_viewport(plan);
    CHECK(viewport.height > 600);
    rmr_page_marks *marks = nullptr;
    OK(rmr_apple_page_marks(&page, (rmr_point){600, 400}, viewport, (rm_str){"page", 4}, fonts,
                            nullptr, &marks, &error));
    const rmm_marked_page *view = rmr_page_marks_view(marks);
    CHECK(!rmm_page_is_empty(view) && view->band_count == 1 && view->highlight_count == 1);
    CHECK(view->bands[0].item.length == 3 && !memcmp(view->bands[0].item.bytes, "1:2", 3));
    CHECK(view->highlights[0].text.length == 9);
    rmr_apple_fonts_free(fonts);
    rmr_bitmap bitmap = {0};
    OK(rmr_apple_bitmap(plan, .5, (rmr_color){1, 1, 1, 1}, nullptr, nullptr, nullptr, &bitmap,
                        &error));
    size_t ink = 0;
    for (size_t i = 0; i < bitmap.height * bitmap.stride; i += 4)
        if (bitmap.pixels[i] < 200)
            ink++;
    CHECK(ink > 100);
    rmm_page_box blocks[] = {view->bands[0].box, view->highlights[0].band.box};
    rmr_mark_sheet sheet = {0};
    OK(rmr_apple_mark_sheet(&bitmap, blocks, 2, 2, 3, nullptr, &sheet, &error));
    CHECK(sheet.count == 2 && sheet.bitmap.height > 6);
    CHECK(sheet.bitmap.pixels[0] == 255);
    CHECK(sheet.bitmap.pixels[(sheet.bitmap.height - 1) * sheet.bitmap.stride] == 0);
    rmr_mark_sheet_free(&sheet);
    rmr_page_marks_free(marks);
    rmr_bitmap_free(&bitmap);
    rmr_buffer pdf = {0};
    const rmr_plan *plans[] = {plan};
    OK(rmr_apple_pdf(plans, 1, RMR_POINTS_PER_UNIT, nullptr, nullptr, nullptr, &pdf, &error));
    CHECK(pdf.size > 1000);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
    return 0;
}
