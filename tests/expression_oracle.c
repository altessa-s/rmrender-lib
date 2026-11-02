/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <math.h>
#include <rmrender/template.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char source[65538];
    rmr_binding bindings[] = {{"x", 1, 7},
                              {"y", 1, -3},
                              {"templateWidth", 13, 1404},
                              {"templateHeight", 14, 1872},
                              {"parentWidth", 11, 640},
                              {"parentHeight", 12, 320},
                              {"zero", 4, 0},
                              {"nan", 3, NAN},
                              {"inf", 3, INFINITY}};
    while (fgets(source, sizeof source, stdin)) {
        size_t length = strlen(source);
        if (length && source[length - 1] == '\n')
            length--;
        rmr_expression *p = nullptr;
        double v;
        rmr_status s = rmr_expression_compile(source, length, nullptr, &p, nullptr);
        if (s == RMR_OK)
            s = rmr_expression_evaluate(p, bindings, sizeof bindings / sizeof *bindings, &v,
                                        nullptr);
        if (s == RMR_OK)
            printf("ok %.17g\n", v);
        else
            puts("error");
        rmr_expression_free(p);
    }
    return ferror(stdin) ? 1 : 0;
}
