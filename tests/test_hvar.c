/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "../backends/linux/rmr_hvar.h"
#include "test.h"
static void put(uint8_t *b, size_t at, uint32_t value, unsigned n) {
    for (unsigned i = 0; i < n; i++)
        b[at + n - i - 1] = (uint8_t)(value >> (i * 8));
}
static size_t fixture(uint8_t b[128], bool wide, int mapping) {
    memset(b, 0, 128);
    put(b, 0, 0x10000, 4);
    put(b, 4, 20, 4);
    put(b, 20, 1, 2); /* ItemVariationStore */
    put(b, 22, 12, 4);
    put(b, 26, 1, 2);
    put(b, 28, 28, 4);
    put(b, 32, 1, 2); /* One axis, two regions */
    put(b, 34, 2, 2);
    put(b, 38, 16384, 2);
    put(b, 40, 16384, 2);
    put(b, 42, (uint32_t)-16384, 2);
    put(b, 44, (uint32_t)-16384, 2);
    put(b, 48, 2, 2); /* Two rows: signed wide/narrow deltas */
    put(b, 50, wide ? 0x8001 : 1, 2);
    put(b, 52, 2, 2);
    put(b, 56, 1, 2);
    unsigned large = wide ? 4 : 2, small = large / 2;
    size_t at = 58;
    put(b, at, (uint32_t)(wide ? -70001 : -3), large);
    at += large;
    put(b, at, wide ? 3000 : 7, small);
    at += small;
    put(b, at, 5, large);
    at += large;
    put(b, at, (uint32_t)-9, small);
    at += small;
    if (mapping >= 0) {
        put(b, 8, (uint32_t)at, 4);
        b[at] = (uint8_t)mapping;
        put(b, at + 2, 1, mapping ? 4 : 2);
        at += mapping ? 6 : 4;
        b[at++] = 1; /* All glyphs map to outer 0, inner 1 */
    }
    return at;
}
int main(void) {
    uint8_t bytes[128], changed[128];
    for (int wide = 0; wide < 2; wide++)
        for (int mapping = -1; mapping <= 1; mapping++) {
            size_t size = fixture(bytes, wide != 0, mapping);
            rmr_ot_bytes b = {bytes, size};
            int coord = 8192;
            double delta = 123;
            CHECK(rmr_hvar_delta(b, 0, &coord, 1, &delta));
            CHECK(delta == (mapping >= 0 ? 2.5 : wide ? -35000.5 : -1.5));
            coord = -8192;
            CHECK(rmr_hvar_delta(b, 0, &coord, 1, &delta));
            CHECK(delta == (mapping >= 0 ? -4.5 : wide ? 1500 : 3.5));
            coord = 0;
            CHECK(rmr_hvar_delta(b, 0, &coord, 1, &delta) && delta == 0);
            CHECK(!rmr_hvar_delta(b, 0, &coord, 0, &delta));
            coord = 16384;
            CHECK(rmr_hvar_delta(b, 1, &coord, 1, &delta) && delta == 5);
            if (mapping >= 0)
                CHECK(rmr_hvar_delta(b, UINT32_MAX, &coord, 1, &delta) && delta == 5);
            else
                CHECK(!rmr_hvar_delta(b, 2, &coord, 1, &delta));
            for (size_t n = 0; n < size; n++)
                CHECK(!rmr_hvar_delta((rmr_ot_bytes){bytes, n}, 0, &coord, 1, &delta));
            /* Corrupt offsets, counts, region indices and packed map fields.
             * Successful parses must stay finite; rejected parses preserve output. */
            for (size_t i = 0; i < size; i++)
                for (unsigned value = 0; value < 256; value++) {
                    memcpy(changed, bytes, size);
                    changed[i] = (uint8_t)value;
                    delta = 123;
                    if (rmr_hvar_delta((rmr_ot_bytes){changed, size}, 0, &coord, 1, &delta))
                        CHECK(isfinite(delta));
                    else
                        CHECK(delta == 123);
                }
        }
    return 0;
}
