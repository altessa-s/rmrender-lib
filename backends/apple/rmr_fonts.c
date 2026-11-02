/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "rmr_font_calibration.h"
#include "rmr_typography_internal.h"
#include <limits.h>
static const char *const names[] = {"NotoSans-Regular",
                                    "NotoSans-Bold",
                                    "NotoSans-Italic",
                                    "NotoSans-Medium",
                                    "NotoSans-Light",
                                    "EBGaramond-VariableFont_wght",
                                    "EBGaramond-Italic-VariableFont_wght",
                                    "NotoSansHebrew-Variable",
                                    "NotoSansArabic-Variable",
                                    "NotoSansDevanagari-Variable",
                                    "NotoSansKR-Variable",
                                    "NotoSansJP-Variable",
                                    "NotoSansThaiLooped-Variable"};
struct rmr_apple_fonts {
    CTFontDescriptorRef descriptors[13];
};
void rmr_apple_fonts_free(rmr_apple_fonts *f) {
    if (!f)
        return;
    for (size_t i = 0; i < 13; i++)
        if (f->descriptors[i])
            CFRelease(f->descriptors[i]);
    free(f);
}
rmr_status rmr_apple_fonts_create(rmr_font_resolve_fn resolve, void *context,
                                  const rmr_limits *given, rmr_apple_fonts **out, rmr_error *e) {
    if (!resolve || !out)
        return text_error(e, RMR_ERR_INVALID_ARGUMENT, "missing font resolver/output");
    rmr_limits l = given ? *given : rmr_limits_default();
    rmr_apple_fonts *f = calloc(1, sizeof *f);
    if (!f)
        return text_error(e, RMR_ERR_OUT_OF_MEMORY, "font allocation failed");
    size_t total = 0;
    rmr_status s = RMR_OK;
    for (size_t i = 0; i < 13; i++) {
        rmr_asset_view view = {0};
        s = resolve(context, names[i], &view);
        if (s != RMR_OK)
            break;
        if (!view.bytes || !view.size) {
            s = RMR_ERR_UNSUPPORTED;
            break;
        }
        if (view.size > l.maximum_plan_bytes - total || view.size > LONG_MAX) {
            s = RMR_ERR_LIMIT;
            break;
        }
        total += view.size;
        CFDataRef data = CFDataCreate(kCFAllocatorDefault, view.bytes, (CFIndex)view.size);
        if (!data) {
            s = RMR_ERR_OUT_OF_MEMORY;
            break;
        }
        CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromData(data);
        CFRelease(data);
        if (!descriptors || CFArrayGetCount(descriptors) == 0) {
            if (descriptors)
                CFRelease(descriptors);
            s = RMR_ERR_BACKEND;
            break;
        }
        f->descriptors[i] = (CTFontDescriptorRef)CFRetain(CFArrayGetValueAtIndex(descriptors, 0));
        CFRelease(descriptors);
    }
    if (s != RMR_OK) {
        rmr_apple_fonts_free(f);
        return text_error(e, s, "font resources could not be loaded");
    }
    *out = f;
    return RMR_OK;
}
static CTFontDescriptorRef weighted(CTFontDescriptorRef d, double weight) {
    int axis = 0x77676874;
    CFNumberRef key = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &axis);
    if (!key)
        return nullptr;
    CTFontDescriptorRef result = CTFontDescriptorCreateCopyWithVariation(d, key, weight);
    CFRelease(key);
    return result;
}
CTFontRef rmr_ct_font(const rmr_apple_fonts *fonts, double size, rmr_type_face face, bool bold,
                      bool italic, int script) {
    unsigned file;
    const char *key;
    char composed[96];
    bool nonlatin = script >= 0;
    double weight = 0, slant = 0;
    if (nonlatin) {
        bool light = italic && size >= 64;
        file = bold ? 1 : face == RMR_FACE_MEDIUM ? 3 : face == RMR_FACE_SERIF && light ? 4 : 0;
        static const char *const families[] = {
            "NotoSans",   "NotoSansHebrew", "NotoSansDevanagari", "NotoSansArabic",
            "NotoSansKR", "NotoSansJP",     "NotoSansThaiLooped"};
        snprintf(composed, sizeof composed, "%s-%s", families[script],
                 file == 1   ? "Bold"
                 : file == 3 ? "Medium"
                 : file == 4 ? "Light"
                             : "Regular");
        key = composed;
        slant = italic ? .3 : 0;
    } else if (face == RMR_FACE_SERIF) {
        file = italic && !bold ? 6 : 5;
        weight = bold ? 700 : 400;
        key = bold     ? "reMarkableSerifSmall-Bold"
              : italic ? "reMarkableSerifSmall-RegularItalic"
                       : "reMarkableSerifSmall-Regular";
        slant = bold && italic ? .3 : 0;
    } else {
        file = bold ? 1 : italic ? 2 : face == RMR_FACE_MEDIUM ? 3 : 0;
        key = bold                      ? "reMarkableSans-Bold"
              : italic                  ? "reMarkableSans-RegularItalic"
              : face == RMR_FACE_MEDIUM ? "reMarkableSans-Medium"
                                        : "reMarkableSans-Regular";
        slant = bold && italic ? .3 : 0;
    }
    double width = 1, nearest = INFINITY;
    for (size_t i = 0; i < sizeof stretches / sizeof *stretches; i++) {
        if (strcmp(key, stretches[i].key) != 0)
            continue;
        double distance = fabs(size - stretches[i].size);
        if (distance < nearest) {
            nearest = distance;
            width = stretches[i].width;
        }
    }
    CTFontDescriptorRef descriptor = weight != 0
                                         ? weighted(fonts->descriptors[file], weight)
                                         : (CTFontDescriptorRef)CFRetain(fonts->descriptors[file]);
    if (!descriptor)
        return nullptr;
    if (nonlatin) {
        bool light = (italic && size >= 64) || (script == 2 && size >= 64 && !bold);
        bool weighted_light = light && (script == 1 || script == 2 || script == 3 || script == 6);
        double cw = bold ? 700 : face == RMR_FACE_MEDIUM ? 500 : weighted_light ? 300 : 400;
        /* Fixed cascade order, independent of resource-array order. */
        const unsigned order[] = {8, 7, 9, 10, 11, 12};
        CFMutableArrayRef cascade =
            CFArrayCreateMutable(kCFAllocatorDefault, 6, &kCFTypeArrayCallBacks);
        if (!cascade) {
            CFRelease(descriptor);
            return nullptr;
        }
        for (unsigned i = 0; i < 6; i++) {
            CTFontDescriptorRef d = weighted(fonts->descriptors[order[i]], cw);
            if (!d) {
                CFRelease(cascade);
                CFRelease(descriptor);
                return nullptr;
            }
            CFArrayAppendValue(cascade, d);
            CFRelease(d);
        }
        const void *keys[] = {kCTFontCascadeListAttribute}, *values[] = {cascade};
        CFDictionaryRef attrs =
            CFDictionaryCreate(kCFAllocatorDefault, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                               &kCFTypeDictionaryValueCallBacks);
        CTFontDescriptorRef next =
            attrs ? CTFontDescriptorCreateCopyWithAttributes(descriptor, attrs) : nullptr;
        if (attrs)
            CFRelease(attrs);
        CFRelease(cascade);
        CFRelease(descriptor);
        descriptor = next;
        if (!descriptor)
            return nullptr;
    }
    CGAffineTransform matrix = CGAffineTransformMake(width, 0, slant, 1, 0, 0);
    CTFontRef font = CTFontCreateWithFontDescriptor(descriptor, size, &matrix);
    CFRelease(descriptor);
    return font;
}
double rmr_ct_kern(UniChar c, rmr_type_face face, bool bold, bool italic, int script) {
    if (script >= 0)
        return script == 2 ? 0 : notoGlyphShare(c);
    if (face == RMR_FACE_SERIF)
        return bold     ? serifBoldGlyphShare(c)
               : italic ? serifItalicGlyphShare(c)
                        : serifGlyphShare(c);
    if (face == RMR_FACE_MEDIUM && !bold)
        return italic ? 0 : sansMediumGlyphShare(c);
    return bold ? sansBoldGlyphShare(c) : italic ? sansItalicGlyphShare(c) : sansGlyphShare(c);
}
#include "../../adapters/rmr_files.h"
typedef struct {
    const char *directory;
    rmr_buffer current;
    size_t maximum;
} font_directory;
static rmr_status directory_font(void *context, const char *name, rmr_asset_view *out) {
    font_directory *d = context;
    rmr_buffer_free(&d->current);
    rmr_status status =
        rmr_read_resource(d->directory, name, strlen(name), ".ttf", d->maximum, &d->current);
    if (status == RMR_OK)
        *out = (rmr_asset_view){d->current.bytes, d->current.size};
    return status;
}
rmr_status rmr_apple_fonts_create_directory(const char *directory, const rmr_limits *limits,
                                            rmr_apple_fonts **out, rmr_error *e) {
    if (!directory)
        return text_error(e, RMR_ERR_INVALID_ARGUMENT, "missing font directory");
    font_directory d = {.directory = directory,
                        .maximum = limits ? limits->maximum_plan_bytes
                                          : rmr_limits_default().maximum_plan_bytes};
    rmr_status status = rmr_apple_fonts_create(directory_font, &d, limits, out, e);
    rmr_buffer_free(&d.current);
    return status;
}
