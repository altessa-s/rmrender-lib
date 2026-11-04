/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* Minimal well-formedness checks for the SVG writer's XML subset, plus an
 * optional xmllint pass when the tool is installed. */
#ifndef SVG_CHECK_H
#define SVG_CHECK_H
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <errno.h>
#include <limits.h>
#include <process.h>
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
/* The environment xmllint is started with; not declared by every libc. */
extern char** environ;
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#ifdef _WIN32
/* Converts UTF-8 text to UTF-16 in out, which holds capacity characters. */
static bool svg_wide(const char* text, wchar_t* out, size_t capacity) {
  return capacity <= INT_MAX &&
         MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out,
                             (int)capacity) > 0;
}
#endif

/* Removes the file at the UTF-8 path. */
static void svg_remove(const char* path) {
#ifdef _WIN32
  wchar_t wide[MAX_PATH];
  if (svg_wide(path, wide, MAX_PATH)) _wremove(wide);
#else
  remove(path);
#endif
}

/* Writes bytes to a fresh temporary file named with suffix (Windows keeps its
 * .tmp name) and stores its UTF-8 path; the caller removes it. */
static bool svg_temporary(const uint8_t* bytes, size_t size, char* path,
                          size_t capacity, const char* suffix) {
#ifdef _WIN32
  /* Wide APIs, so a non-ASCII %TEMP% works. */
  (void)suffix;
  wchar_t directory[MAX_PATH], temporary[MAX_PATH];
  DWORD n = GetTempPathW(MAX_PATH, directory);
  if (!n || n >= MAX_PATH || !GetTempFileNameW(directory, L"rmr", 0, temporary))
    return false;
  FILE* f = nullptr;
  if (capacity <= INT_MAX &&
      WideCharToMultiByte(CP_UTF8, 0, temporary, -1, path, (int)capacity,
                          nullptr, nullptr))
    f = _wfopen(temporary, L"wb");
  if (!f) {
    DeleteFileW(temporary);
    return false;
  }
#else
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
#endif
  bool ok = fwrite(bytes, 1, size, f) == size;
  return fclose(f) == 0 && ok;
}

/* Runs xmllint --noout over the document when available; true otherwise.
 * POSIX passes the path to the tool as is; the Windows CRT joins arguments
 * into one command line, so the path is quoted there. */
static bool svg_xmllint(const uint8_t* bytes, size_t size) {
  char path[512];
  if (!svg_temporary(bytes, size, path, sizeof path, ".svg")) return false;

#ifdef _WIN32
  char quoted[sizeof path + 2];
  snprintf(quoted, sizeof quoted, "\"%s\"", path);
  wchar_t wide[sizeof quoted];
  bool ok = false;
  if (svg_wide(quoted, wide, sizeof quoted)) {
    const wchar_t* argv[] = {L"xmllint", L"--noout", wide, nullptr};
    intptr_t status = _wspawnvp(_P_WAIT, L"xmllint", argv);
    ok = status == 0 || (status == -1 && errno == ENOENT);
  }
#else
  char* argv[] = {"xmllint", "--noout", path, nullptr};
  pid_t child;
  int spawned =
      posix_spawnp(&child, "xmllint", nullptr, nullptr, argv, environ);
  int status = 0;
  bool ok = true;
  if (spawned == 0)
    ok = waitpid(child, &status, 0) == child && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0;
#endif
  svg_remove(path);
  return ok;
}

#endif
