/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include <float.h>

#include "test.h"

int main(void) {
  rmr_point size;
  OK(rmr_paper_points(RMR_PAPER_A4, 1404, 1872, &size, nullptr));
  CHECK(size.x == 595.276 && size.y == 841.89);
  OK(rmr_paper_points(RMR_PAPER_LETTER, 1872, 1404, &size, nullptr));
  CHECK(size.x == 792 && size.y == 612);
  OK(rmr_paper_points(RMR_PAPER_TABLET, 820, 1458, &size, nullptr));
  CHECK(size.x == 820 * RMR_POINTS_PER_UNIT &&
        size.y == 1458 * RMR_POINTS_PER_UNIT);

  rmr_sheet s;
  OK(rmr_sheet_create((rmr_rect){-50, -10, 100, 240}, 120, 200, .5, &s,
                      nullptr));
  CHECK(s.width == 120 && s.height == 200 && s.transform.a == .5 &&
        s.transform.d == -.5);
  rmr_point top = rmr_transform_point(s.transform, (rmr_point){0, -10});
  CHECK(top.x == 60 && top.y == 200);
  rmr_point bottom = rmr_transform_point(s.transform, (rmr_point){0, 230});
  CHECK(bottom.y == 80);

  OK(rmr_sheet_create((rmr_rect){-50, 0, 100.5, 200.5}, 1, 1, 1, &s, nullptr));
  CHECK(s.width == 101 && s.height == 201 && s.transform.tx == 50.25 &&
        s.transform.ty == 201);

  OK(rmr_paper_sheet(RMR_PAPER_A4, 1404, 1872, (rmr_rect){-702, 0, 1404, 1872},
                     &s, nullptr));
  CHECK(s.width == 595 && s.height == 842 && s.transform.a == 595.276 / 1404);

  rmr_sheet grown;
  OK(rmr_paper_sheet(RMR_PAPER_A4, 1404, 1872,
                     (rmr_rect){-936, -20, 1872, 4000}, &grown, nullptr));
  CHECK(grown.transform.a == s.transform.a && grown.width > s.width &&
        grown.height > s.height);

  rmr_sheet original = s;
  CHECK(rmr_sheet_create((rmr_rect){0, 0, .1, .1}, .1, .1, 1, &s, nullptr) ==
        RMR_ERR_LIMIT);
  CHECK(memcmp(&s, &original, sizeof s) == 0);
  CHECK(rmr_sheet_create((rmr_rect){0, 0, DBL_MAX, 2}, 1, 1, 2, &s, nullptr) ==
        RMR_ERR_LIMIT);
  CHECK(rmr_paper_sheet(RMR_PAPER_A4, 0, 100, (rmr_rect){0, 0, 1, 1}, &s,
                        nullptr) == RMR_ERR_INVALID_ARGUMENT);
  CHECK(rmr_paper_points((rmr_paper)99, 100, 200, &size, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);
  CHECK(rmr_sheet_create((rmr_rect){NAN, 0, 1, 1}, 1, 1, 1, &s, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);
  CHECK(rmr_sheet_create((rmr_rect){0, 0, 1, 1}, 1, 1, -1, &s, nullptr) ==
        RMR_ERR_INVALID_ARGUMENT);

  puts("paper sizes, rounding, placement and errors passed");
}
