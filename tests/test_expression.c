/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "rmr_internal.h"
#include "test.h"
#include <locale.h>
#include <rmrender/template.h>

static double eval(const char *s) {
    rmr_expression *p = nullptr;
    double v;
    rmr_binding b[] = {{"templateWidth", 13, 1404}, {"x", 1, 3}, {"x", 1, 7}, {"zero", 4, 0}};
    OK(rmr_expression_compile(s, strlen(s), nullptr, &p, nullptr));
    OK(rmr_expression_evaluate(p, b, 4, &v, nullptr));
    rmr_expression_free(p);
    return v;
}

int main(void) {
    CHECK(eval("2+3*4") == 14 && eval("(2+3)*4") == 20);
    CHECK(eval("templateWidth / 2 + x") == 709);
    CHECK(eval("10 - 3 - 2") == 5 && eval("20 / 2 / 5") == 2);
    CHECK(eval("1 || 0 && 0") == 1 && eval("2 < 3 && 4 >= 4") == 1);
    CHECK(eval("0 ? 1 : 1 ? 2 : 3") == 2 && eval("1 ? 0 ? 2 : 3 : 4") == 3);
    CHECK(eval("---2 + +.5") == -1.5 && eval("1.25 + 2.5") == 3.75);
    CHECK(eval("-7 / -zero") == 0 && !signbit(eval("-7 / -zero")));
    char source[] = "x+1";
    rmr_expression *p = nullptr;
    OK(rmr_expression_compile(source, 3, nullptr, &p, nullptr));
    source[0] = 'y';
    rmr_binding b = {"x", 1, 8};
    double out = 123;
    OK(rmr_expression_evaluate(p, &b, 1, &out, nullptr));
    CHECK(out == 9);
    out = 123;
    rmr_error e = {0};
    CHECK(rmr_expression_evaluate(p, nullptr, 0, &out, &e) == RMR_ERR_INVALID_ARGUMENT &&
          out == 123 && e.index == 0);
    rmr_test_fail_after(0);
    CHECK(rmr_expression_evaluate(p, &b, 1, &out, nullptr) == RMR_ERR_OUT_OF_MEMORY && out == 123);
    rmr_test_fail_after(SIZE_MAX);
    rmr_expression_free(p);
    p = nullptr;
    const char *bad[] = {"",     "1..2", "1e3",  "1 < 2 < 3", "!1",  "1?2",
                         "(1+2", "1 2",  "a(2)", "1 ? : 3",   "1 &&"};
    for (size_t i = 0; i < sizeof bad / sizeof *bad; i++)
        CHECK(rmr_expression_compile(bad[i], strlen(bad[i]), nullptr, &p, nullptr) ==
                  RMR_ERR_INVALID_ARGUMENT &&
              !p);
    CHECK(rmr_expression_compile("\xc3\xa9", 2, nullptr, &p, nullptr) == RMR_ERR_UNSUPPORTED);
    const char *eager[] = {"1 ? 2 : missing", "0 && missing", "1 || missing"};
    for (size_t i = 0; i < 3; i++) {
        OK(rmr_expression_compile(eager[i], strlen(eager[i]), nullptr, &p, nullptr));
        out = 123;
        CHECK(rmr_expression_evaluate(p, nullptr, 0, &out, nullptr) == RMR_ERR_INVALID_ARGUMENT &&
              out == 123);
        rmr_expression_free(p);
        p = nullptr;
    }
    for (size_t i = 0; i < 3; i++) {
        rmr_test_fail_after(i);
        CHECK(rmr_expression_compile("1+2", 3, nullptr, &p, nullptr) == RMR_ERR_OUT_OF_MEMORY &&
              !p);
    }
    rmr_test_fail_after(SIZE_MAX);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 2;
    CHECK(rmr_expression_compile("1+2", 3, &limits, &p, nullptr) == RMR_ERR_LIMIT);
    limits = rmr_limits_default();
    limits.maximum_plan_bytes = 1;
    CHECK(rmr_expression_compile("1", 1, &limits, &p, nullptr) == RMR_ERR_LIMIT);
    char nested[131];
    memset(nested, '(', 65);
    nested[65] = '1';
    memset(nested + 66, ')', 65);
    CHECK(rmr_expression_compile(nested, sizeof nested, nullptr, &p, nullptr) == RMR_ERR_LIMIT);
    CHECK(rmr_expression_compile("1", 65537, nullptr, &p, nullptr) == RMR_ERR_LIMIT);
    OK(rmr_expression_compile(nested + 1, sizeof nested - 2, nullptr, &p, nullptr));
    OK(rmr_expression_evaluate(p, nullptr, 0, &out, nullptr));
    CHECK(out == 1);
    rmr_expression_free(p);
    p = nullptr;
    OK(rmr_expression_compile("x", 1, nullptr, &p, nullptr));
    b.value = INFINITY;
    OK(rmr_expression_evaluate(p, &b, 1, &out, nullptr));
    CHECK(isinf(out));
    b.value = NAN;
    OK(rmr_expression_evaluate(p, &b, 1, &out, nullptr));
    CHECK(isnan(out));
    CHECK(rmr_expression_evaluate(p, &b, 257, &out, nullptr) == RMR_ERR_LIMIT);
    b.name = "1x";
    b.length = 2;
    CHECK(rmr_expression_evaluate(p, &b, 1, &out, nullptr) == RMR_ERR_INVALID_ARGUMENT);
    rmr_expression_free(p);
    /* No global-locale dependency: comma locales must still parse a decimal dot. */
    const char *current = setlocale(LC_NUMERIC, nullptr);
    char saved[128];
    CHECK(current && strlen(current) < sizeof saved);
    strcpy(saved, current);
    const char *locales[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8"};
    for (size_t i = 0; i < 3; i++)
        if (setlocale(LC_NUMERIC, locales[i])) {
            CHECK(eval("1.25+2.5") == 3.75);
            break;
        }
    CHECK(setlocale(LC_NUMERIC, saved));
    puts("expression precedence, ownership, eager branches, locale and limits passed");
}
