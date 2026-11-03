/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <rmmarks/text.h>
#ifdef RMR_TEST_LINUX
#include <fontconfig/fontconfig.h>
#include <rmrender/linux_marks.h>
#define native(name) rmr_linux_##name
#define CARET_TOLERANCE .02
#else
#include <rmrender/apple_marks.h>
#define native(name) rmr_apple_##name
#define CARET_TOLERANCE 1e-8
#endif
static void marks_contract(void) {
    rm_stroke_point points[] = {{.x = 10, .y = 20, .width = 40}, {.x = 80, .y = 20, .width = 40}};
    rm_stroke strokes[] = {{.tool = RM_TOOL_HIGHLIGHTER_2,
                            .color = RM_COLOR_YELLOW,
                            .points = points,
                            .point_count = 2,
                            .id = {1, 2}},
                           {.tool = RM_TOOL_HIGHLIGHTER_2,
                            .color = RM_COLOR_YELLOW,
                            .points = points,
                            .point_count = 1,
                            .id = {1, 3}}};
    char words[] = "owned", id[] = "page";
    rm_rect rectangles[] = {{-10, 30, 40, 10}, {20, 35, 20, 10}};
    rm_highlight highlight = {.rectangles = rectangles,
                              .rectangle_count = 2,
                              .color = RM_COLOR_YELLOW,
                              .has_text = true,
                              .text = {words, 5},
                              .id = {2, 3}};
    rm_page page = {
        .strokes = strokes, .stroke_count = 2, .highlights = &highlight, .highlight_count = 1};
    rmr_page_marks *marks = nullptr;
    rmr_rect frame = {0, 0, 100, 100};
    OK(native(page_marks)(&page, (rmr_point){100, 100}, frame, (rm_str){id, 4}, nullptr, nullptr,
                          &marks, nullptr));
    const rmm_marked_page *view = rmr_page_marks_view(marks);
    CHECK(view->band_count == 2 && view->highlight_count == 1);
    CHECK(fabs(view->bands[0].box.x0 - .1) < .0001);
    CHECK(fabs(view->bands[0].box.x1 - .8) < .0001);
    CHECK(fabs(view->bands[0].box.y0 - .15) < .0001);
    CHECK(fabs(view->bands[0].box.y1 - .25) < .0001);
    CHECK(fabs(view->bands[1].box.x0 - .05) < .0001);
    CHECK(fabs(view->bands[1].box.x1 - .15) < .0001);
    CHECK(view->highlights[0].band.box.x0 == 0);
    CHECK(fabs(view->highlights[0].band.box.x1 - .4) < .0001);
    CHECK(fabs(view->highlights[0].band.box.y1 - .45) < .0001);
    words[0] = id[0] = 'X';
    CHECK(!memcmp(view->highlights[0].text.bytes, "owned", 5));
    CHECK(!memcmp(view->page_id.bytes, "page", 4));
    rmr_page_marks *out = marks;
    rmr_limits small = rmr_limits_default();
    small.maximum_plan_bytes = 1;
    CHECK(native(page_marks)(&page, (rmr_point){100, 100}, frame, (rm_str){0}, nullptr, &small,
                             &out, nullptr) == RMR_ERR_LIMIT);
    CHECK(out == marks);
    rectangles[0].width = -1;
    CHECK(native(page_marks)(&page, (rmr_point){100, 100}, frame, (rm_str){0}, nullptr, nullptr,
                             &out, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    CHECK(out == marks);
    rmr_page_marks_free(marks);
    rmr_page_marks_free(nullptr);
    CHECK(!rmr_page_marks_view(nullptr));
}

static void sheet_contract(void) {
    uint8_t pixels[80] = {0};
    for (size_t y = 0; y < 4; y++)
        for (size_t x = 0; x < 4; x++) {
            pixels[y * 20 + x * 4] = 64;
            pixels[y * 20 + x * 4 + 3] = 128;
        }
    rmr_bitmap page = {pixels, 4, 4, 20};
    rmm_page_box boxes[] = {{0, .25, .5, .75}, {2, 2, 3, 3}};
    rmr_mark_sheet sheet = {0};
    OK(native(mark_sheet)(&page, boxes, 2, 0, 1, nullptr, &sheet, nullptr));
    CHECK(sheet.count == 1 && sheet.bitmap.width == 2 && sheet.bitmap.height == 4);
    CHECK(sheet.bitmap.pixels[0] == 255);
    const uint8_t *row = sheet.bitmap.pixels + sheet.bitmap.stride;
    CHECK(row[0] == 191 && row[1] == 127 && row[2] == 127 && row[3] == 255);
    CHECK(sheet.bitmap.pixels[3 * sheet.bitmap.stride] == 0);
    CHECK(sheet.strips[0].on_page.y0 == .25 && sheet.strips[0].in_sheet.y0 == .25);
    CHECK(sheet.strips[0].in_sheet.y1 == .75);
    rmm_placed_text piece = {RMM_STR("recognized"), sheet.strips[0].in_sheet};
    rmm_line line = {&piece, 1};
    size_t number = 7;
    rmm_readings *reading = nullptr;
    CHECK(rmm_text_brought(&line, 1, sheet.strips, sheet.count, &number, 1, &reading, nullptr) ==
          RMM_OK);
    CHECK(reading->count == 1 && reading->items[0].band == 7);
    CHECK(reading->items[0].lines[0].pieces[0].box.y0 == boxes[0].y0);
    CHECK(reading->items[0].lines[0].pieces[0].box.y1 == boxes[0].y1);
    rmm_readings_free(reading);
    rmr_mark_sheet out = sheet;
    rmr_limits small = rmr_limits_default();
    small.maximum_plan_bytes = 1;
    CHECK(native(mark_sheet)(&page, boxes, 2, 0, 1, &small, &out, nullptr) == RMR_ERR_LIMIT);
    CHECK(out.bitmap.pixels == sheet.bitmap.pixels);
    boxes[0].x1 = -1;
    CHECK(native(mark_sheet)(&page, boxes, 2, 0, 1, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    CHECK(out.bitmap.pixels == sheet.bitmap.pixels);
    rmr_mark_sheet_free(&sheet);
    CHECK(!sheet.bitmap.pixels && !sheet.strips && !sheet.count);
    OK(native(mark_sheet)(&page, nullptr, 0, 0, 1, nullptr, &sheet, nullptr));
    CHECK(!sheet.bitmap.pixels && !sheet.count);
    boxes[0] = (rmm_page_box){.25, .25, .75, .75};
    OK(native(mark_sheet)(&page, boxes, 1, 1, 0, nullptr, &sheet, nullptr));
    CHECK(sheet.bitmap.width == 4 && sheet.bitmap.height == 4);
    rmr_mark_sheet_free(&sheet);
    rmm_page_box around = rmr_mark_around((rmm_page_box){.1, .1, .9, .9}, 1);
    CHECK(around.x0 == 0 && around.y0 == 0 && around.x1 == 1 && around.y1 == 1);
}

int main(int argc, char **argv) {
    marks_contract();
    sheet_contract();
    CHECK(argc == 2);
    native(fonts) *fonts = nullptr;
    rmr_error error = {0};
    OK(native(fonts_create_directory)(argv[1], nullptr, &fonts, &error));
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
    native(layout) *layout = nullptr;
    OK(native(layout_create)(&text, 600, fonts, nullptr, &layout, &error));
    const rmr_anchor_layout *anchors = native(layout_anchors)(layout);
    CHECK(anchors->paragraphs[0].line_count == 7);
    /* Independent CoreText geometry for the style-overlay/soft-wrap fixture. */
    const double starts[] = {-230.192, -241.136, -244.912, -237.616, -234.608, -235.216, -217.552};
    const double ends[] = {-202, -194.4288, -202, -194.4288, -202, -202.96, -202};
    for (size_t li = 0; li < 7; li++) {
        rmr_text_line measured;
        OK(native(layout_line)(layout, 0, li, &measured, nullptr));
        CHECK(fabs(measured.start - starts[li]) < CARET_TOLERANCE);
        CHECK(fabs(measured.end - ends[li]) < CARET_TOLERANCE);
    }
    rmr_caret_offsets offsets;
    OK(anchors->caret->measure(anchors->caret->context, 0, 0, 0, &offsets));
    CHECK(fabs(offsets.at_start - 28.192) < CARET_TOLERANCE);
    rm_text_anchor anchor = {.paragraph = 0, .offset = 3};
    rmr_point pen;
    OK(rmr_lines_anchor_pen(anchors, &anchor, nullptr, &pen, &error));
    CHECK(fabs(pen.y - 209.15) < 1e-8);
    native(layout_free)(layout);
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
    OK(native(page_plan)(&page, (rmr_point){600, 400}, RMR_EDGE_RIBBON, 0, nullptr, fonts, nullptr,
                         &plan, &error));
    CHECK(rmr_plan_count(plan) >= 3);
    rmr_rect viewport = rmr_plan_viewport(plan);
    CHECK(viewport.height == 400);
    rmr_plan_free(plan);
    plan = nullptr;
    OK(native(page_plan)(&page, (rmr_point){600, 400}, RMR_EDGE_UNCUT, 0, nullptr, fonts, nullptr,
                         &plan, &error));
    viewport = rmr_plan_viewport(plan);
    CHECK(viewport.height > 600);
    rmr_page_marks *marks = nullptr;
    OK(native(page_marks)(&page, (rmr_point){600, 400}, viewport, (rm_str){"page", 4}, fonts,
                          nullptr, &marks, &error));
    const rmm_marked_page *view = rmr_page_marks_view(marks);
    CHECK(!rmm_page_is_empty(view) && view->band_count == 1 && view->highlight_count == 1);
    CHECK(view->bands[0].item.length == 3 && !memcmp(view->bands[0].item.bytes, "1:2", 3));
    CHECK(view->highlights[0].text.length == 9);
    CHECK(fabs(view->bands[0].box.x0 - fmin(1, fmax(0, (pen.x - viewport.x) / viewport.width))) <
          .0001);
    CHECK(fabs(view->bands[0].box.y0 -
               fmin(1, fmax(0, (pen.y - 5 - viewport.y) / viewport.height))) < .0001);
    native(fonts_free)(fonts);
    rmr_bitmap bitmap = {0};
    OK(native(bitmap)(plan, .5, (rmr_color){1, 1, 1, 1}, nullptr, nullptr, nullptr, &bitmap,
                      &error));
    size_t ink = 0;
    for (size_t i = 0; i < bitmap.height * bitmap.stride; i += 4)
        if (bitmap.pixels[i] < 200)
            ink++;
    CHECK(ink > 100);
    rmm_page_box blocks[] = {view->bands[0].box, view->highlights[0].band.box};
    rmr_mark_sheet sheet = {0};
    OK(native(mark_sheet)(&bitmap, blocks, 2, 2, 3, nullptr, &sheet, &error));
    CHECK(sheet.count == 2 && sheet.bitmap.height > 6);
    CHECK(sheet.bitmap.pixels[0] == 255);
    CHECK(sheet.bitmap.pixels[(sheet.bitmap.height - 1) * sheet.bitmap.stride] == 0);
    rmr_mark_sheet_free(&sheet);
    rmr_page_marks_free(marks);
    rmr_bitmap_free(&bitmap);
    rmr_buffer pdf = {0};
    const rmr_plan *plans[] = {plan};
    OK(native(pdf)(plans, 1, RMR_POINTS_PER_UNIT, nullptr, nullptr, nullptr, &pdf, &error));
    CHECK(pdf.size > 1000);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
#ifdef RMR_TEST_LINUX
    FcFini();
#endif
    return 0;
}
