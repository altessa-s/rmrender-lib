/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"
#include <rmrender/template.h>

static rmr_expression *expr(const char *s) {
    rmr_expression *p = nullptr;
    OK(rmr_expression_compile(s, strlen(s), nullptr, &p, nullptr));
    return p;
}

static rmr_plan *plan(rmr_template *t) {
    rmr_plan *p = nullptr;
    OK(rmr_template_plan(t, (rmr_point){100, 200}, (rmr_point){100, 300},
                         (rmr_rect){-50, 0, 100, 300}, nullptr, &p, nullptr));
    return p;
}

static rmr_status begin(void *ctx, rmr_rect v) {
    (void)v;
    ++*(int *)ctx;
    return RMR_OK;
}

static rmr_status draw(void *ctx, const rmr_draw *d) {
    (void)ctx;
    (void)d;
    return RMR_OK;
}

static rmr_status end(void *ctx) {
    (void)ctx;
    return RMR_OK;
}

static void abort_draw(void *ctx) { (void)ctx; }

int main(void) {
    rmr_expression *step = expr("step"), *width = expr("parentWidth"), *twice = expr("step * 2");
    rmr_template_constant constants[] = {{"step", 4, {.number = 20}},
                                         {"twice", 5, {.expression = twice}}};
    rmr_template_element line[] = {{.kind = RMR_MOVE},
                                   {.kind = RMR_LINE, .x = {.expression = width}}};
    rmr_template_item child = {.kind = RMR_TEMPLATE_PATH, .path = {.elements = line, .count = 2}};
    rmr_template_item root = {.kind = RMR_TEMPLATE_GROUP,
                              .group = {.box = {.x = {.number = 10},
                                                .y = {.number = 5},
                                                .width = {.number = 80},
                                                .height = {.expression = step}},
                                        .rows = {.kind = RMR_REPEAT_FORWARD},
                                        .children = &child,
                                        .count = 1}};
    rmr_template t = {constants, 2, &root, 1};
    rmr_plan *p = plan(&t);
    CHECK(rmr_plan_count(p) == 15);
    CHECK(rmr_plan_draw(p, 0)->path.elements[0].end.x == -40);
    CHECK(rmr_plan_draw(p, 14)->path.elements[1].end.x == 40 &&
          rmr_plan_draw(p, 14)->path.elements[1].end.y == 285);
    CHECK(rmr_plan_draw(p, 0)->color.red == .752941 && rmr_plan_draw(p, 0)->line_width == .5);
    CHECK(rmr_plan_draw(p, 0)->join == RMR_JOIN_MITER &&
          rmr_plan_draw(p, 0)->cap == RMR_CAP_SQUARE);
    rmr_plan_free(p);
    root.group.box.y.number = 55;
    root.group.rows.kind = RMR_REPEAT_BACKWARD;
    p = plan(&t);
    CHECK(rmr_plan_count(p) == 4 && rmr_plan_draw(p, 3)->path.elements[0].end.y == -5);
    rmr_plan_free(p);
    root.group.rows = (rmr_template_repeat){.kind = RMR_REPEAT_COUNT, .count = {.number = 2.9}};
    p = plan(&t);
    CHECK(rmr_plan_count(p) == 2);
    rmr_plan_free(p);
    root.group.rows.count.number = INFINITY;
    p = plan(&t);
    CHECK(rmr_plan_count(p) == 1);
    rmr_plan_free(p);
    root.group.rows.count.number = 0;
    p = plan(&t);
    CHECK(rmr_plan_count(p) == 1);
    rmr_plan_free(p);
    root.group.rows.count.number = 10001;
    p = plan(&t);
    CHECK(rmr_plan_count(p) == 10000);
    rmr_plan_free(p);
    root.group.rows.count.number = 1;
    child.path.has_fill = true;
    child.path.fill = (rmr_color){1, 0, 0, .5};
    p = plan(&t);
    CHECK(rmr_plan_draw(p, 0)->mode == RMR_FILL);
    rmr_plan_free(p);
    child.path.has_stroke_width = true;
    child.path.stroke_width.number = 2;
    p = plan(&t);
    CHECK(rmr_plan_draw(p, 0)->mode == RMR_FILL_STROKE &&
          rmr_plan_draw(p, 0)->stroke_color.alpha == 1);
    int calls = 0;
    rmr_backend backend = {
        RMR_BACKEND_ABI, sizeof backend, RMR_CAP_FILL | RMR_CAP_STROKE, &calls, begin, draw, end,
        abort_draw};
    CHECK(rmr_plan_execute(p, &backend, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED &&
          !calls);
    backend.capabilities |= RMR_CAP_FILL_STROKE;
    CHECK(rmr_plan_execute(p, &backend, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED &&
          !calls);
    backend.capabilities |= RMR_CAP_MITER;
    OK(rmr_plan_execute(p, &backend, nullptr, nullptr, nullptr));
    CHECK(calls == 1);
    rmr_draw invalid = *rmr_plan_draw(p, 0);
    rmr_plan *rejected = nullptr;
    invalid.stroke_color.alpha = NAN;
    CHECK(rmr_plan_create(&invalid, 1, rmr_plan_viewport(p), nullptr, &rejected, nullptr) ==
              RMR_ERR_INVALID_ARGUMENT &&
          !rejected);
    invalid = *rmr_plan_draw(p, 0);
    invalid.group = 1;
    CHECK(rmr_plan_create(&invalid, 1, rmr_plan_viewport(p), nullptr, &rejected, nullptr) ==
              RMR_ERR_INVALID_ARGUMENT &&
          !rejected);
    rmr_plan *hidden = nullptr;
    OK(rmr_template_background_plan(nullptr, (rmr_point){0}, (rmr_point){0}, p, false, nullptr,
                                    &hidden, nullptr));
    CHECK(rmr_plan_count(hidden) == rmr_plan_count(p) &&
          rmr_plan_draw(hidden, 0)->path.elements != rmr_plan_draw(p, 0)->path.elements);
    rmr_plan_free(hidden);
    rmr_plan_free(p);
    p = nullptr;
    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 10;
    CHECK(rmr_template_plan(&t, (rmr_point){100, 200}, (rmr_point){100, 300},
                            (rmr_rect){-50, 0, 100, 300}, &limits, &p, nullptr) == RMR_ERR_LIMIT &&
          !p);
    /* A cyclic caller tree is bounded by group depth, including empty repeats. */
    root.group.children = &root;
    CHECK(rmr_template_plan(&t, (rmr_point){100, 200}, (rmr_point){100, 300},
                            (rmr_rect){-50, 0, 100, 300}, nullptr, &p, nullptr) == RMR_ERR_LIMIT &&
          !p);
    root.group.children = &child;
    root.group.count = 0;
    root.group.rows.count.number = 10000;
    root.group.columns = root.group.rows;
    limits.maximum_elements = 100;
    CHECK(rmr_template_plan(&t, (rmr_point){100, 200}, (rmr_point){100, 300},
                            (rmr_rect){-50, 0, 100, 300}, &limits, &p, nullptr) == RMR_ERR_LIMIT);
    /* Numeric-only fixture: deterministic allocation-failure coverage. */
    line[1].x = (rmr_template_value){.number = 50};
    t = (rmr_template){.items = &child, .count = 1};
    for (size_t i = 0; i < 5; i++) {
        rmr_test_fail_after(i);
        CHECK(rmr_template_plan(&t, (rmr_point){100, 200}, (rmr_point){100, 300},
                                (rmr_rect){-50, 0, 100, 300}, nullptr, &p,
                                nullptr) == RMR_ERR_OUT_OF_MEMORY &&
              !p);
    }
    rmr_test_fail_after(SIZE_MAX);
    line[0].kind = RMR_LINE;
    CHECK(rmr_template_plan(&t, (rmr_point){100, 200}, (rmr_point){100, 300},
                            (rmr_rect){-50, 0, 100, 300}, nullptr, &p,
                            nullptr) == RMR_ERR_INVALID_ARGUMENT);
    rmr_expression_free(step);
    rmr_expression_free(width);
    rmr_expression_free(twice);
    rmr_template empty = {0};
    limits = rmr_limits_default();
    limits.maximum_plan_bytes = sizeof(rmr_plan);
    OK(rmr_template_plan(&empty, (rmr_point){100, 200}, (rmr_point){100, 300},
                         (rmr_rect){-50, 0, 100, 300}, &limits, &p, nullptr));
    CHECK(rmr_plan_count(p) == 0 && rmr_plan_bytes(p) == sizeof(rmr_plan));
    rmr_plan_free(p);
    puts("template constants, repetitions, paint, capabilities, limits and ownership passed");
}
