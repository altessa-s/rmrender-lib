/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <rmrender/rmlines.h>

typedef struct {
    size_t calls;
    rmr_point pen;
    rmr_status status;
} state;

static rmr_status resolve(void *context, const rm_text_anchor *anchor, rmr_point *out) {
    state *s = context;
    s->calls++;
    CHECK(anchor->paragraph == -2 && anchor->offset == INT32_MAX);
    *out = s->pen;
    return s->status;
}

int main(void) {
    rm_stroke_point points[] = {{.x = 1, .y = 2, .width = 8, .pressure = .5f, .speed = 31},
                                {.x = 2, .y = 3, .width = 12, .pressure = .8f, .speed = 32}};
    rm_text_anchor anchor = {.paragraph = -2, .offset = INT32_MAX};
    rm_stroke strokes[2] = {{.tool = RM_TOOL_FINELINER_2,
                             .thickness_scale = 2,
                             .points = points,
                             .point_count = 2,
                             .has_anchor = true,
                             .anchor = anchor,
                             .has_argb = true,
                             .argb = 0xff123456,
                             .id = {2, 55},
                             .has_source_id = true,
                             .source_id = {3, 66}}};
    strokes[1] = strokes[0];
    strokes[1].anchor.origin_x = NAN; /* ignored when flag is absent */
    char file[] = "own.png";
    rm_page_image image = {.file = {file, 7},
                           .vertices = {{1, 2, 0, 0}, {11, 2, 1, 0}, {11, 12, 1, 1}, {1, 12, 0, 1}},
                           .strokes_before = 1,
                           .has_anchor = true,
                           .anchor = anchor};
    state context = {.pen = {100.123456789, 200.123456789}};
    rmr_anchor_provider provider = {RMR_ANCHOR_PROVIDER_ABI, sizeof provider, &context, resolve};
    rmr_placed_scene *out = nullptr;

    OK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, nullptr, &out, nullptr));
    CHECK(context.calls == 1);
    CHECK(rmr_placed_scene_stroke_count(out) == 2 && rmr_placed_scene_image_count(out) == 1);
    const rm_stroke *placed = rmr_placed_scene_strokes(out);
    const rm_page_image *images = rmr_placed_scene_images(out);
    CHECK(!placed[0].has_anchor && !images[0].has_anchor && placed[0].anchor.offset == 0);
    CHECK(placed[0].points[0].x == 1 + (float)context.pen.x &&
          placed[0].points[0].y == 2 + (float)context.pen.y);
    CHECK(images[0].vertices[0].x == 1 + context.pen.x &&
          images[0].vertices[0].y == 2 + context.pen.y);
    CHECK(placed[0].points[0].pressure == .5f && placed[0].points[0].width == 8 &&
          placed[0].points[0].speed == 31);
    CHECK(placed[0].thickness_scale == 2 && placed[0].argb == 0xff123456 &&
          placed[0].id.counter == 55 && placed[0].source_id.counter == 66);
    CHECK(images[0].strokes_before == 1 && images[0].vertices[2].u == 1 &&
          images[0].vertices[2].v == 1);

    points[0].x = -999;
    file[0] = 'X';
    CHECK(placed[0].points[0].x > 100 && images[0].file.bytes[0] == 'o');

    rmr_placed_scene *again = nullptr;
    OK(rmr_lines_place_scene(placed, 2, images, 1, nullptr, nullptr, &again, nullptr));
    CHECK(!memcmp(rmr_placed_scene_strokes(again)[0].points, placed[0].points, 2 * sizeof *points));

    rmr_plan *plan = nullptr;
    OK(rmr_lines_plan(placed, 2, (rmr_rect){0, 0, 400, 400}, nullptr, &plan, nullptr));
    rmr_plan_free(plan);

    size_t retained = rmr_placed_scene_bytes(out);
    rmr_placed_scene_free(out);
    rmr_placed_scene_free(again);
    out = nullptr;

    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, nullptr, nullptr, &out, nullptr) ==
              RMR_ERR_UNSUPPORTED &&
          !out);

    provider.abi_version = 0;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);

    provider.abi_version = RMR_ANCHOR_PROVIDER_ABI;
    context.status = RMR_ERR_CANCELLED;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, nullptr, &out, nullptr) ==
              RMR_ERR_CANCELLED &&
          !out);

    context.status = RMR_OK;
    context.pen.x = NAN;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, nullptr, &out, nullptr) ==
          RMR_ERR_BACKEND);

    context.pen.x = 100;
    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 1;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, &limits, &out, nullptr) ==
              RMR_ERR_LIMIT &&
          !out);

    limits = rmr_limits_default();
    limits.maximum_points = 7;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, &limits, &out, nullptr) ==
          RMR_ERR_LIMIT);

    limits = rmr_limits_default();
    limits.maximum_plan_bytes = retained;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, &limits, &out, nullptr) ==
          RMR_ERR_LIMIT); /* temporary cache also counts */

    limits = rmr_limits_default();
    limits.maximum_coordinate = 1000;
    context.pen.x = 1000;
    points[0].x = 1;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, &limits, &out, nullptr) ==
          RMR_ERR_LIMIT);

    context.pen.x = 100;
    points[0].x = INFINITY;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);

    points[0].x = 1;
    strokes[0].anchor.has_origin_x = true;
    strokes[0].anchor.origin_x = NAN;
    CHECK(rmr_lines_place_scene(strokes, 2, &image, 1, &provider, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);

    strokes[0].anchor.origin_x = 0;
    strokes[1].anchor.has_origin_x = true;
    strokes[1].anchor.origin_x = -0.0;
    context.calls = 0;
    OK(rmr_lines_place_scene(strokes, 2, nullptr, 0, &provider, nullptr, &out, nullptr));
    CHECK(context.calls == 1);

    rmr_placed_scene_free(out);
    out = nullptr;

    strokes[1].anchor.origin_x = 1;
    context.calls = 0;
    OK(rmr_lines_place_scene(strokes, 2, nullptr, 0, &provider, nullptr, &out, nullptr));
    CHECK(context.calls == 2);
    rmr_placed_scene_free(out);

    rmr_error error = {.index = 123, .code = RMR_ERR_BACKEND};
    OK(rmr_lines_place_scene(nullptr, 0, nullptr, 0, nullptr, nullptr, &out, &error));
    CHECK(error.index == 123 && error.code == RMR_ERR_BACKEND);
    CHECK(rmr_placed_scene_strokes(out) == nullptr && rmr_placed_scene_images(out) == nullptr);

    rmr_placed_scene_free(out);
    rmr_placed_scene_free(nullptr);

    CHECK(rmr_placed_scene_bytes(nullptr) == 0);
    return 0;
}
