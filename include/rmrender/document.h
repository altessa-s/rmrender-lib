/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifndef RMR_DOCUMENT_H
#define RMR_DOCUMENT_H
#include "rmrender.h"

typedef struct {
  const char *id, *template_name;
  size_t source_page;
  bool has_source, refused_source;
} rmr_document_page;

typedef struct rmr_document_layout rmr_document_layout;

/* Decode .content, owning strings. CRDT idx ordering is stable, first living
 * duplicate wins, tombstones remain available for archive recovery. Flat pages
 * and redirectionPageMap are supported. Malformed redir is local to its page.
 * Strict JSON rejects duplicate keys and embedded NULs. */
[[nodiscard]] RMR_API rmr_status rmr_document_layout_decode(
    const char* json, size_t length, const rmr_limits* limits,
    rmr_document_layout** out, rmr_error* error);
RMR_API size_t rmr_document_layout_bytes(const rmr_document_layout* layout);
RMR_API void rmr_document_layout_free(rmr_document_layout* layout);
RMR_API size_t rmr_document_layout_count(const rmr_document_layout* layout);

RMR_API const rmr_document_page* rmr_document_layout_page(
    const rmr_document_layout* layout, size_t index);
RMR_API size_t
rmr_document_layout_deleted_count(const rmr_document_layout* layout);
RMR_API const char* rmr_document_layout_deleted(
    const rmr_document_layout* layout, size_t index);
RMR_API rmr_point rmr_document_layout_canvas(const rmr_document_layout* layout);
RMR_API bool rmr_document_layout_landscape(const rmr_document_layout* layout);
RMR_API rmr_document_page rmr_document_page_checked(rmr_document_page page,
                                                    size_t source_count);

#endif
