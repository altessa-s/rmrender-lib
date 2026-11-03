/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"

int main(void) {
  rmr_sample points[] = {{{0, 0}, 12}, {{100, 0}, 24}, {{101, 80}, 18}};
  rmr_path p = {0};
  OK(rmr_outline(points, 3, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &p,
                 nullptr));
  CHECK(p.count > 20 && p.elements[0].kind == RMR_MOVE &&
        p.elements[p.count - 1].kind == RMR_CLOSE);
  for (size_t i = 0; i < p.count; i++)
    CHECK(isfinite(p.elements[i].end.x) && isfinite(p.elements[i].end.y));
  rmr_path_free(&p);

  OK(rmr_outline(points, 1, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &p,
                 nullptr));
  CHECK(!p.count);
  points[1] = points[0];
  OK(rmr_outline(points, 2, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &p,
                 nullptr));
  CHECK(p.count == 12);
  rmr_path_free(&p);

  rmr_sample line[1000];
  for (size_t i = 0; i < 1000; i++) line[i] = (rmr_sample){{(double)i, 0}, 10};
  OK(rmr_outline(line, 1000, RMR_CAP_BUTT, RMR_JOIN_ROUND, nullptr, &p,
                 nullptr));
  CHECK(p.count < 250);
  rmr_path_free(&p);

  rmr_transform t;
  OK(rmr_page_to_pdf((rmr_rect){-702, 0, 1404, 1872}, RMR_POINTS_PER_UNIT, &t,
                     nullptr));
  rmr_point a = rmr_transform_point(t, (rmr_point){-702, 1872});
  CHECK(a.x == 0 && fabs(a.y) < 1e-10);
  a = rmr_transform_point(t, (rmr_point){702, 0});
  CHECK(fabs(a.x - 1404 * RMR_POINTS_PER_UNIT) < 1e-10);

  rmr_plan* plan = rectangle((rmr_color){1, 0, 0, 1}, RMR_BLEND_NORMAL);
  CHECK(rmr_plan_count(plan) == 1);
  CHECK(rmr_plan_draw(plan, 0)->path.elements[0].end.x == -20);
  CHECK(!rmr_plan_draw(plan, 1));
  rmr_rect bounds = rmr_plan_bounds(plan);
  CHECK(bounds.x == -20 && bounds.y == 5 && bounds.width == 20 &&
        bounds.height == 20);
  rmr_plan_free(plan);

  puts("geometry passed");
  return 0;
}
