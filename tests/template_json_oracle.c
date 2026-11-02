/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include <rmrender/apple.h>
#include <rmrender/template.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    char line[65536];
    while (fgets(line, sizeof line, stdin)) {
        double cw, ch, ew, eh;
        int offset = 0;
        if (sscanf(line, "%lf %lf %lf %lf %n", &cw, &ch, &ew, &eh, &offset) != 4)
            return 1;
        rmr_template_document *d = nullptr;
        rmr_plan *p = nullptr;
        rmr_error e = {0};
        rmr_status s =
            rmr_template_document_decode(line + offset, strlen(line + offset), nullptr, &d, &e);
        rmr_text_provider provider = rmr_apple_text_provider();
        if (s == RMR_OK)
            s = rmr_template_plan_with_text(rmr_template_document_model(d), (rmr_point){cw, ch},
                                            (rmr_point){ew, eh}, (rmr_rect){-cw / 2, 0, ew, eh},
                                            &provider, nullptr, &p, &e);
        rmr_template_document_free(d);
        rmr_bitmap bitmap = {0};
        if (s == RMR_OK)
            s = rmr_apple_bitmap(p, 1, (rmr_color){0}, nullptr, nullptr, nullptr, &bitmap, &e);
        rmr_plan_free(p);
        if (s != RMR_OK) {
            fprintf(stderr, "%s\n", e.message);
            return 2;
        }
        for (size_t i = 0; i < bitmap.stride * bitmap.height; i++)
            printf("%02x", bitmap.pixels[i]);
        putchar('\n');
        rmr_bitmap_free(&bitmap);
    }
    return 0;
}
