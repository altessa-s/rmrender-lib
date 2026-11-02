/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "image_fixture.h"
#include "image_jpeg_fixture.h"
#include "pdf_fixture.h"
#include "test.h"
#include <fpdf_annot.h>
#include <fpdf_doc.h>
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#include <rmrender/linux_pdf.h>
static bool cancelled(void *ctx) {
    (void)ctx;
    return true;
}
static void graphics(void) {
    rmr_element shape[] = {{.kind = RMR_MOVE, .end = {0, 0}},
                           {.kind = RMR_LINE, .end = {3, 0}},
                           {.kind = RMR_LINE, .end = {3, 8}},
                           {.kind = RMR_LINE, .end = {0, 8}},
                           {.kind = RMR_CLOSE}};
    uint8_t bits[] = {0x0f, 0x0f, 255, 255, 255, 255, 255, 255};
    rmr_draw d = {
        .path = {shape, 5}, .color = {1, 0, 0, .5}, .mask = {bits, 8, 8, 1, {0, 0, 8, 8}}};
    rmr_plan *plan = nullptr;
    OK(rmr_plan_create(&d, 1, (rmr_rect){0, 0, 8, 8}, nullptr, &plan, nullptr));
    rmr_bitmap bitmap = {0};
    OK(rmr_linux_bitmap(plan, 4, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, nullptr));
    CHECK(bitmap.pixels[4 * bitmap.stride + 4 * 4 + 3] >= 127 &&
          bitmap.pixels[4 * bitmap.stride + 4 * 4 + 3] <= 128);
    CHECK(bitmap.pixels[4 * bitmap.stride + 14 * 4 + 3] == 0);
    CHECK(bitmap.pixels[24 * bitmap.stride + 4 * 4 + 3] == 0);
    rmr_bitmap_free(&bitmap);
    const rmr_plan *plans[] = {plan};
    rmr_buffer pdf = {0};
    OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc);
    CHECK(FPDF_GetPageCount(doc) == 1);
    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    FPDF_BITMAP bm = FPDFBitmap_Create(32, 32, 1);
    CHECK(bm);
    FPDFBitmap_FillRect(bm, 0, 0, 32, 32, 0);
    FPDF_RenderPageBitmap(bm, page, 0, 0, 32, 32, 0, 0);
    uint8_t *px = FPDFBitmap_GetBuffer(bm);
    CHECK(px[4 * 128 + 4 * 4 + 3] >= 126 && px[4 * 128 + 4 * 4 + 3] <= 129);
    CHECK(px[4 * 128 + 14 * 4 + 3] == 0);
    FPDFBitmap_Destroy(bm);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    rmr_element narrow[] = {{.kind = RMR_MOVE, .end = {0, 0}},
                            {.kind = RMR_LINE, .end = {1, 0}},
                            {.kind = RMR_LINE, .end = {1, 8}},
                            {.kind = RMR_LINE, .end = {0, 8}},
                            {.kind = RMR_CLOSE}};
    rmr_draw imported = {.mode = RMR_PDF,
                         .path = {narrow, 5},
                         .color = {0, 0, 0, 1},
                         .image = {pdf.bytes, pdf.size, {8, 0, 0, 8, 0, 0}}};
    rmr_plan *nested = nullptr;
    OK(rmr_plan_create(&imported, 1, (rmr_rect){0, 0, 8, 8}, nullptr, &nested, nullptr));
    OK(rmr_linux_bitmap(nested, 4, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, nullptr));
    CHECK(bitmap.pixels[4 * bitmap.stride + 2 * 4] >= 126 &&
          bitmap.pixels[4 * bitmap.stride + 2 * 4] <= 129);
    CHECK(bitmap.pixels[4 * bitmap.stride + 2 * 4 + 3] >= 126 &&
          bitmap.pixels[4 * bitmap.stride + 2 * 4 + 3] <= 129);
    CHECK(bitmap.pixels[4 * bitmap.stride + 8 * 4 + 3] == 0);
    rmr_bitmap_free(&bitmap);
    const rmr_plan *nested_pages[] = {nested};
    rmr_buffer nested_pdf = {0};
    OK(rmr_linux_pdf(nested_pages, 1, 1, nullptr, nullptr, nullptr, &nested_pdf, nullptr));
    doc = FPDF_LoadMemDocument64(nested_pdf.bytes, nested_pdf.size, nullptr);
    CHECK(doc);
    page = FPDF_LoadPage(doc, 0);
    CHECK(page);
    bm = FPDFBitmap_Create(32, 32, 1);
    CHECK(bm);
    FPDFBitmap_FillRect(bm, 0, 0, 32, 32, 0);
    FPDF_RenderPageBitmap(bm, page, 0, 0, 32, 32, 0, 0);
    px = FPDFBitmap_GetBuffer(bm);
    CHECK(px[4 * 128 + 2 * 4 + 3] >= 126 && px[4 * 128 + 2 * 4 + 3] <= 129);
    CHECK(px[4 * 128 + 8 * 4 + 3] == 0);
    FPDFBitmap_Destroy(bm);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    rmr_buffer_free(&nested_pdf);
    rmr_plan_free(nested);
    rmr_buffer_free(&pdf);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_bitmap_side = 2;
    CHECK(rmr_linux_bitmap(plan, 1, (rmr_color){0}, &limits, nullptr, nullptr, &bitmap, nullptr) ==
          RMR_ERR_LIMIT);
    CHECK(rmr_linux_pdf(plans, 1, 1, nullptr, cancelled, nullptr, &pdf, nullptr) ==
          RMR_ERR_CANCELLED);
    CHECK(!pdf.bytes);
    limits = rmr_limits_default();
    limits.maximum_pdf_bytes = 20;
    CHECK(rmr_linux_pdf(plans, 1, 1, &limits, nullptr, nullptr, &pdf, nullptr) == RMR_ERR_LIMIT);
    CHECK(!pdf.bytes);
    rmr_plan_free(plan);
    rmr_plan *base = rectangle((rmr_color){0, 0, 0, 1}, RMR_BLEND_NORMAL);
    rmr_element hole[] = {{.kind = RMR_MOVE, .end = {-12, 0}},
                          {.kind = RMR_LINE, .end = {-8, 0}},
                          {.kind = RMR_LINE, .end = {-8, 64}},
                          {.kind = RMR_LINE, .end = {-12, 64}},
                          {.kind = RMR_CLOSE}};
    rmr_path_view eraser = {hole, 5};
    rmr_draw draws[] = {*rmr_plan_draw(base, 0), *rmr_plan_draw(base, 0)};
    for (size_t i = 0; i < 2; i++) {
        draws[i].erasers = &eraser;
        draws[i].eraser_count = 1;
        draws[i].group = 1;
        draws[i].group_alpha = .5;
    }
    OK(rmr_plan_create(draws, 2, rmr_plan_viewport(base), nullptr, &plan, nullptr));
    rmr_plan_free(base);
    OK(rmr_linux_bitmap(plan, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, nullptr));
    CHECK(bitmap.pixels[10 * bitmap.stride + 22 * 4 + 3] == 0);
    CHECK(bitmap.pixels[10 * bitmap.stride + 15 * 4 + 3] >= 127 &&
          bitmap.pixels[10 * bitmap.stride + 15 * 4 + 3] <= 128);
    rmr_bitmap_free(&bitmap);
    rmr_plan_free(plan);
}
static void pdf_source(void) {
    fixture f = source(false);
    rmr_linux_pdf_source *src = nullptr;
    rmr_error error = {0};
    OK(rmr_linux_pdf_source_create((uint8_t *)f.bytes, f.n, nullptr, &src, &error));
    CHECK(rmr_linux_pdf_source_count(src) == 2);
    rmr_plan *plan = rectangle((rmr_color){1, 0, 0, 1}, RMR_BLEND_NORMAL);
    rmr_pdf_page pages[2];
    for (size_t i = 0; i < 2; i++)
        pages[i] = (rmr_pdf_page){
            .marks = plan,
            .sheet = {.width = 200, .height = 200, .transform = {1, 0, 0, -1, 100, 200}},
            .source_rect = {-100, 0, 200, 200},
            .source_page = i,
            .has_source = true,
            .source_visible = true};
    rmr_buffer pdf = {0};
    OK(rmr_linux_pdf_overlay(src, pages, 2, nullptr, nullptr, nullptr, &pdf, nullptr, &error));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc && FPDF_GetPageCount(doc) == 2);
    FPDF_CloseDocument(doc);
    rmr_buffer_free(&pdf);
    OK(rmr_linux_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr, &pdf, nullptr, &error));
    CHECK(pdf.size > f.n && !memcmp(pdf.bytes, f.bytes, f.n));
    FPDF_DOCUMENT updated = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(updated);
    FPDF_CloseDocument(updated);
    rmr_buffer_free(&pdf);
    pages[0].source_page = 1;
    CHECK(rmr_linux_pdf_incremental(src, pages, 2, nullptr, nullptr, nullptr, &pdf, nullptr,
                                    &error) == RMR_ERR_UNSUPPORTED);
    CHECK(!pdf.bytes);
    rmr_plan_free(plan);
    rmr_linux_pdf_source_free(src);
}
static void images(void) {
    rmr_element clip[] = {{.kind = RMR_MOVE, .end = {0, 0}},
                          {.kind = RMR_LINE, .end = {8, 0}},
                          {.kind = RMR_LINE, .end = {8, 8}},
                          {.kind = RMR_LINE, .end = {0, 8}},
                          {.kind = RMR_CLOSE}};
    for (size_t format = 0; format < 2; format++) {
        rmr_draw d = {.mode = RMR_IMAGE,
                      .path = {clip, 5},
                      .color = {0, 0, 0, 1},
                      .image = {format ? image_jpeg : image_png,
                                format ? sizeof image_jpeg : sizeof image_png,
                                {8, 0, 0, 8, 0, 0}}};
        rmr_plan *p = nullptr;
        OK(rmr_plan_create(&d, 1, (rmr_rect){0, 0, 8, 8}, nullptr, &p, nullptr));
        rmr_bitmap b = {0};
        OK(rmr_linux_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr));
        CHECK(b.pixels[1 * b.stride + 1 * 4] > 220);
        CHECK(b.pixels[1 * b.stride + 6 * 4 + 1] > 220);
        CHECK(b.pixels[6 * b.stride + 1 * 4 + 2] > 220);
        if (!format) {
            CHECK(b.pixels[6 * b.stride + 6 * 4 + 3] == 128);
            CHECK(b.pixels[6 * b.stride + 6 * 4] == 128);
        }
        rmr_bitmap_free(&b);
        const rmr_plan *plans[] = {p};
        rmr_buffer pdf = {0};
        OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
        CHECK(pdf.size > 100);
        rmr_buffer_free(&pdf);
        rmr_limits l = rmr_limits_default();
        l.maximum_bitmap_bytes = 100;
        CHECK(rmr_linux_bitmap(p, .5, (rmr_color){0}, &l, nullptr, nullptr, &b, nullptr) ==
              RMR_ERR_LIMIT);
        CHECK(!b.pixels);
        rmr_plan_free(p);
        d.image.size = 10;
        OK(rmr_plan_create(&d, 1, (rmr_rect){0, 0, 8, 8}, nullptr, &p, nullptr));
        CHECK(rmr_linux_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &b, nullptr) ==
              RMR_ERR_BACKEND);
        CHECK(!b.pixels);
        rmr_plan_free(p);
    }
}
static fixture rotated(int rotate, bool crop) {
    fixture f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, 0, "<< /Type /Catalog /Pages 2 0 R >>");
    obj(&f, 2, 0, "<< /Type /Pages /Kids [3 0 R] /Count 1 /MediaBox [10 20 210 320] >>");
    char body[512];
    snprintf(body, sizeof body,
             "<< /Type /Page /Parent 2 0 R /Rotate %d /Resources <<>> /Contents 4 0 R /Annots [5 0 "
             "R] %s >>",
             rotate, crop ? "/CropBox [30 50 170 270]" : "");
    obj(&f, 3, 0, body);
    stream(&f, 4, "1 0 0 rg 10 20 200 150 re f 0 0 1 rg 10 170 200 150 re f\n");
    obj(&f, 5, 0,
        "<< /Type /Annot /Subtype /Link /Rect [40 60 80 90] /A << /S /URI /URI "
        "(https://example.com/) >> >>");
    size_t start = f.n;
    add(&f, "xref\n0 6\n0000000000 65535 f \n");
    for (size_t i = 1; i < 6; i++)
        add(&f, "%010zu 00000 n \n", f.offsets[i]);
    add(&f, "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", start);
    return f;
}
static void rotations(void) {
    for (int rotation = 0; rotation < 360; rotation += 90)
        for (int crop = 0; crop < 2; crop++) {
            fixture f = rotated(rotation, crop != 0);
            rmr_linux_pdf_source *source_pdf = nullptr;
            rmr_error error = {0};
            OK(rmr_linux_pdf_source_create((uint8_t *)f.bytes, f.n, nullptr, &source_pdf, &error));
            double w = rotation % 180 ? 300 : 200, h = rotation % 180 ? 200 : 300;
            rmr_plan *empty = nullptr;
            OK(rmr_plan_create(nullptr, 0, (rmr_rect){0, 0, w, h}, nullptr, &empty, &error));
            rmr_pdf_page page = {
                .marks = empty,
                .sheet = {.width = w, .height = h, .transform = {1, 0, 0, -1, 0, h}},
                .source_rect = {0, 0, w, h},
                .has_source = true,
                .source_visible = true};
            rmr_buffer pdf = {0};
            rmr_pdf_report report = {0};
            OK(rmr_linux_pdf_overlay(source_pdf, &page, 1, nullptr, nullptr, nullptr, &pdf, &report,
                                     &error));
            CHECK(report.links_preserved == 1 && report.links_dropped == 0);
            FPDF_DOCUMENT a = FPDF_LoadMemDocument64(f.bytes, f.n, nullptr),
                          b = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
            CHECK(a && b);
            FPDF_PAGE pa = FPDF_LoadPage(a, 0), pb = FPDF_LoadPage(b, 0);
            CHECK(pa && pb);
            CHECK(fabs(FPDF_GetPageWidthF(pa) - FPDF_GetPageWidthF(pb)) < .01);
            CHECK(fabs(FPDF_GetPageHeightF(pa) - FPDF_GetPageHeightF(pb)) < .01);
            FPDF_BITMAP ba = FPDFBitmap_Create(60, 60, 1), bb = FPDFBitmap_Create(60, 60, 1);
            CHECK(ba && bb);
            FPDFBitmap_FillRect(ba, 0, 0, 60, 60, 0xffffffff);
            FPDFBitmap_FillRect(bb, 0, 0, 60, 60, 0xffffffff);
            FPDF_RenderPageBitmap(ba, pa, 0, 0, 60, 60, 0, 0);
            FPDF_RenderPageBitmap(bb, pb, 0, 0, 60, 60, 0, 0);
            const uint8_t *aa = FPDFBitmap_GetBuffer(ba), *ab = FPDFBitmap_GetBuffer(bb);
            size_t diff = 0;
            for (size_t k = 0; k < 60 * 60 * 4; k++)
                if (abs((int)aa[k] - (int)ab[k]) > 2)
                    diff++;
            CHECK(diff < 100);
            FPDFBitmap_Destroy(ba);
            FPDFBitmap_Destroy(bb);
            FPDF_ClosePage(pa);
            FPDF_ClosePage(pb);
            FPDF_CloseDocument(a);
            FPDF_CloseDocument(b);
            rmr_buffer_free(&pdf);
            rmr_plan_free(empty);
            rmr_linux_pdf_source_free(source_pdf);
        }
}
static fixture navigation_fixture(void) {
    fixture f = {0};
    add(&f, "%%PDF-1.7\n");
    obj(&f, 1, 0,
        "<< /Type /Catalog /Pages 2 0 R /Outlines 8 0 R /Names << /Dests << /Names [(second) [4 0 "
        "R /XYZ 30 80 null]] >> >> >>");
    obj(&f, 2, 0, "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 /MediaBox [0 0 100 100] >>");
    obj(&f, 3, 0,
        "<< /Type /Page /Parent 2 0 R /Contents 5 0 R /Resources <<>> /Annots [6 0 R 7 0 R] >>");
    obj(&f, 4, 0, "<< /Type /Page /Parent 2 0 R /Contents 5 0 R /Resources <<>> /Rotate 90 >>");
    stream(&f, 5, "0 0 1 rg 0 0 100 100 re f\n");
    obj(&f, 6, 0,
        "<< /Type /Annot /Subtype /Link /Rect [10 20 30 40] /Dest [4 0 R /XYZ 30 80 null] >>");
    obj(&f, 7, 0,
        "<< /Type /Annot /Subtype /Link /Rect [40 20 60 40] /A << /S /GoTo /D (second) >> >>");
    obj(&f, 8, 0, "<< /Type /Outlines /First 9 0 R /Last 9 0 R /Count 2 >>");
    obj(&f, 9, 0,
        "<< /Title <FEFF041F043B0430043D> /Parent 8 0 R /Dest [3 0 R /XYZ 0 100 null] /First 10 0 "
        "R /Last 10 0 R /Count -1 >>");
    obj(&f, 10, 0, "<< /Title (Child) /Parent 9 0 R /A << /S /GoTo /D (second) >> >>");
    size_t start = f.n;
    add(&f, "xref\n0 11\n0000000000 65535 f \n");
    for (size_t i = 1; i < 11; i++)
        add(&f, "%010zu 00000 n \n", f.offsets[i]);
    add(&f, "trailer\n<< /Size 11 /Root 1 0 R >>\nstartxref\n%zu\n%%%%EOF\n", start);
    return f;
}
static void navigation_test(void) {
    fixture f = navigation_fixture();
    rmr_linux_pdf_source *src = nullptr;
    OK(rmr_linux_pdf_source_create((uint8_t *)f.bytes, f.n, nullptr, &src, nullptr));
    rmr_plan *empty = nullptr;
    OK(rmr_plan_create(nullptr, 0, (rmr_rect){0, 0, 100, 100}, nullptr, &empty, nullptr));
    rmr_pdf_page pages[2];
    for (size_t i = 0; i < 2; i++)
        pages[i] = (rmr_pdf_page){.marks = empty,
                                  .sheet = {200, 200, {2, 0, 0, -2, 0, 200}},
                                  .source_rect = {0, 0, 100, 100},
                                  .source_page = 1 - i,
                                  .has_source = true,
                                  .source_visible = true};
    for (size_t count = 2; count > 0; count--) {
        rmr_buffer pdf = {0};
        rmr_pdf_report report = {0};
        const rmr_pdf_page *selected = count == 2 ? pages : pages + 1;
        OK(rmr_linux_pdf_overlay(src, selected, count, nullptr, nullptr, nullptr, &pdf, &report,
                                 nullptr));
        CHECK(report.links_preserved == (count == 2 ? 2u : 0u));
        CHECK(report.links_dropped == (count == 2 ? 0u : 2u));
        FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
        CHECK(doc);
        FPDF_BOOKMARK root = FPDFBookmark_GetFirstChild(doc, nullptr);
        CHECK(root);
        unsigned short title[16] = {0};
        CHECK(FPDFBookmark_GetTitle(root, title, sizeof title) == 10);
        CHECK(title[0] == 0x41f && title[3] == 0x43d);
        CHECK(FPDFBookmark_GetCount(root) == -1);
        FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc, root);
        CHECK(child);
        FPDF_DEST dest = FPDFBookmark_GetDest(doc, child);
        if (count == 2) {
            CHECK(dest && FPDFDest_GetDestPageIndex(doc, dest) == 0);
            FPDF_PAGE page = FPDF_LoadPage(doc, 1);
            CHECK(page);
            int pos = 0;
            FPDF_LINK link;
            size_t n = 0;
            while (FPDFLink_Enumerate(page, &pos, &link)) {
                dest = FPDFLink_GetDest(doc, link);
                CHECK(dest && FPDFDest_GetDestPageIndex(doc, dest) == 0);
                FPDF_BOOL hx, hy, hz;
                float x, y, z;
                CHECK(FPDFDest_GetLocationInPage(dest, &hx, &hy, &hz, &x, &y, &z));
                CHECK(hx && hy && fabs(x - 160) < .01 && fabs(y - 140) < .01);
                n++;
            }
            CHECK(n == 2);
            FPDF_ClosePage(page);
        } else
            CHECK(!dest);
        FPDF_CloseDocument(doc);
        rmr_buffer_free(&pdf);
    }
    rmr_plan_free(empty);
    rmr_linux_pdf_source_free(src);
}
static void contour_test(void) {
    fixture f = rotated(0, false);
    rmr_element clip[] = {
        {.kind = RMR_MOVE, .end = {10, 10}},
        {.kind = RMR_CURVE, .control1 = {90, 10}, .control2 = {90, 90}, .end = {10, 90}},
        {.kind = RMR_CLOSE},
        {.kind = RMR_MOVE, .end = {20, 40}},
        {.kind = RMR_LINE, .end = {20, 60}},
        {.kind = RMR_LINE, .end = {40, 60}},
        {.kind = RMR_LINE, .end = {40, 40}},
        {.kind = RMR_CLOSE}};
    rmr_draw draw = {.mode = RMR_PDF,
                     .path = {clip, 8},
                     .color = {0, 0, 0, 1},
                     .image = {(uint8_t *)f.bytes, f.n, {100, 0, 0, 100, 0, 0}}};
    rmr_plan *plan = nullptr;
    OK(rmr_plan_create(&draw, 1, (rmr_rect){0, 0, 100, 100}, nullptr, &plan, nullptr));
    rmr_buffer pdf = {0};
    const rmr_plan *plans[] = {plan};
    OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc);
    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    CHECK(page);
    CHECK(FPDFPage_CountObjects(page) > 0);
    CHECK(FPDFPageObj_GetType(FPDFPage_GetObject(page, 0)) == FPDF_PAGEOBJ_FORM);
    FPDF_BITMAP bm = FPDFBitmap_Create(100, 100, 1);
    CHECK(bm);
    FPDFBitmap_FillRect(bm, 0, 0, 100, 100, 0);
    FPDF_RenderPageBitmap(bm, page, 0, 0, 100, 100, 0, 0);
    const uint8_t *px = FPDFBitmap_GetBuffer(bm);
    int stride = FPDFBitmap_GetStride(bm);
    CHECK(px[50 * stride + 50 * 4 + 3] == 255);
    CHECK(px[50 * stride + 30 * 4 + 3] == 0); /* Opposite winding creates a hole. */
    CHECK(px[50 * stride + 80 * 4 + 3] == 0);
    CHECK(px[15 * stride + 50 * 4 + 3] == 0);
    FPDFBitmap_Destroy(bm);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
}
int main(void) {
    graphics();
    images();
    pdf_source();
    rotations();
    navigation_test();
    contour_test();
    return 0;
}
