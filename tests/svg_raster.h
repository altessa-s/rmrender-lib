/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Rasterizes SVG with an installed command-line renderer (rsvg-convert or
 * resvg) and compares the result with a native bitmap. */
#ifndef SVG_RASTER_H
#define SVG_RASTER_H
#include <math.h>
#include <rmrender/output.h>
#include <zlib.h>

#include "svg_check.h"

/* Name of the available SVG rasterizer, or nullptr when none is installed
 * or, on Windows, the temporary directory has no ASCII spelling for it. */
static const char* svg_rasterizer(void) {
#ifdef _WIN32
  const char* tool = nullptr;
  if (system("where rsvg-convert >NUL 2>&1") == 0)
    tool = "rsvg-convert";
  else if (system("where resvg >NUL 2>&1") == 0)
    tool = "resvg";
  char path[512], alias[512];
  if (tool && svg_temporary((const uint8_t*)"", 0, path, sizeof path, "")) {
    bool ascii = svg_tool_path(path, alias, sizeof alias);
    svg_remove(path);
    if (ascii) return tool;
    printf("note: %s skipped; the temporary directory has no ASCII path\n",
           tool);
  }
  return nullptr;
#else
  if (system("command -v rsvg-convert >/dev/null 2>&1") == 0)
    return "rsvg-convert";
  if (system("command -v resvg >/dev/null 2>&1") == 0) return "resvg";
#endif
  return nullptr;
}

/* Reads the whole file at the UTF-8 path into a malloc'd buffer. */
static uint8_t* svg_read(const char* path, size_t* size) {
#ifdef _WIN32
  wchar_t wide[MAX_PATH];
  FILE* f = svg_wide(path, wide, MAX_PATH) ? _wfopen(wide, L"rb") : nullptr;
#else
  FILE* f = fopen(path, "rb");
#endif
  if (!f) return nullptr;
  size_t capacity = 1 << 16, used = 0;
  uint8_t* bytes = malloc(capacity);
  for (size_t n; bytes && (n = fread(bytes + used, 1, capacity - used, f));) {
    used += n;
    if (used == capacity) {
      uint8_t* grown = realloc(bytes, capacity *= 2);
      if (!grown) free(bytes);
      bytes = grown;
    }
  }
  fclose(f);
  *size = used;
  return bytes;
}

/* Big-endian 32-bit value. */
static uint32_t svg_u32(const uint8_t* p) {
  return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 |
         p[3];
}

/* Paeth predictor from the PNG specification. */
static uint8_t svg_paeth(int a, int b, int c) {
  int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
  return (uint8_t)(pa <= pb && pa <= pc ? a : pb <= pc ? b : c);
}

/* Decodes a non-interlaced 8-bit RGB/RGBA PNG to straight RGBA8. */
static bool svg_png_decode(const uint8_t* png, size_t size, rmr_bitmap* out) {
  if (size < 33 || memcmp(png, "\x89PNG\r\n\x1a\n", 8)) return false;
  size_t width = svg_u32(png + 16), height = svg_u32(png + 20);
  uint8_t depth = png[24], type = png[25];
  if (depth != 8 || (type != 6 && type != 2) || png[28]) return false;
  size_t channels = type == 6 ? 4 : 3, row = width * channels;

  uint8_t* packed = malloc(size);
  size_t packed_size = 0;
  for (size_t at = 8; at + 12 <= size;) {
    size_t length = svg_u32(png + at);
    if (!memcmp(png + at + 4, "IDAT", 4)) {
      memcpy(packed + packed_size, png + at + 8, length);
      packed_size += length;
    }
    at += length + 12;
  }

  uLongf raw_size = (uLongf)((row + 1) * height);
  uint8_t* raw = malloc(raw_size);
  bool ok = uncompress(raw, &raw_size, packed, (uLong)packed_size) == Z_OK &&
            raw_size == (row + 1) * height;
  free(packed);

  uint8_t* pixels = calloc(width * height, 4);
  for (size_t y = 0; ok && y < height; y++) {
    uint8_t* line = raw + y * (row + 1) + 1;
    const uint8_t* above = y ? raw + (y - 1) * (row + 1) + 1 : nullptr;
    for (size_t x = 0; x < row; x++) {
      int a = x >= channels ? line[x - channels] : 0, b = above ? above[x] : 0,
          c = above && x >= channels ? above[x - channels] : 0;
      switch (line[-1]) {
        case 1:
          line[x] = (uint8_t)(line[x] + a);
          break;
        case 2:
          line[x] = (uint8_t)(line[x] + b);
          break;
        case 3:
          line[x] = (uint8_t)(line[x] + (a + b) / 2);
          break;
        case 4:
          line[x] = (uint8_t)(line[x] + svg_paeth(a, b, c));
          break;
      }
    }
    for (size_t x = 0; x < width; x++) {
      memcpy(pixels + (y * width + x) * 4, line + x * channels, channels);
      if (channels == 3) pixels[(y * width + x) * 4 + 3] = 255;
    }
  }

  free(raw);
  if (!ok) {
    free(pixels);
    return false;
  }
  *out = (rmr_bitmap){pixels, width, height, width * 4};
  return true;
}

/* Appends one PNG chunk with its CRC. */
[[maybe_unused]] static uint8_t* svg_png_chunk(uint8_t* at, const char* type,
                                               const uint8_t* data,
                                               size_t size) {
  for (unsigned i = 0; i < 4; i++) at[i] = (uint8_t)(size >> (24 - 8 * i));
  memcpy(at + 4, type, 4);
  if (size) memcpy(at + 8, data, size);
  uLong crc = crc32(0, at + 4, (uInt)size + 4);
  for (unsigned i = 0; i < 4; i++)
    at[8 + size + i] = (uint8_t)(crc >> (24 - 8 * i));
  return at + 12 + size;
}

/* A side x side RGBA PNG: color quadrants, a diagonal ramp and a translucent
 * corner, large enough that resampling differences stay at its edges. */
[[maybe_unused]] static rmr_buffer svg_png_quadrants(size_t side) {
  size_t row = side * 4 + 1, raw_size = row * side;
  uint8_t* raw = calloc(raw_size, 1);
  for (size_t y = 0; y < side; y++)
    for (size_t x = 0; x < side; x++) {
      uint8_t* p = raw + y * row + 1 + x * 4;
      bool right = x >= side / 2, bottom = y >= side / 2;
      p[0] = (uint8_t)(right ? (x + y) * 255 / (2 * side) : bottom ? 30 : 220);
      p[1] = (uint8_t)(right && !bottom ? 180 : 40);
      p[2] = (uint8_t)(bottom && !right ? 230 : 60);
      p[3] = (uint8_t)(right && bottom ? 128 : 255);
    }

  uLongf packed_size = compressBound((uLong)raw_size);
  uint8_t* packed = malloc(packed_size);
  compress(packed, &packed_size, raw, (uLong)raw_size);
  free(raw);

  uint8_t header[13] = {0, 0, 0, 0, 0, 0, 0, 0, 8, 6, 0, 0, 0};
  for (unsigned i = 0; i < 4; i++)
    header[i] = header[4 + i] = (uint8_t)(side >> (24 - 8 * i));
  uint8_t* png = malloc(8 + 25 + 12 + packed_size + 12);
  memcpy(png, "\x89PNG\r\n\x1a\n", 8);
  uint8_t* at = svg_png_chunk(png + 8, "IHDR", header, sizeof header);
  at = svg_png_chunk(at, "IDAT", packed, packed_size);
  at = svg_png_chunk(at, "IEND", nullptr, 0);
  free(packed);
  return (rmr_buffer){png, (size_t)(at - png)};
}

/* Rasterizes svg over white at exactly width x height pixels. */
static bool svg_rasterize(const char* tool, const rmr_buffer* svg, size_t width,
                          size_t height, rmr_bitmap* out) {
  char input[512], output[512], command[1400];
  if (!svg_temporary(svg->bytes, svg->size, input, sizeof input, ".svg"))
    return false;
  if (!svg_temporary((const uint8_t*)"", 0, output, sizeof output, ".png")) {
    svg_remove(input);
    return false;
  }

#ifdef _WIN32
  /* The tools take narrow arguments: pass the ASCII aliases. */
  char input_alias[512], output_alias[512];
  if (!svg_tool_path(input, input_alias, sizeof input_alias) ||
      !svg_tool_path(output, output_alias, sizeof output_alias)) {
    svg_remove(input);
    svg_remove(output);
    return false;
  }
  const char* resvg_format =
      "resvg --background white -w %zu -h %zu \"%s\" \"%s\"";
  const char* rsvg_format =
      "rsvg-convert -b white -w %zu -h %zu -o \"%s\" \"%s\"";
#else
  const char* resvg_format = "resvg --background white -w %zu -h %zu '%s' '%s'";
  const char* rsvg_format = "rsvg-convert -b white -w %zu -h %zu -o '%s' '%s'";
  const char *input_alias = input, *output_alias = output;
#endif
  if (!strcmp(tool, "resvg"))
    snprintf(command, sizeof command, resvg_format, width, height, input_alias,
             output_alias);
  else
    snprintf(command, sizeof command, rsvg_format, width, height, output_alias,
             input_alias);
  bool ok = system(command) == 0;

  size_t size = 0;
  uint8_t* png = ok ? svg_read(output, &size) : nullptr;
  ok = png && svg_png_decode(png, size, out) && out->width == width &&
       out->height == height;
  free(png);
  svg_remove(input);
  svg_remove(output);
  return ok;
}

typedef struct {
  double mean;   /* mean absolute channel difference, 0..255 */
  double strong; /* share of pixels differing by more than 64 in a channel */
} svg_difference;

/* Compares two equally sized opaque RGBA bitmaps. */
static svg_difference svg_compare(const rmr_bitmap* a, const rmr_bitmap* b) {
  double total = 0;
  size_t strong = 0;
  for (size_t y = 0; y < a->height; y++)
    for (size_t x = 0; x < a->width; x++) {
      const uint8_t* p = a->pixels + y * a->stride + x * 4;
      const uint8_t* q = b->pixels + y * b->stride + x * 4;
      int largest = 0;
      for (size_t c = 0; c < 3; c++) {
        int d = abs((int)p[c] - (int)q[c]);
        total += d;
        if (d > largest) largest = d;
      }
      if (largest > 64) strong++;
    }
  double pixels = (double)(a->width * a->height);
  return (svg_difference){total / (pixels * 3), (double)strong / pixels};
}

#endif
