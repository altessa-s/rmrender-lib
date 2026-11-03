/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include "text_metrics_cases.h"
#include "text_metrics_reference.h"
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
#include "../backends/apple/rmr_typography_internal.h"
#define Fonts rmr_apple_fonts
#define create_fonts rmr_apple_fonts_create_directory
#define free_fonts rmr_apple_fonts_free
#else
#include "../backends/linux/rmr_typography_internal.h"
#define Fonts rmr_linux_fonts
#define create_fonts rmr_linux_fonts_create_directory
#define free_fonts rmr_linux_fonts_free
#endif
typedef struct {
    unsigned index, glyph;
    double x, y;
} glyph_origin;

typedef struct {
    double width;
    size_t count;
    glyph_origin glyphs[64];
} glyph_case_reference;

static const struct {
    const char *text;
    int script;
    int metric_sample;
} glyph_cases[] = {
    {"مرحبا بالعالم", 3, 3}, {"नमस्ते दुनिया", 2, 6}, {"के कि कु कं कँ कः क्र क्त क़", 2, -1},
    {"कुं कूँ कें कैं", 2, -1},      {"กิ กุ ก่ ก้", 6, -1},   {"שָׁלוֹם", 1, -1},
    {"مَرْحَبًا", 3, -1},
};

#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
#include "text_glyph_reference.h"
#endif
static size_t capture(Fonts *fonts, size_t sample_index, double size, bool bold, bool italic,
                      glyph_origin out[64], double *width) {
    const char *sample = glyph_cases[sample_index].text;
    size_t count = 0;
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
    CTFontRef font =
        rmr_ct_font(fonts, size, RMR_FACE_SANS, bold, italic, glyph_cases[sample_index].script);
    CHECK(font);
    CFStringRef string = CFStringCreateWithCString(nullptr, sample, kCFStringEncodingUTF8);
    CHECK(string);
    CFIndex length = CFStringGetLength(string);
    CFMutableAttributedStringRef a = CFAttributedStringCreateMutable(nullptr, 0);
    CHECK(a);
    CFAttributedStringReplaceString(a, CFRangeMake(0, 0), string);
    CFAttributedStringSetAttribute(a, CFRangeMake(0, length), kCTFontAttributeName, font);
    for (CFIndex i = 0; i < length; i++) {
        double kern = rmr_ct_kern(CFStringGetCharacterAtIndex(string, i), RMR_FACE_SANS, bold,
                                  italic, glyph_cases[sample_index].script) *
                      size;
        if (kern != 0) {
            CFNumberRef value = CFNumberCreate(nullptr, kCFNumberDoubleType, &kern);
            CHECK(value);
            CFAttributedStringSetAttribute(a, CFRangeMake(i, 1), kCTKernAttributeName, value);
            CFRelease(value);
        }
    }
    CTLineRef line = CTLineCreateWithAttributedString(a);
    CHECK(line);
    *width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CFArrayRef runs = CTLineGetGlyphRuns(line);
    for (CFIndex r = 0; r < CFArrayGetCount(runs); r++) {
        CTRunRef run = CFArrayGetValueAtIndex(runs, r);
        CGAffineTransform matrix = CTRunGetTextMatrix(run);
        for (CFIndex i = 0; i < CTRunGetGlyphCount(run); i++) {
            CGGlyph glyph;
            CFIndex index;
            CGPoint p;
            CTRunGetGlyphs(run, CFRangeMake(i, 1), &glyph);
            CTRunGetStringIndices(run, CFRangeMake(i, 1), &index);
            CTRunGetPositions(run, CFRangeMake(i, 1), &p);
            p = CGPointApplyAffineTransform(p, matrix);
            CHECK(count < 64);
            out[count++] = (glyph_origin){(unsigned)index, glyph, p.x, p.y};
        }
    }
    CFRelease(line);
    CFRelease(a);
    CFRelease(string);
    CFRelease(font);
#else
    PangoLayout *layout = rmr_pango_layout(fonts, (rm_str){sample, strlen(sample)}, size,
                                           RMR_FACE_SANS, bold, italic);
    CHECK(layout);
    rmr_limits limits = rmr_limits_default();
    OK(rmr_pango_calibrate(layout, &limits));
    /* Repeated calibration must not shift marks or apply HVAR twice. */
    OK(rmr_pango_calibrate(layout, &limits));
    PangoRectangle logical;
    pango_layout_get_extents(layout, nullptr, &logical);
    *width = (double)logical.width / PANGO_SCALE;
    double x = 0;
    PangoLayoutLine *line = pango_layout_get_line_readonly(layout, 0);
    CHECK(line && pango_layout_get_line_count(layout) == 1);
    for (GSList *r = line->runs; r; r = r->next) {
        PangoGlyphItem *run = r->data;
        for (int i = 0; i < run->glyphs->num_glyphs; i++) {
            PangoGlyphInfo g = run->glyphs->glyphs[i];
            int offset = run->item->offset + run->glyphs->log_clusters[i];
            unsigned index = (unsigned)g_utf8_pointer_to_offset(sample, sample + offset);
            CHECK(count < 64);
            out[count++] =
                (glyph_origin){index, g.glyph, x + (double)g.geometry.x_offset / PANGO_SCALE,
                               -(double)g.geometry.y_offset / PANGO_SCALE};
            x += (double)g.geometry.width / PANGO_SCALE;
        }
    }
    g_object_unref(layout);
#endif
    return count;
}

int main(int argc, char **argv) {
    CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "--dump") == 0));
    bool dump = argc == 3;
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
    dump = true;
#else
    CHECK(sizeof apple_glyph_cases / sizeof *apple_glyph_cases ==
          (sizeof glyph_cases / sizeof *glyph_cases) * 16);
#endif
    if (dump) {
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
        printf("/* CoreText reference initializer: sample, size, bold, italic order. */\n");
#else
        printf("/* Linux diagnostic values; not a reference initializer. */\n");
#endif
    }
    for (size_t sample = 0; sample < sizeof glyph_cases / sizeof *glyph_cases; sample++)
        for (size_t size = 0; size < 4; size++)
            for (unsigned bold = 0; bold < 2; bold++)
                for (unsigned italic = 0; italic < 2; italic++) {
                    /* Fresh font maps exercise named instances on their first use. */
                    Fonts *fonts = nullptr;
                    OK(create_fonts(argv[1], nullptr, &fonts, nullptr));
                    glyph_origin glyphs[64];
                    double width;
                    size_t count = capture(fonts, sample, metric_sizes[size], bold != 0,
                                           italic != 0, glyphs, &width);
                    CHECK(count > 0 && count <= 64);
                    if (glyph_cases[sample].metric_sample >= 0) {
                        size_t metric = size * 108 +
                                        (size_t)glyph_cases[sample].metric_sample * 12 + bold * 2 +
                                        italic;
                        CHECK(fabs(width - apple_metric_widths[metric]) < .015);
                    }
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
                    size_t at = ((sample * 4 + size) * 2 + bold) * 2 + italic;
                    const glyph_case_reference *reference = &apple_glyph_cases[at];
                    CHECK(count == reference->count);
                    if (fabs(width - reference->width) >= .02)
                        fprintf(stderr, "case %zu width: Apple %.9f Linux %.9f\n", at,
                                reference->width, width);
                    CHECK(fabs(width - reference->width) < .02);
                    double tolerance = sample == 0 ? .01 : .015;
#endif
                    if (dump)
                        printf("{%.9f, %zu, {\n", width, count);
                    for (size_t i = 0; i < count; i++) {
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
                        glyph_origin expected = reference->glyphs[i];
                        CHECK(glyphs[i].glyph == expected.glyph);
                        /* Shapers can assign different character indices inside a cluster;
                         * this test compares visual glyph origins, not caret mappings. */
                        if (sample == 0)
                            CHECK(glyphs[i].index == expected.index);
                        if (fabs(glyphs[i].x - expected.x) >= tolerance ||
                            fabs(glyphs[i].y - expected.y) >= tolerance)
                            fprintf(stderr, "case %zu glyph %zu: Apple %.9f %.9f Linux %.9f %.9f\n",
                                    at, i, expected.x, expected.y, glyphs[i].x, glyphs[i].y);
                        CHECK(fabs(glyphs[i].x - expected.x) < tolerance);
                        CHECK(fabs(glyphs[i].y - expected.y) < tolerance);
#endif
                        if (dump)
                            printf("{%u, %u, %.9f, %.9f},\n", glyphs[i].index, glyphs[i].glyph,
                                   glyphs[i].x, glyphs[i].y);
                    }
                    if (dump)
                        printf("}},\n");
                    free_fonts(fonts);
                }
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
    FcFini();
#endif
    return 0;
}
