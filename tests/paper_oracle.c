/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <rmrender/rmrender.h>
#include <stdio.h>
int main(void) {
    for (;;) {
        unsigned paper;
        double w, h;
        rmr_rect b;
        int n =
            scanf("%u %lf %lf %lf %lf %lf %lf", &paper, &w, &h, &b.x, &b.y, &b.width, &b.height);
        if (n == EOF)
            return 0;
        if (n != 7)
            return 1;
        rmr_point floor;
        rmr_sheet sheet;
        if (rmr_paper_points((rmr_paper)paper, w, h, &floor, nullptr) != RMR_OK ||
            rmr_paper_sheet((rmr_paper)paper, w, h, b, &sheet, nullptr) != RMR_OK)
            return 2;
        rmr_transform t = sheet.transform;
        printf("%.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g %.17g\n", floor.x, floor.y,
               sheet.width, sheet.height, t.a, t.b, t.c, t.d, t.tx, t.ty);
    }
}
