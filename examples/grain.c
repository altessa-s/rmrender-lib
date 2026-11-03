/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Own synthetic strokes at three pressures; writes PPM and vector/mask PDF. */
#include <math.h>
#include <rmrender/apple.h>
#include <rmrender/grain.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(rmr_status status) {
    if (status != RMR_OK) {
        fprintf(stderr, "%s\n", rmr_status_string(status));
        exit(1);
    }
}

static void save(const char *path, const void *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        exit(1);
    }

    if (fwrite(bytes, 1, size, file) != size || fclose(file) != 0) {
        perror(path);
        exit(1);
    }
}

int main(int argc, char **argv) {
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "usage: %s output.ppm output.pdf [streak|scratch|stamp|noise|bristle]\n",
                argv[0]);
        return 2;
    }

    rmr_grain grain = RMR_GRAIN_STREAK;
    if (argc == 4) {
        if (strcmp(argv[3], "scratch") == 0)
            grain = RMR_GRAIN_SCRATCH;
        else if (strcmp(argv[3], "stamp") == 0)
            grain = RMR_GRAIN_STAMP;
        else if (strcmp(argv[3], "noise") == 0)
            grain = RMR_GRAIN_NOISE;
        else if (strcmp(argv[3], "bristle") == 0)
            grain = RMR_GRAIN_BRISTLE;
        else if (strcmp(argv[3], "streak") != 0) {
            fprintf(stderr, "unknown grain\n");
            return 2;
        }
    }

    rmr_unit_raster raster = rmr_apple_unit_raster();
    rmr_draw draws[3] = {0};
    rmr_path paths[3] = {0};
    rmr_mask masks[3] = {0};

    for (size_t row = 0; row < 3; row++) {
        rmr_sample shape[32];
        rmr_grain_point points[32];
        for (size_t i = 0; i < 32; i++) {
            float x = (float)i * 8, y = (float)row * 60 + 15 * sinf((float)i * 0.25f),
                  width = 12 + 6 * sinf((float)i * 0.15f);
            shape[i] = (rmr_sample){{x, y}, width};
            points[i] = (rmr_grain_point){x, y, width, (float)(row + 1) * 85, 40};
        }
        rmr_rect box;
        check(rmr_grain_box(points, 32, nullptr, &box, nullptr));
        check(rmr_outline(shape, 32, RMR_CAP_ROUND, RMR_JOIN_ROUND, nullptr, &paths[row], nullptr));
        check(rmr_grain_mask(points, 32, grain, box, &raster, nullptr, &masks[row], nullptr));
        rmr_mask m = masks[row];
        draws[row] = (rmr_draw){.path = {paths[row].elements, paths[row].count},
                                .color = {0, 0, 0, 1},
                                .mask = {m.bytes, m.width, m.height, m.stride, m.box}};
    }

    rmr_plan *plan = nullptr;
    check(rmr_plan_create(draws, 3, (rmr_rect){-24, -32, 300, 190}, nullptr, &plan, nullptr));

    for (size_t i = 0; i < 3; i++) {
        rmr_path_free(&paths[i]);
        rmr_mask_free(&masks[i]);
    }

    rmr_bitmap bitmap = {0};
    check(rmr_apple_bitmap(plan, 3, (rmr_color){1, 1, 1, 1}, nullptr, nullptr, nullptr, &bitmap,
                           nullptr));
    size_t count = bitmap.width * bitmap.height * 3;
    char header[128];
    int length = snprintf(header, sizeof header, "P6\n%zu %zu\n255\n", bitmap.width, bitmap.height);
    if (length < 0 || (size_t)length >= sizeof header)
        return 1;
    uint8_t *ppm = malloc((size_t)length + count);
    if (!ppm)
        return 1;

    for (int i = 0; i < length; i++)
        ppm[i] = (uint8_t)header[i];
    for (size_t y = 0; y < bitmap.height; y++)
        for (size_t x = 0; x < bitmap.width; x++)
            for (size_t c = 0; c < 3; c++)
                ppm[(size_t)length + (y * bitmap.width + x) * 3 + c] =
                    bitmap.pixels[y * bitmap.stride + x * 4 + c];

    save(argv[1], ppm, (size_t)length + count);
    free(ppm);
    rmr_bitmap_free(&bitmap);

    const rmr_plan *pages[] = {plan};
    rmr_buffer pdf = {0};
    check(rmr_apple_pdf(pages, 1, RMR_POINTS_PER_UNIT, nullptr, nullptr, nullptr, &pdf, nullptr));
    save(argv[2], pdf.bytes, pdf.size);
    rmr_buffer_free(&pdf);

    rmr_plan_free(plan);
    return 0;
}
