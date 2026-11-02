/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "image_fixture.h"
#include "test.h"
#include "text_metrics_cases.h"
#include "text_metrics_reference.h"
#include <fontconfig/fontconfig.h>
#include <fpdf_text.h>
#include <fpdfview.h>
#include <rmlines/writer.h>
#include <rmrender/linux_document.h>
static void archive_text(const rmr_linux_fonts *fonts) {
    const char *doc = "11111111-1111-1111-1111-111111111111",
               *id = "22222222-2222-2222-2222-222222222222";
    rm_draft_paragraph paragraph = {.text = "Archive text Привет", .bold = true};
    rm_draft_image picture = {.file = "33333333-3333-3333-3333-333333333333.png",
                              .key = {1, 2, 3},
                              .x = -100,
                              .y = 200,
                              .width = 80,
                              .height = 80};
    rm_page_draft draft = {.sheet = {1404, 1872, 1404, 1872},
                           .column = rm_draft_column_of(RM_COLUMN_MEDIUM),
                           .paragraphs = &paragraph,
                           .paragraph_count = 1,
                           .images = &picture,
                           .image_count = 1};
    uint8_t *bytes = nullptr;
    size_t size = 0;
    CHECK(rm_write_page(&draft, &bytes, &size, nullptr) == RM_OK);
    char content[256], name1[64], name2[128], name3[180];
    snprintf(content, sizeof content, "{\"cPages\":{\"pages\":[{\"id\":\"%s\"}]}}", id);
    snprintf(name1, sizeof name1, "%s.content", doc);
    snprintf(name2, sizeof name2, "%s/%s.rm", doc, id);
    snprintf(name3, sizeof name3, "%s/%s/%s", doc, id, picture.file);
    rmd_file files[] = {{{name1, strlen(name1)}, {(const uint8_t *)content, strlen(content)}},
                        {{name2, strlen(name2)}, {bytes, size}},
                        {{name3, strlen(name3)}, {image_png, sizeof image_png}}};
    rmd_archive archive = {.files = files, .count = 3};
    rmr_archive_document *document = nullptr;
    rmr_error error = {0};
    OK(rmr_archive_document_create(&archive, doc, true, nullptr, &document, &error));
    rmr_buffer pdf = {0};
    rmr_document_report report = {0};
    OK(rmr_linux_document_pdf(document, nullptr, fonts, RMR_PAPER_A4, 0, nullptr, nullptr, nullptr,
                              &pdf, &report, &error));
    CHECK(report.rendered_pages == 1);
    FPDF_DOCUMENT output = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(output);
    FPDF_PAGE page = FPDF_LoadPage(output, 0);
    CHECK(page);
    FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
    CHECK(text && FPDFText_CountChars(text) >= 18);
    FPDFText_ClosePage(text);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(output);
    rmr_buffer_free(&pdf);
    rmr_archive_document_free(document);
    rm_bytes_free(bytes);
}
/* Independent measurements from the Apple backend using the same open fonts. */
static void calibrated_metrics(const rmr_linux_fonts *fonts) {
    static const double hello[2][2][2] = {{{164.9420192, 164.0710272}, {172.6564096, 172.6564096}},
                                          {{167.1887872, 167.1174272}, {172.6564096, 172.6564096}}};
    for (unsigned face = 0; face < 2; face++)
        for (unsigned bold = 0; bold < 2; bold++)
            for (unsigned italic = 0; italic < 2; italic++) {
                double width = 0;
                OK(rmr_linux_text_width((rm_str){"Hello world", 11}, 32, (rmr_type_face)face,
                                        bold != 0, italic != 0, false, fonts, nullptr, &width,
                                        nullptr));
                CHECK(fabs(width - hello[face][bold][italic]) < .02);
            }
    const char *sample = "Hello world Hello world Hello world";
    rm_paragraph paragraph = {.text = {sample, strlen(sample)}};
    rm_text_block block = {.origin_x = -100,
                           .origin_y = 10,
                           .paragraphs = &paragraph,
                           .paragraph_count = 1,
                           .has_column_width = true,
                           .column_width = 200};
    rmr_linux_layout *layout = nullptr;
    OK(rmr_linux_layout_create(&block, 200, fonts, nullptr, &layout, nullptr));
    const rmr_anchor_layout *a = rmr_linux_layout_anchors(layout);
    CHECK(a->paragraphs[0].line_count == 3);
    const rmr_text_line expected[] = {{0, 0, 12, 43.6, -96, 76.5120032},
                                      {0, 12, 24, 89.15, -96, 76.5120032},
                                      {0, 24, 35, 134.7, -96, 68.9420192}};
    for (size_t i = 0; i < 3; i++) {
        rmr_text_line line;
        OK(rmr_linux_layout_line(layout, 0, i, &line, nullptr));
        CHECK(line.from == expected[i].from && line.to == expected[i].to);
        CHECK(fabs(line.baseline - expected[i].baseline) < 1e-6);
        CHECK(fabs(line.start - expected[i].start) < 1e-6);
        CHECK(fabs(line.end - expected[i].end) < .01);
    }
    const double carets[] = {0,           22.9698144, 40.452912,   48.7496992,
                             57.3456864,  76.1093184, 83.6345024,  107.7544448,
                             126.2188768, 138.006816, 145.5788032, 164.9420192};
    for (size_t i = 0; i < 12; i++) {
        rmr_caret_offsets c;
        OK(a->caret->measure(a->caret->context, 0, 0, i, &c));
        CHECK(fabs(c.at_index - carets[i]) < .02);
        CHECK(c.at_start == 0);
    }
    rmr_caret_offsets end_caret;
    OK(a->caret->measure(a->caret->context, 0, 0, 12, &end_caret));
    CHECK(fabs(end_caret.at_index - 173.0016032) < .01);
    rmr_plan *text_plan = nullptr;
    OK(rmr_linux_layout_plan(layout, (rmr_rect){-100, 0, 200, 160}, nullptr, &text_plan, nullptr));
    const rmr_plan *plans[] = {text_plan};
    rmr_buffer pdf = {0};
    OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc);
    FPDF_PAGE pdf_page = FPDF_LoadPage(doc, 0);
    CHECK(pdf_page);
    FPDF_TEXTPAGE text_page = FPDFText_LoadPage(pdf_page);
    CHECK(text_page && FPDFText_GetUnicode(text_page, 0) == 'H');
    double x, y;
    CHECK(FPDFText_GetCharOrigin(text_page, 0, &x, &y));
    CHECK(fabs(x - 4) < .02 && fabs(y - 116.4) < .02);
    CHECK(FPDFText_GetCharOrigin(text_page, 1, &x, &y));
    CHECK(fabs(x - 26.9698144) < .02);
    FPDFText_ClosePage(text_page);
    FPDF_ClosePage(pdf_page);
    FPDF_CloseDocument(doc);
    rmr_buffer_free(&pdf);
    rmr_plan_free(text_plan);
    rmr_linux_layout_free(layout);
    /* The uncalibrated string is too wide; the calibrated string fits. */
    paragraph.text = (rm_str){"Hello world", 11};
    block.column_width = 173.2;
    layout = nullptr;
    OK(rmr_linux_layout_create(&block, 200, fonts, nullptr, &layout, nullptr));
    CHECK(rmr_linux_layout_anchors(layout)->paragraphs[0].line_count == 1);
    rmr_linux_layout_free(layout);
    static const double pair_widths[3][2][2] = {
        {{234.387942, 232.102131}, {248.327654, 246.215654}},
        {{238.649677, 232.783731}, {248.327654, 246.215654}},
        {{247.398992, 234.228966}, {265.554812, 262.150650}}};
    for (unsigned face = 0; face < 3; face++)
        for (unsigned bold = 0; bold < 2; bold++)
            for (unsigned italic = 0; italic < 2; italic++) {
                double width;
                OK(rmr_linux_text_width((rm_str){"AVATAR office ffi", 17}, 32, (rmr_type_face)face,
                                        bold != 0, italic != 0, false, fonts, nullptr, &width,
                                        nullptr));
                CHECK(fabs(width - pair_widths[face][bold][italic]) < .05);
            }
    paragraph.text = (rm_str){"ééé", 9};
    block.column_width = 40;
    layout = nullptr;
    OK(rmr_linux_layout_create(&block, 200, fonts, nullptr, &layout, nullptr));
    a = rmr_linux_layout_anchors(layout);
    CHECK(a->paragraphs[0].line_count > 1);
    for (size_t i = 0; i < a->paragraphs[0].line_count; i++) {
        CHECK(a->paragraphs[0].lines[i].location % 2 == 0);
        CHECK(a->paragraphs[0].lines[i].length % 2 == 0);
    }
    rmr_linux_layout_free(layout);
    rmr_limits limits = rmr_limits_default();
    limits.maximum_plan_bytes = 64;
    double untouched = 123;
    CHECK(rmr_linux_text_width((rm_str){"Hello world", 11}, 32, RMR_FACE_SANS, false, false, false,
                               fonts, &limits, &untouched, nullptr) == RMR_ERR_LIMIT);
    CHECK(untouched == 123);
}
static void multilingual_metrics(const rmr_linux_fonts *fonts) {
    const size_t count = sizeof apple_metric_widths / sizeof *apple_metric_widths;
    CHECK(count == 432);
    /* Reverse order also checks variation/font-cache independence. */
    for (size_t pass = 0; pass < 2; pass++)
        for (size_t k = 0; k < count; k++) {
            size_t i = pass ? count - k - 1 : k;
            size_t size_index = i / 108, sample = (i / 12) % 9;
            rmr_type_face face = (rmr_type_face)((i / 4) % 3);
            bool bold = (i / 2) % 2 != 0, italic = i % 2 != 0;
            double measured = 0;
            OK(rmr_linux_text_width((rm_str){metric_texts[sample], strlen(metric_texts[sample])},
                                    metric_sizes[size_index], face, bold, italic, false, fonts,
                                    nullptr, &measured, nullptr));
            double tolerance = .015;
            if (fabs(measured - apple_metric_widths[i]) >= tolerance)
                fprintf(stderr, "metric %zu: Apple %.9f Linux %.9f tolerance %.9f\n", i,
                        apple_metric_widths[i], measured, tolerance);
            CHECK(fabs(measured - apple_metric_widths[i]) < tolerance);
        }
}
static void synthetic_pdf_positions(const rmr_linux_fonts *fonts) {
    rm_face_run face = {.start = 0, .end = 6, .bold = true, .italic = true};
    rm_paragraph paragraph = {.text = {"AVATAR", 6}, .faces = &face, .face_count = 1};
    rm_text_block block = {.paragraphs = &paragraph,
                           .paragraph_count = 1,
                           .has_column_width = true,
                           .column_width = 200};
    rmr_linux_layout *layout = nullptr;
    OK(rmr_linux_layout_create(&block, 200, fonts, nullptr, &layout, nullptr));
    rmr_plan *plan = nullptr;
    OK(rmr_linux_layout_plan(layout, (rmr_rect){0, 0, 200, 100}, nullptr, &plan, nullptr));
    const rmr_plan *plans[] = {plan};
    rmr_buffer pdf = {0};
    OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc);
    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    CHECK(page);
    FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
    CHECK(text && FPDFText_GetUnicode(text, 1) == 'V');
    double x, y;
    CHECK(FPDFText_GetCharOrigin(text, 1, &x, &y));
    /* CoreText's A advance for synthetic bold italic is 19.4952192. */
    CHECK(fabs(x - 23.4952192) < .02);
    CHECK(fabs(y - 66.4) < .02);
    FPDFText_ClosePage(text);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
    rmr_linux_layout_free(layout);
}
static void rtl_pdf_positions(const rmr_linux_fonts *fonts) {
    const char *sample = "שלום ";
    rm_paragraph paragraph = {.text = {sample, strlen(sample)}};
    rm_text_block block = {.paragraphs = &paragraph,
                           .paragraph_count = 1,
                           .has_column_width = true,
                           .column_width = 200};
    rmr_linux_layout *layout = nullptr;
    OK(rmr_linux_layout_create(&block, 200, fonts, nullptr, &layout, nullptr));
    rmr_text_line visual;
    OK(rmr_linux_layout_line(layout, 0, 0, &visual, nullptr));
    rmr_plan *plan = nullptr;
    OK(rmr_linux_layout_plan(layout, (rmr_rect){0, 0, 200, 100}, nullptr, &plan, nullptr));
    const rmr_plan *plans[] = {plan};
    rmr_buffer pdf = {0};
    OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc);
    FPDF_PAGE page = FPDF_LoadPage(doc, 0);
    CHECK(page);
    FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
    CHECK(text);
    bool found = false;
    for (int i = 0; i < FPDFText_CountChars(text); i++) {
        if (FPDFText_GetUnicode(text, i) != 0x05dd)
            continue;
        double x, y;
        CHECK(FPDFText_GetCharOrigin(text, i, &x, &y));
        /* Final mem is the leftmost visible glyph; trailing whitespace must
         * not shift it when translating between CoreText and Pango origins. */
        CHECK(fabs(x - visual.start) < .02);
        CHECK(fabs(y - (100 - visual.baseline)) < .02);
        found = true;
    }
    CHECK(found);
    FPDFText_ClosePage(text);
    FPDF_ClosePage(page);
    FPDF_CloseDocument(doc);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
    rmr_linux_layout_free(layout);
}
int main(int argc, char **argv) {
    CHECK(argc == 2);
    rmr_linux_fonts *fonts = nullptr;
    rmr_error error = {0};
    OK(rmr_linux_fonts_create_directory(argv[1], nullptr, &fonts, &error));
    calibrated_metrics(fonts);
    multilingual_metrics(fonts);
    synthetic_pdf_positions(fonts);
    rtl_pdf_positions(fonts);
    const char *samples[] = {"Hello Cyrillic Привет",
                             "مرحبا بالعالم العربية",
                             "שלום עולם",
                             "日本語 한국어",
                             "नमस्ते दुनिया",
                             "ไทย",
                             "😀 combining é text"};
    rm_paragraph p[7] = {0};
    for (size_t i = 0; i < 7; i++)
        p[i].text = (rm_str){samples[i], strlen(samples[i])};
    rm_page page = {.has_text = true,
                    .text = {.origin_x = -180,
                             .origin_y = 10,
                             .paragraphs = p,
                             .paragraph_count = 7,
                             .has_column_width = true,
                             .column_width = 360}};
    rmr_linux_layout *layout = nullptr;
    OK(rmr_linux_layout_create(&page.text, 400, fonts, nullptr, &layout, &error));
    const rmr_anchor_layout *anchors = rmr_linux_layout_anchors(layout);
    CHECK(anchors->count == 7);
    CHECK(anchors->paragraphs[1].right_to_left);
    CHECK(anchors->paragraphs[2].right_to_left);
    for (size_t i = 0; i < 7; i++) {
        CHECK(anchors->paragraphs[i].line_count > 0);
        rmr_caret_offsets offsets;
        OK(anchors->caret->measure(anchors->caret->context, i, 0, 0, &offsets));
        CHECK(isfinite(offsets.at_start));
    }
    rmr_plan *plan = nullptr;
    OK(rmr_linux_page_plan(&page, (rmr_point){400, 1000}, RMR_EDGE_RIBBON, 0, nullptr, fonts,
                           nullptr, &plan, &error));
    archive_text(fonts);
    rmr_linux_fonts_free(fonts);
    rmr_linux_layout_free(layout);
    const rmr_plan *plans[] = {plan};
    rmr_buffer pdf = {0};
    OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, &error));
    FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
    CHECK(doc);
    FPDF_PAGE pp = FPDF_LoadPage(doc, 0);
    CHECK(pp);
    FPDF_TEXTPAGE text = FPDFText_LoadPage(pp);
    CHECK(text);
    CHECK(FPDFText_CountChars(text) > 50);
    CHECK(FPDFText_GetUnicode(text, 0) == 'H');
    FPDFText_ClosePage(text);
    FPDF_ClosePage(pp);
    FPDF_CloseDocument(doc);
    rmr_bitmap bitmap = {0};
    OK(rmr_linux_bitmap(plan, .5, (rmr_color){1, 1, 1, 1}, nullptr, nullptr, nullptr, &bitmap,
                        &error));
    size_t ink = 0;
    for (size_t i = 0; i < bitmap.height * bitmap.stride; i += 4)
        if (bitmap.pixels[i] < 200)
            ink++;
    CHECK(ink > 100);
    rmr_bitmap_free(&bitmap);
    rmr_buffer_free(&pdf);
    rmr_plan_free(plan);
    /* Standalone test owns the process: release Fontconfig global caches for LSan. */
    FcFini();
    return 0;
}
