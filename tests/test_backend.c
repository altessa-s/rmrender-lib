/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "test.h"
typedef struct {
    int began, drawn, ended, aborted, cancel_calls, fail, cancel_at;
} calls;
static rmr_status begin(void *ctx, rmr_rect v) {
    calls *c = ctx;
    CHECK(v.width == 64);
    c->began++;
    return c->fail == 1 ? RMR_ERR_BACKEND : RMR_OK;
}
static rmr_status draw(void *ctx, const rmr_draw *d) {
    calls *c = ctx;
    CHECK(d->path.count == 5);
    c->drawn++;
    return c->fail == 2 ? RMR_ERR_BACKEND : RMR_OK;
}
static rmr_status end(void *ctx) {
    calls *c = ctx;
    c->ended++;
    return c->fail == 3 ? RMR_ERR_BACKEND : RMR_OK;
}
static void abort_draw(void *ctx) { ((calls *)ctx)->aborted++; }
static bool cancel(void *ctx) {
    calls *c = ctx;
    return ++c->cancel_calls == c->cancel_at;
}
int main(void) {
    rmr_plan *plan = rectangle((rmr_color){1, 0, 0, 1}, RMR_BLEND_MULTIPLY);
    calls c = {0};
    rmr_backend b = {RMR_BACKEND_ABI, sizeof b, RMR_CAP_FILL, &c, begin, draw, end, abort_draw};
    CHECK(rmr_plan_execute(plan, &b, nullptr, nullptr, nullptr) == RMR_ERR_UNSUPPORTED);
    CHECK(c.began == 0);
    b.capabilities |= RMR_CAP_MULTIPLY;
    for (int fail = 0; fail < 4; fail++) {
        c = (calls){.fail = fail};
        rmr_status s = rmr_plan_execute(plan, &b, nullptr, nullptr, nullptr);
        CHECK(s == (fail ? RMR_ERR_BACKEND : RMR_OK));
        CHECK(c.began == 1 && c.aborted == (fail ? 1 : 0));
        CHECK(c.drawn == (fail == 1 ? 0 : 1));
        CHECK(c.ended == (fail == 1 || fail == 2 ? 0 : 1));
    }
    for (int at = 1; at <= 3; at++) {
        c = (calls){.cancel_at = at};
        CHECK(rmr_plan_execute(plan, &b, cancel, &c, nullptr) == RMR_ERR_CANCELLED);
        CHECK(c.began == (at == 1 ? 0 : 1));
        CHECK(c.aborted == (at == 1 ? 0 : 1));
        CHECK(!c.ended);
    }
    b.abi_version = 99;
    c = (calls){0};
    CHECK(rmr_plan_execute(plan, &b, nullptr, nullptr, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    CHECK(!c.began);
    rmr_plan_free(plan);
    puts("backend passed");
    return 0;
}
