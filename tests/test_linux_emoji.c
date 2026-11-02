/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "../adapters/rmr_files.h"
#include "../backends/linux/rmr_typography_internal.h"
#include "test.h"
#include <fpdf_edit.h>
#include <fpdf_text.h>
#include <fpdfview.h>
static const char *samples[] = {
    "😀", "👍🏽", "🇫🇷", "👩‍💻", "👨‍👩‍👧‍👦", "❤️", "1️⃣", "🏳️‍🌈",
    "#⃣",  "*️⃣",    "9️⃣"};
static void shaping(rmr_linux_fonts *fonts) {
    const double sizes[] = {16, 32, 64};
    const char *contexts[] = {"%s", "x %s", "ש %s"};
    rmr_limits limits = rmr_limits_default();
    for (size_t i = 0; i < sizeof samples / sizeof *samples; i++)
        for (size_t size = 0; size < 3; size++)
            for (unsigned style = 0; style < 4; style++)
                for (size_t context = 0; context < 3; context++) {
                    char sample[128];
                    int length = snprintf(sample, sizeof sample, contexts[context], samples[i]);
                    CHECK(length > 0 && (size_t)length < sizeof sample);
                    PangoLayout *layout =
                        rmr_pango_layout(fonts, (rm_str){sample, (size_t)length}, sizes[size],
                                         RMR_FACE_SANS, (style & 1) != 0, (style & 2) != 0);
                    CHECK(layout);
                    OK(rmr_pango_calibrate(layout, &limits));
                    CHECK(pango_layout_get_unknown_glyphs_count(layout) == 0);
                    PangoLayoutLine *line = pango_layout_get_line_readonly(layout, 0);
                    size_t visible = 0;
                    for (GSList *it = line->runs; it; it = it->next) {
                        PangoGlyphItem *run = it->data;
                        PangoFontDescription *desc = pango_font_describe(run->item->analysis.font);
                        bool emoji = !strcmp(pango_font_description_get_family(desc), "Noto Emoji");
                        CHECK(emoji || context != 0);
                        pango_font_description_free(desc);
                        if (!emoji)
                            continue;
                        for (int g = 0; g < run->glyphs->num_glyphs; g++)
                            visible += run->glyphs->glyphs[g].glyph != PANGO_GLYPH_EMPTY;
                    }
                    CHECK(visible == 1);
                    g_object_unref(layout);
                }
}
static void composite_cluster(rmr_linux_fonts *fonts) {
    const char *text = "👩‍😀";
    PangoLayout *layout = rmr_pango_layout(fonts, (rm_str){text, strlen(text)}, 32,
                                           RMR_FACE_SANS, false, false);
    CHECK(layout);
    rmr_limits limits = rmr_limits_default();
    OK(rmr_pango_calibrate(layout, &limits));
    PangoLayoutLine *line = pango_layout_get_line_readonly(layout, 0);
    CHECK(line && line->runs && !line->runs->next);
    PangoGlyphItem *run = line->runs->data;
    size_t visible = 0;
    for (int i = 0; i < run->glyphs->num_glyphs; i++) {
        CHECK(run->glyphs->log_clusters[i] == 0);
        visible += run->glyphs->glyphs[i].glyph != PANGO_GLYPH_EMPTY;
    }
    CHECK(visible == 2);
    g_object_unref(layout);
}
static void emoji_limits(const rmr_linux_fonts *fonts) {
    const char *sample = "😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀😀";
    rm_paragraph p = {.text = {sample, strlen(sample)}};
    rm_text_block block = {
        .paragraphs = &p, .paragraph_count = 1, .has_column_width = true, .column_width = 1000};
    rmr_linux_layout *layout = nullptr;
    OK(rmr_linux_layout_create(&block, 1000, fonts, nullptr, &layout, nullptr));
    rmr_limits limits = rmr_limits_default();
    limits.maximum_plan_bytes = 1024;
    rmr_plan *out = nullptr;
    CHECK(rmr_linux_layout_plan(layout, (rmr_rect){0, 0, 1000, 100}, &limits, &out, nullptr) ==
          RMR_ERR_LIMIT);
    CHECK(!out);
    limits = rmr_limits_default();
    limits.maximum_pdf_bytes = 512;
    CHECK(rmr_linux_layout_plan(layout, (rmr_rect){0, 0, 1000, 100}, &limits, &out, nullptr) ==
          RMR_ERR_LIMIT);
    CHECK(!out);
    rmr_linux_layout_free(layout);
}
static void pdf_roundtrip(const rmr_linux_fonts *fonts, const char *preview) {
    const char *mixed[] = {"👍👍🏽👍🏿👍🏽", "👍🏽👍👍🏿👍", "🇫🇷🇩🇪🇫🇷", "A👍🏽B🇫🇷C", "A👩‍😀B", "x😀y❤️z", "A👍🏽B\nC🇫🇷D"};
    size_t singles = sizeof samples / sizeof *samples;
    size_t cases = singles + sizeof mixed / sizeof *mixed;
    cairo_surface_t *sheet =
        cairo_image_surface_create(CAIRO_FORMAT_RGB24, 640, (int)((cases + 3) / 4) * 100);
    cairo_t *cr = cairo_create(sheet);
    cairo_set_source_rgb(cr, 1, 1, 1);
    cairo_paint(cr);
    for (size_t i = 0; i < cases; i++) {
        const char *sample = i < singles ? samples[i] : mixed[i - singles];
        rm_paragraph p = {.text = {sample, strlen(sample)}};
        double page_width = i < singles ? 160 : 320;
        rm_text_block block = {.paragraphs = &p,
                               .paragraph_count = 1,
                               .has_column_width = true,
                               .column_width = page_width};
        rmr_linux_layout *layout = nullptr;
        OK(rmr_linux_layout_create(&block, page_width, fonts, nullptr, &layout, nullptr));
        rmr_plan *plan = nullptr;
        if (i == 0) {
            rmr_limits small = rmr_limits_default();
            small.maximum_plan_bytes = 1;
            CHECK(rmr_linux_layout_plan(layout, (rmr_rect){0, 0, page_width, 100}, &small, &plan,
                                        nullptr) == RMR_ERR_LIMIT);
            CHECK(!plan);
        }
        OK(rmr_linux_layout_plan(layout, (rmr_rect){0, 0, page_width, 100}, nullptr, &plan,
                                 nullptr));
        rmr_linux_layout_free(layout);
        rmr_buffer pdf = {0};
        const rmr_plan *plans[] = {plan};
        OK(rmr_linux_pdf(plans, 1, 1, nullptr, nullptr, nullptr, &pdf, nullptr));
        rmr_plan_free(plan);
        if (preview) {
            char path[512];
            int n = snprintf(path, sizeof path, "%s-%zu.pdf", preview, i);
            CHECK(n > 0 && (size_t)n < sizeof path);
            FILE *file = fopen(path, "wb");
            CHECK(file);
            CHECK(fwrite(pdf.bytes, 1, pdf.size, file) == pdf.size);
            CHECK(fclose(file) == 0);
            n = snprintf(path, sizeof path, "%s-%zu.txt", preview, i);
            CHECK(n > 0 && (size_t)n < sizeof path);
            file = fopen(path, "wb");
            CHECK(file);
            CHECK(fwrite(sample, 1, strlen(sample), file) == strlen(sample));
            CHECK(fclose(file) == 0);
        }
        FPDF_DOCUMENT doc = FPDF_LoadMemDocument64(pdf.bytes, pdf.size, nullptr);
        CHECK(doc);
        FPDF_PAGE page = FPDF_LoadPage(doc, 0);
        CHECK(page);
        FPDF_TEXTPAGE text = FPDFText_LoadPage(page);
        CHECK(text);
        unsigned short extracted[64] = {0};
        CHECK(FPDFText_CountChars(text) < 64);
        int count = FPDFText_GetText(text, 0, FPDFText_CountChars(text), extracted);
        glong units = 0;
        char expected_text[256];
        size_t at = 0;
        for (const char *c = sample; *c; c++) {
            if (*c == '\n')
                expected_text[at++] = '\r';
            expected_text[at++] = *c;
        }
        expected_text[at] = 0;
        gunichar2 *expected = g_utf8_to_utf16(expected_text, -1, nullptr, &units, nullptr);
        CHECK(expected);
        if (count != units + 1)
            fprintf(stderr, "emoji PDF case %zu: extracted %d units, expected %ld\n", i, count,
                    units + 1);
        CHECK(count == units + 1);
        for (int c = 0; c < count; c++)
            CHECK(GUINT16_FROM_LE(extracted[c]) == expected[c]);
        double origin_x, origin_y;
        CHECK(FPDFText_GetCharOrigin(text, 0, &origin_x, &origin_y));
        CHECK(fabs(origin_x - 4) < .02 && fabs(origin_y - 66.4) < .02);
        g_free(expected);
        FPDF_BITMAP bitmap = FPDFBitmap_Create(160, 100, 0);
        CHECK(bitmap);
        FPDFBitmap_FillRect(bitmap, 0, 0, 160, 100, 0xffffffff);
        FPDF_RenderPageBitmap(bitmap, page, 0, 0, 160, 100, 0, 0);
        unsigned char *bytes = FPDFBitmap_GetBuffer(bitmap);
        int stride = FPDFBitmap_GetStride(bitmap);
        size_t ink = 0;
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 160; x++)
                ink += bytes[y * stride + x * 4] < 240;
        CHECK(ink > 30);
        cairo_surface_t *image =
            cairo_image_surface_create_for_data(bytes, CAIRO_FORMAT_RGB24, 160, 100, stride);
        cairo_set_source_surface(cr, image, (double)(i % 4) * 160, (double)(i / 4) * 100);
        cairo_paint(cr);
        cairo_surface_destroy(image);
        FPDFBitmap_Destroy(bitmap);
        FPDFText_ClosePage(text);
        FPDF_ClosePage(page);
        FPDF_CloseDocument(doc);
        rmr_buffer_free(&pdf);
    }
    if (preview)
        CHECK(cairo_surface_write_to_png(sheet, preview) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(sheet);
}
typedef struct {
    const char *directory;
    unsigned mode;
    rmr_buffer bytes;
} resolver;
static rmr_status resolve(void *context, const char *name, rmr_asset_view *out) {
    resolver *r = context;
    rmr_buffer_free(&r->bytes);
    if (!strcmp(name, "NotoEmoji-Variable")) {
        if (r->mode == 1)
            return RMR_ERR_UNSUPPORTED;
        if (r->mode == 2) {
            *out = (rmr_asset_view){0};
            return RMR_OK;
        }
        if (r->mode == 3) {
            *out = (rmr_asset_view){(const uint8_t *)"bad", 3};
            return RMR_OK;
        }
        if (r->mode == 4)
            return RMR_ERR_BACKEND;
        if (r->mode == 5) {
            *out = (rmr_asset_view){(const uint8_t *)"bad", SIZE_MAX};
            return RMR_OK;
        }
    }
    rmr_status s = rmr_read_resource(r->directory, name, strlen(name), ".ttf", SIZE_MAX, &r->bytes);
    if (s == RMR_OK)
        *out = (rmr_asset_view){r->bytes.bytes, r->bytes.size};
    return s;
}
int main(int argc, char **argv) {
    CHECK(argc == 2 || argc == 3);
    rmr_linux_fonts *fonts = nullptr;
    OK(rmr_linux_fonts_create_directory(argv[1], nullptr, &fonts, nullptr));
    CHECK(fonts->has_emoji);
    shaping(fonts);
    composite_cluster(fonts);
    emoji_limits(fonts);
    pdf_roundtrip(fonts, argc == 3 ? argv[2] : nullptr);
    rmr_linux_fonts_free(fonts);
    for (unsigned mode = 1; mode <= 5; mode++) {
        resolver r = {.directory = argv[1], .mode = mode};
        fonts = nullptr;
        rmr_status s = rmr_linux_fonts_create(resolve, &r, nullptr, &fonts, nullptr);
        rmr_buffer_free(&r.bytes);
        if (mode <= 2) {
            CHECK(s == RMR_OK && fonts && !fonts->has_emoji);
            double width;
            OK(rmr_linux_text_width((rm_str){"Hello", 5}, 32, RMR_FACE_SANS, false, false, false,
                                    fonts, nullptr, &width, nullptr));
            CHECK(width > 0 && isfinite(width));
            rmr_linux_fonts_free(fonts);
        } else {
            CHECK(s == (mode == 5 ? RMR_ERR_LIMIT : RMR_ERR_BACKEND));
            CHECK(!fonts);
        }
    }
    FcFini();
    return 0;
}
