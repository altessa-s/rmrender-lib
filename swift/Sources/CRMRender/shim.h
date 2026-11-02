/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

/* C23 libraries expose C-compatible layouts to the Swift Clang importer. */
#include <stdbool.h>
#include <assert.h>
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ < 202311L
#define constexpr static const
#endif
#include <rmrender/apple_document.h>
#include <rmrender/apple_context.h>
#include <rmrender/apple_marks.h>
#include <rmlines/rmlines.h>
#include <rmlines/parser.h>
#include <rmlines/editor.h>
#include <rmlines/writer.h>
#include <rmlines/log.h>
#include <rmmarks/rmmarks.h>
#ifdef constexpr
#undef constexpr
#endif
