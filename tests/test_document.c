/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "test.h"
#include <rmrender/document.h>
int main(void) {
    const char json[] = "{\"orientation\":{\"value\":\"landscape\"},\"customZoomPageWidth\":1404,"
                        "\"customZoomPageHeight\":1872,\"cPages\":{\"pages\":["
                        "{\"id\":\"b\",\"idx\":{\"value\":\"bb\"},\"redir\":{\"value\":1}},"
                        "{\"id\":\"dead\",\"deleted\":{\"value\":1}},"
                        "{\"id\":\"a\",\"idx\":{\"value\":\"ba\"},\"template\":{\"value\":\"Grid\"}"
                        ",\"redir\":{\"value\":1.5}},"
                        "{\"id\":\"b\",\"idx\":{\"value\":\"bc\"}}]}}";
    rmr_document_layout *d = nullptr;
    OK(rmr_document_layout_decode(json, sizeof json - 1, nullptr, &d, nullptr));
    CHECK(rmr_document_layout_count(d) == 2 && rmr_document_layout_deleted_count(d) == 1);
    CHECK(!strcmp(rmr_document_layout_page(d, 0)->id, "a"));
    CHECK(!strcmp(rmr_document_layout_page(d, 0)->template_name, "Grid"));
    CHECK(rmr_document_layout_page(d, 0)->refused_source);
    CHECK(rmr_document_layout_page(d, 1)->source_page == 1);
    CHECK(rmr_document_layout_landscape(d) && rmr_document_layout_canvas(d).x == 1404);
    rmr_document_page checked = rmr_document_page_checked(*rmr_document_layout_page(d, 1), 1);
    CHECK(!checked.has_source && checked.refused_source);
    for (size_t i = 0; i < sizeof json - 2; i++) {
        rmr_document_layout *sentinel = d;
        CHECK(rmr_document_layout_decode(json, i, nullptr, &sentinel, nullptr) != RMR_OK);
        CHECK(sentinel == d);
    }
    rmr_document_layout_free(d);
    const char flat[] = "{\"pages\":[\"x\",\"x\",\"y\"],\"redirectionPageMap\":[0,9,-1]}";
    OK(rmr_document_layout_decode(flat, sizeof flat - 1, nullptr, &d, nullptr));
    CHECK(rmr_document_layout_count(d) == 2 && rmr_document_layout_page(d, 0)->source_page == 0 &&
          rmr_document_layout_page(d, 1)->refused_source);
    rmr_document_layout_free(d);
    const char exact[] =
        "{\"pages\":[\"a\",\"b\",\"c\",\"d\",\"e\"],\"redirectionPageMap\":[9223372036854775807,1."
        "0000000000000000001,10e-1,1e-9999,9223372036854775808]}";
    OK(rmr_document_layout_decode(exact, sizeof exact - 1, nullptr, &d, nullptr));
    CHECK(rmr_document_layout_page(d, 0)->source_page == (size_t)INT64_MAX);
    CHECK(rmr_document_layout_page(d, 1)->refused_source);
    CHECK(rmr_document_layout_page(d, 2)->has_source &&
          rmr_document_layout_page(d, 2)->source_page == 1);
    CHECK(rmr_document_layout_page(d, 3)->refused_source &&
          rmr_document_layout_page(d, 4)->refused_source);
    rmr_document_layout_free(d);
    rmr_plan *a = rectangle((rmr_color){1, 0, 0, 1}, RMR_BLEND_NORMAL), *b = nullptr;
    const rmr_plan *parts[] = {a, a};
    OK(rmr_plan_concat(parts, 2, (rmr_rect){-32, 0, 64, 64}, nullptr, &b, nullptr));
    rmr_plan_free(a);
    CHECK(rmr_plan_count(b) == 2);
    rmr_plan_free(b);
    return 0;
}
