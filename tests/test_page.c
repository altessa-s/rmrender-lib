/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "test.h"
#include <rmrender/rmlines.h>
#ifdef RMRENDER_TEST_APPLE
#include <rmrender/apple.h>
#endif
typedef struct {
    const rmr_template *model;
    size_t calls;
    rmr_status status;
} templates;
static rmr_status resolve_template(void *context, rm_str name, const rmr_template **out) {
    templates *t = context;
    t->calls++;
    CHECK(name.length == 3 && !memcmp(name.bytes, "Own", 3));
    *out = t->model;
    return t->status;
}
static rmr_status missing_image(void *context, rm_str name, rmr_asset_view *out) {
    (void)name;
    ++*(size_t *)context;
    *out = (rmr_asset_view){0};
    return RMR_OK;
}
static void canvas_checks(void) {
    rmr_point out = {0};
    rm_page a = {0}, b = {.has_paper_size = true, .paper_size = {100, 200}};
    const rm_page *pages[] = {nullptr, &a, &b};
    OK(rmr_lines_document_canvas(pages, 3, (rmr_point){800, 600}, false, nullptr, &out, nullptr));
    CHECK(out.x == 100 && out.y == 200);
    OK(rmr_lines_document_canvas(pages, 3, (rmr_point){800, 600}, true, nullptr, &out, nullptr));
    CHECK(out.x == 200 && out.y == 100);
    a.has_paper_size = true;
    a.paper_size = (rm_size){50, 80};
    OK(rmr_lines_document_canvas(pages, 3, (rmr_point){0}, false, nullptr, &out, nullptr));
    CHECK(out.x == 50 && out.y == 80);
    a.paper_size.width = NAN;
    b.paper_size.height = 100001;
    OK(rmr_lines_document_canvas(pages, 3, (rmr_point){800, 600}, false, nullptr, &out, nullptr));
    CHECK(out.x == 800 && out.y == 600);
    OK(rmr_lines_document_canvas(pages, 3, (rmr_point){INFINITY, 600}, true, nullptr, &out,
                                 nullptr));
    CHECK(out.x == 1872 && out.y == 1404);
    OK(rmr_lines_document_canvas(nullptr, 0, (rmr_point){100000, 100000}, true, nullptr, &out,
                                 nullptr));
    CHECK(out.x == 100000 && out.y == 100000);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_elements = 2;
    out = (rmr_point){1, 2};
    CHECK(rmr_lines_document_canvas(pages, 3, (rmr_point){0}, false, &limits, &out, nullptr) ==
              RMR_ERR_LIMIT &&
          out.x == 1);
    CHECK(rmr_lines_document_canvas(nullptr, 1, (rmr_point){0}, false, nullptr, &out, nullptr) ==
          RMR_ERR_INVALID_ARGUMENT);
    OK(rmr_template_page_extent((rmr_point){100, 200}, (rmr_rect){-100, -20, 300, 420}, &out,
                                nullptr));
    CHECK(out.x == 250 && out.y == 400);
    OK(rmr_template_page_extent((rmr_point){100, 200}, (rmr_rect){-1e8, -1e8, 2e8, 2e8}, &out,
                                nullptr));
    CHECK(out.x == 100000 && out.y == 100000);
    OK(rmr_template_page_extent((rmr_point){100, 200}, (rmr_rect){-80, -20, 130, 220}, &out,
                                nullptr));
    CHECK(out.x == 100 && out.y == 200);
    double fractional = 990.8701741838819, left = 898.9821295774763;
    OK(rmr_template_page_extent((rmr_point){fractional, 200},
                                (rmr_rect){-fractional / 2 - left, 0, fractional + left, 200}, &out,
                                nullptr));
    CHECK(out.x == fractional && out.y == 200);
    CHECK(rmr_template_page_extent((rmr_point){100, 200}, (rmr_rect){-40, 0, 100, 200}, &out,
                                   nullptr) == RMR_ERR_INVALID_ARGUMENT);
}
int main(void) {
    canvas_checks();
    const char json[] =
        "{\"name\":\"Own\",\"orientation\":\"landscape\",\"items\":[{\"type\":\"group\","
        "\"boundingBox\":{\"height\":20},\"repeat\":{\"rows\":\"down\"},\"children\":[{\"type\":"
        "\"path\",\"fillColor\":\"#00ff00\",\"data\":[\"M\",0,0,\"L\",\"templateWidth\",0,\"L\","
        "\"templateWidth\",20,\"L\",0,20,\"Z\"]}]}]}";
    rmr_template_document *document = nullptr;
    OK(rmr_template_document_decode(json, strlen(json), nullptr, &document, nullptr));
    templates store = {.model = rmr_template_document_model(document)};
    size_t image_calls = 0;
    rmr_page_resources resources = {.template = resolve_template,
                                    .template_context = &store,
                                    .image = missing_image,
                                    .image_context = &image_calls};
    rm_stroke_point points[] = {{.x = -20, .y = 240, .width = 8}, {.x = 20, .y = 240, .width = 8}};
    rm_stroke stroke = {.tool = RM_TOOL_FINELINER_2, .points = points, .point_count = 2};
    rm_page page = {.strokes = &stroke,
                    .stroke_count = 1,
                    .has_template_name = true,
                    .template_name = {"Own", 3},
                    .background_visible = true};
    rmr_plan *p = nullptr;
    OK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                    nullptr, &p, nullptr));
    CHECK(store.calls == 1 && image_calls == 0);
    rmr_rect viewport = rmr_plan_viewport(p);
    CHECK(viewport.height > 240 && viewport.width == 100);
    size_t repeats = (size_t)ceil(viewport.height / 20);
    CHECK(rmr_plan_count(p) == repeats + 1);
    CHECK(rmr_plan_draw(p, 0)->color.green == 1 && rmr_plan_draw(p, repeats)->color.green == 0);
    CHECK(rmr_plan_draw(p, 0)->path.elements[1].end.x ==
          50); /* template orientation never turns canvas */
    size_t composed_bytes = rmr_plan_bytes(p);
    rmr_template_document_free(document);
    store.model = nullptr;
#ifdef RMRENDER_TEST_APPLE
    rmr_sheet sheet;
    OK(rmr_sheet_create(viewport, 100, 200, 1, &sheet, nullptr));
    rmr_bitmap bitmap = {0};
    OK(rmr_apple_bitmap_on_sheet(p, &sheet, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap,
                                 nullptr));
    CHECK(bitmap.height > 240 && bitmap.pixels[230 * bitmap.stride + 50 * 4 + 1] == 255);
    CHECK(bitmap.pixels[240 * bitmap.stride + 50 * 4 + 1] == 0);
    rmr_bitmap_free(&bitmap);
    OK(rmr_paper_sheet(RMR_PAPER_TABLET, 100, 200, viewport, &sheet, nullptr));
    const rmr_plan *pages[] = {p};
    rmr_buffer pdf = {0};
    OK(rmr_apple_pdf_sheets(pages, &sheet, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    CHECK(pdf.size > 100);
    rmr_buffer_free(&pdf);
#endif
    rmr_plan_free(p);
    p = nullptr;
    /* Missing model and hidden background keep the foreground and skip needless copies. */
    OK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                    nullptr, &p, nullptr));
    CHECK(rmr_plan_count(p) == 1 && store.calls == 2);
    size_t foreground_bytes = rmr_plan_bytes(p);
    rmr_plan_free(p);
    p = nullptr;
    page.background_visible = false;
    resources.template = nullptr;
    OK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                    nullptr, &p, nullptr));
    CHECK(store.calls == 2);
    rmr_plan_free(p);
    p = nullptr;
    page.background_visible = true;
    CHECK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                       nullptr, &p, nullptr) == RMR_ERR_UNSUPPORTED &&
          !p);
    resources.template = resolve_template;
    store.status = RMR_ERR_BACKEND;
    CHECK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                       nullptr, &p, nullptr) == RMR_ERR_BACKEND &&
          !p);
    store.status = RMR_OK;
    OK(rmr_template_document_decode(json, strlen(json), nullptr, &document, nullptr));
    store.model = rmr_template_document_model(document);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_plan_bytes = foreground_bytes + composed_bytes - 1;
    CHECK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                       &limits, &p, nullptr) == RMR_ERR_LIMIT &&
          !p);
    rm_page_image image = {
        .vertices = {{-10, 0, 0, 0}, {10, 0, 1, 0}, {10, 300, 1, 1}, {-10, 300, 0, 1}}};
    page.images = &image;
    page.image_count = 1;
    OK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                    nullptr, &p, nullptr));
    CHECK(image_calls == 1 && rmr_plan_viewport(p).height >= 300);
    rmr_plan_free(p);
    p = nullptr;
    page.has_text = true;
    CHECK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                       nullptr, &p, nullptr) == RMR_ERR_UNSUPPORTED &&
          !p);
    rmr_template_document_free(document);
    /* A named template can contain text; only its provider is platform-specific. */
    page.has_text = false;
    page.images = nullptr;
    page.image_count = 0;
    const char text_json[] = "{\"name\":\"Own\",\"items\":[{\"type\":\"text\",\"text\":\"Own "
                             "label\",\"position\":{\"x\":10,\"y\":30}}]}";
    OK(rmr_template_document_decode(text_json, strlen(text_json), nullptr, &document, nullptr));
    store.model = rmr_template_document_model(document);
    CHECK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                       nullptr, &p, nullptr) == RMR_ERR_UNSUPPORTED &&
          !p);
#ifdef RMRENDER_TEST_APPLE
    rmr_text_provider text_provider = rmr_apple_text_provider();
    resources.text = &text_provider;
    OK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                    nullptr, &p, nullptr));
    CHECK(rmr_plan_draw(p, 0)->mode == RMR_TEXT);
    rmr_plan_free(p);
    p = nullptr;
#endif
    page.template_name.length = 0;
    resources.template = nullptr;
    OK(rmr_lines_composed_page_plan(&page, (rmr_point){100, 200}, RMR_EDGE_UNCUT, 0, &resources,
                                    nullptr, &p, nullptr));
    rmr_plan_free(p);
    rmr_template_document_free(document);
    return 0;
}
