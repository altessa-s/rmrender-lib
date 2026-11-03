/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#include "test.h"
#include <stdio.h>
#include <string.h>
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
#include <rmrender/apple_text.h>
#define F rmr_apple_fonts
#define create rmr_apple_fonts_create_directory
#define release rmr_apple_fonts_free
#define L rmr_apple_layout
#define layout_create rmr_apple_layout_create
#define anchors rmr_apple_layout_anchors
#define line_get rmr_apple_layout_line
#define layout_free rmr_apple_layout_free
#else
#include <fontconfig/fontconfig.h>
#include <rmrender/linux_text.h>
#define F rmr_linux_fonts
#define create rmr_linux_fonts_create_directory
#define release rmr_linux_fonts_free
#define L rmr_linux_layout
#define layout_create rmr_linux_layout_create
#define anchors rmr_linux_layout_anchors
#define line_get rmr_linux_layout_line
#define layout_free rmr_linux_layout_free
#endif
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
#include <CoreFoundation/CoreFoundation.h>
#endif
static const char *samples[] = {"office ffi affine",
                                "é ä ô é ä",
                                "مرحبا بالعالم مرحبا بالعالم",
                                "مَرْحَبًا مَرْحَبًا",
                                "नमस्ते दुनिया नमस्ते दुनिया",
                                "के कि कु कं कँ",
                                "שָׁלוֹם שָׁלוֹם",
                                "abc שלום 123 world",
                                "שלום abc 123 עולם",
                                "abc العربية xyz",
                                "123 שלום abc",
                                "one   two\nthree"};
static const double columns[] = {40, 100, 200, 500};

typedef struct {
    size_t case_id, line, index;
    double at_index, at_start;
} caret_reference;

#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
#include "text_caret_reference.h"
#endif
int main(int argc, char **argv) {
    CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "--dump") == 0));
    bool dump = argc == 3;
    size_t mismatches = 0;
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
    size_t at = 0;
#else
    dump = true;
#endif
    F *fonts = nullptr;
    OK(create(argv[1], nullptr, &fonts, nullptr));
    for (size_t sample = 0; sample < sizeof samples / sizeof *samples; sample++)
        for (size_t column = 0; column < 4; column++)
            for (size_t style = 0; style < 2; style++) {
                size_t case_id = (sample * 4 + column) * 2 + style;
                size_t scalars = 0;
                for (const unsigned char *c = (const unsigned char *)samples[sample]; *c; c++)
                    scalars += (*c & 0xc0) != 0x80;
                CHECK(scalars <= INT32_MAX);
                rm_face_run face = {
                    .start = 0, .end = (int32_t)scalars, .bold = style != 0, .italic = style != 0};
                rm_paragraph p = {.text = {samples[sample], strlen(samples[sample])},
                                  .faces = &face,
                                  .face_count = 1};
                rm_text_block block = {.paragraphs = &p,
                                       .paragraph_count = 1,
                                       .has_column_width = true,
                                       .column_width = columns[column]};
                L *layout = nullptr;
                OK(layout_create(&block, columns[column], fonts, nullptr, &layout, nullptr));
                const rmr_anchor_layout *a = anchors(layout);
#if defined(__APPLE__) && !defined(RMR_TEST_CAIRO)
                CFStringRef text =
                    CFStringCreateWithCString(nullptr, samples[sample], kCFStringEncodingUTF8);
                CHECK(text);
                for (size_t line = 0; line < a->paragraphs[0].line_count; line++) {
                    rmr_text_line visual;
                    OK(line_get(layout, 0, line, &visual, nullptr));
                    for (size_t index = visual.from; index <= visual.to; index++) {
                        if (index != visual.from && index != visual.to &&
                            CFStringGetRangeOfComposedCharactersAtIndex(text, (CFIndex)index)
                                    .location != (CFIndex)index)
                            continue;
                        rmr_caret_offsets c;
                        OK(a->caret->measure(a->caret->context, 0, line, index, &c));
                        if (dump)
                            printf("{%zu, %zu, %zu, %.9f, %.9f},\n", case_id, line, index,
                                   c.at_index, c.at_start);
                    }
                }
                CFRelease(text);
#else
                CHECK(at < sizeof apple_carets / sizeof *apple_carets &&
                      apple_carets[at].case_id == case_id);
                while (at < sizeof apple_carets / sizeof *apple_carets &&
                       apple_carets[at].case_id == case_id) {
                    caret_reference expected = apple_carets[at++];
                    rmr_caret_offsets c = {0};
                    rmr_status status =
                        a->caret->measure(a->caret->context, 0, expected.line, expected.index, &c);
                    if (status != RMR_OK || fabs(c.at_index - expected.at_index) >= .02 ||
                        fabs(c.at_start - expected.at_start) >= .02) {
                        if (mismatches < 12)
                            fprintf(stderr,
                                    "caret %zu line %zu index %zu: Apple %.9f %.9f Linux %.9f %.9f "
                                    "status %d\n",
                                    case_id, expected.line, expected.index, expected.at_index,
                                    expected.at_start, c.at_index, c.at_start, status);
                        mismatches++;
                    }
                    if (dump)
                        printf("{%zu, %zu, %zu, %.9f, %.9f},\n", case_id, expected.line,
                               expected.index, c.at_index, c.at_start);
                }
#endif
                layout_free(layout);
            }
    release(fonts);
#if !defined(__APPLE__) || defined(RMR_TEST_CAIRO)
    CHECK(at == sizeof apple_carets / sizeof *apple_carets);
    FcFini();
#endif
    if (mismatches)
        fprintf(stderr, "%zu caret mismatches\n", mismatches);
    return mismatches ? 1 : 0;
}
