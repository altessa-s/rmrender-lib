/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Own synthetic vector-template protocol, with no template/document assets. */
#include <rmrender/apple.h>
#include <rmrender/template.h>
#include <stdio.h>
#include <string.h>
static rmr_expression *compile(const char *s) {
    rmr_expression *p = nullptr;
    if (rmr_expression_compile(s, strlen(s), nullptr, &p, nullptr) != RMR_OK)
        return nullptr;
    return p;
}
int main(void) {
    const char *sources[] = {"parentWidth",
                             "parentHeight",
                             "parentWidth / 2",
                             "parentHeight / 2",
                             "parentWidth / 4",
                             "parentWidth * .75",
                             "cellWidth",
                             "cellHeight",
                             "base * 2",
                             "pen",
                             "offset + paperOriginX"};
    rmr_expression *expressions[11];
    rmr_template_value e[11];
    for (size_t i = 0; i < 11; i++) {
        expressions[i] = compile(sources[i]);
        if (!expressions[i])
            return 1;
        e[i] = (rmr_template_value){.expression = expressions[i]};
    }
    int result = 0;
    for (;;) {
        unsigned shape, nested, rk, ck, hf, hw, hc;
        double rows, cols, x, y, w, h, width, cw, ch, ew, eh;
        rmr_color fill, stroke;
        int n = scanf("%u %u %u %u %lf %lf %lf %lf %lf %lf %u %u %u %lf %lf %lf %lf %lf %lf %lf "
                      "%lf %lf %lf %lf %lf %lf",
                      &shape, &nested, &rk, &ck, &rows, &cols, &x, &y, &w, &h, &hf, &hw, &hc,
                      &width, &fill.red, &fill.green, &fill.blue, &fill.alpha, &stroke.red,
                      &stroke.green, &stroke.blue, &stroke.alpha, &cw, &ch, &ew, &eh);
        if (n == EOF)
            break;
        if (n != 26 || shape > 3) {
            result = 2;
            break;
        }
        rmr_template_element path[5] = {{.kind = RMR_MOVE}};
        size_t count;
        if (shape == 0) {
            path[1] = (rmr_template_element){.kind = RMR_LINE, .x = e[0], .y = e[1]};
            count = 2;
        } else if (shape == 1) {
            path[1] = (rmr_template_element){.kind = RMR_LINE, .x = e[0]};
            path[2] = (rmr_template_element){.kind = RMR_LINE, .x = e[0], .y = e[1]};
            path[3] = (rmr_template_element){.kind = RMR_LINE, .y = e[1]};
            path[4].kind = RMR_CLOSE;
            count = 5;
        } else if (shape == 2) {
            path[0].y = e[3];
            path[1] = (rmr_template_element){.kind = RMR_CURVE,
                                             .x = e[0],
                                             .y = e[3],
                                             .control1_x = e[4],
                                             .control2_x = e[5],
                                             .control2_y = e[1]};
            count = 2;
        } else {
            path[0].y = e[1];
            path[1] = (rmr_template_element){.kind = RMR_LINE, .x = e[2]};
            path[2] = (rmr_template_element){.kind = RMR_LINE, .x = e[0], .y = e[1]};
            path[3].kind = RMR_CLOSE;
            count = 4;
        }
        rmr_template_item leaf = {.kind = RMR_TEMPLATE_PATH,
                                  .path = {.elements = path,
                                           .count = count,
                                           .has_fill = hf != 0,
                                           .has_stroke_width = hw != 0,
                                           .has_stroke_color = hc != 0,
                                           .fill = fill,
                                           .stroke = stroke,
                                           .stroke_width = e[9]}};
        rmr_template_item inner = {
            .kind = RMR_TEMPLATE_GROUP,
            .group = {
                .box = {.x = {.number = 2}, .y = {.number = 3}, .width = e[2], .height = e[3]},
                .children = &leaf,
                .count = 1}};
        rmr_template_item root = {
            .kind = RMR_TEMPLATE_GROUP,
            .group = {.box = {.x = e[10], .y = {.number = y}, .width = e[6], .height = e[7]},
                      .rows = {.kind = (rmr_repeat_kind)rk, .count = {.number = rows}},
                      .columns = {.kind = (rmr_repeat_kind)ck, .count = {.number = cols}},
                      .children = nested ? &inner : &leaf,
                      .count = 1}};
        rmr_template_constant constants[] = {{"cellWidth", 9, {.number = w}},
                                             {"cellHeight", 10, {.number = h}},
                                             {"base", 4, {.number = width / 2}},
                                             {"pen", 3, e[8]},
                                             {"offset", 6, {.number = x}}};
        rmr_template t = {constants, 5, &root, 1};
        rmr_plan *p = nullptr;
        rmr_error error = {0};
        if (rmr_template_plan(&t, (rmr_point){cw, ch}, (rmr_point){ew, eh},
                              (rmr_rect){-cw / 2, 0, ew, eh}, nullptr, &p, &error) != RMR_OK) {
            fprintf(stderr, "%s\n", error.message);
            result = 3;
            break;
        }
        rmr_bitmap bitmap = {0};
        if (rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, &error) !=
            RMR_OK) {
            rmr_plan_free(p);
            result = 4;
            break;
        }
        for (size_t i = 0; i < bitmap.stride * bitmap.height; i++)
            printf("%02x", bitmap.pixels[i]);
        putchar('\n');
        rmr_bitmap_free(&bitmap);
        rmr_plan_free(p);
    }
    for (size_t i = 0; i < 11; i++)
        rmr_expression_free(expressions[i]);
    return result;
}
