/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Minimal well-formedness checks for the SVG writer's XML subset, plus an
 * optional xmllint pass when the tool is installed. */
#ifndef SVG_CHECK_H
#define SVG_CHECK_H
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Whether name is an XML name made of the characters the writer uses. */
static bool svg_name_char(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == ':' || c == '_';
}

/* Parses the document: one XML declaration, then properly nested elements
 * with quoted, entity-safe attributes and whitespace-only text. Every
 * url(#id) and id must be unique/defined. Returns false on the first
 * violation. */
static bool svg_well_formed(const char* x, size_t n) {
  const char* end = x + n;
  const char* decl = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
  if (n < strlen(decl) || memcmp(x, decl, strlen(decl))) return false;
  const char* p = x + strlen(decl);

  const char* stack[256];
  size_t lengths[256], depth = 0;
  char ids[512][16];
  size_t id_count = 0;
  bool root = false;

  while (p < end) {
    if (*p == ' ' || *p == '\n') {
      p++;
      continue;
    }
    if (*p != '<') return false;
    p++;

    if (*p == '/') {
      const char* name = ++p;
      while (p < end && svg_name_char(*p)) p++;
      size_t length = (size_t)(p - name);
      if (!depth || p >= end || *p != '>' || length != lengths[depth - 1] ||
          memcmp(name, stack[depth - 1], length))
        return false;
      depth--;
      p++;
      continue;
    }

    if (!depth && root) return false;
    const char* name = p;
    while (p < end && svg_name_char(*p)) p++;
    size_t length = (size_t)(p - name);
    if (!length) return false;
    if (!depth) root = length == 3 && !memcmp(name, "svg", 3);
    if (!root) return false;

    for (;;) {
      while (p < end && *p == ' ') p++;
      if (p >= end) return false;
      if (*p == '/' || *p == '>') break;

      const char* attribute = p;
      while (p < end && svg_name_char(*p)) p++;
      if (p == attribute || p + 1 >= end || p[0] != '=' || p[1] != '"')
        return false;
      bool is_id = p - attribute == 2 && !memcmp(attribute, "id", 2);
      p += 2;

      const char* value = p;
      while (p < end && *p != '"') {
        if (*p == '<') return false;
        if (*p == '&' && strncmp(p, "&amp;", 5) && strncmp(p, "&lt;", 4) &&
            strncmp(p, "&gt;", 4) && strncmp(p, "&quot;", 6))
          return false;
        if ((unsigned char)*p < 0x20) return false;
        p++;
      }
      if (p >= end) return false;

      if (is_id) {
        size_t size = (size_t)(p - value);
        if (size >= sizeof ids[0] || id_count == 512) return false;
        for (size_t i = 0; i < id_count; i++)
          if (strlen(ids[i]) == size && !memcmp(ids[i], value, size))
            return false;
        memcpy(ids[id_count], value, size);
        ids[id_count++][size] = 0;
      }
      p++;
    }

    if (*p == '/') {
      if (p + 1 >= end || p[1] != '>') return false;
      p += 2;
    } else {
      if (depth == 256) return false;
      stack[depth] = name;
      lengths[depth++] = length;
      p++;
    }
  }

  if (depth || !root) return false;

  for (const char* r = x; r + 5 <= end; r++) {
    if (memcmp(r, "url(#", 5)) continue;
    const char* id = r + 5;
    const char* close = memchr(id, ')', (size_t)(end - id));
    if (!close) return false;
    bool found = false;
    for (size_t i = 0; i < id_count && !found; i++)
      found = strlen(ids[i]) == (size_t)(close - id) &&
              !memcmp(ids[i], id, (size_t)(close - id));
    if (!found) return false;
  }
  return true;
}

/* Writes bytes to a fresh temporary file named with suffix; the caller
 * removes it. */
static bool svg_temporary(const uint8_t* bytes, size_t size, char* path,
                          size_t capacity, const char* suffix) {
  const char* directory = getenv("TMPDIR");
  snprintf(path, capacity, "%s/rmr-svg-XXXXXX%s",
           directory && *directory ? directory : "/tmp", suffix);
  int fd = mkstemps(path, (int)strlen(suffix));
  if (fd < 0) return false;
  FILE* f = fdopen(fd, "wb");
  if (!f) {
    close(fd);
    return false;
  }
  bool ok = fwrite(bytes, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

/* Runs xmllint --noout over the document when available; true otherwise. */
static bool svg_xmllint(const uint8_t* bytes, size_t size) {
  if (system("command -v xmllint >/dev/null 2>&1") != 0) return true;

  char path[512], command[600];
  if (!svg_temporary(bytes, size, path, sizeof path, ".svg")) return false;
  snprintf(command, sizeof command, "xmllint --noout '%s'", path);
  int status = system(command);
  remove(path);
  return status == 0;
}

#endif
