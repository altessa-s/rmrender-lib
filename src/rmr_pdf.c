/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */
#include "rmr_internal.h"
#include <limits.h>
#include <rmrender/pdf.h>
#include <stdarg.h>
#include <stddef.h>
#include <zlib.h>
/* Bounded arena; parsed scalar tokens retain their exact source spelling.
 * References are the only scalar rewritten when importing an overlay. */
typedef struct mem {
    struct mem *next;
} mem;
typedef struct {
    rmr_limits limits;
    rmr_status status;
    const char *reason;
    size_t allocated, work;
    mem *memory;
    rmr_cancel_fn cancel;
    void *context;
} job;
static void stop(job *j, rmr_status s, const char *why) {
    if (j->status == RMR_OK) {
        j->status = s;
        j->reason = why;
    }
}
static bool tick(job *j) {
    if (j->status != RMR_OK)
        return false;
    if (j->cancel && j->cancel(j->context))
        stop(j, RMR_ERR_CANCELLED, "PDF update cancelled");
    if (j->work >= j->limits.maximum_elements)
        stop(j, RMR_ERR_LIMIT, "PDF work limit");
    else
        j->work++;
    return j->status == RMR_OK;
}
static void *take(job *j, size_t n) {
    if (j->status != RMR_OK)
        return nullptr;
    if (n > SIZE_MAX - sizeof(mem) ||
        n + sizeof(mem) > j->limits.maximum_plan_bytes - j->allocated) {
        stop(j, RMR_ERR_LIMIT, "PDF scratch limit");
        return nullptr;
    }
    mem *m = rmr_calloc(1, sizeof *m + n);
    if (!m) {
        stop(j, RMR_ERR_OUT_OF_MEMORY, "PDF allocation failed");
        return nullptr;
    }
    m->next = j->memory;
    j->memory = m;
    j->allocated += sizeof *m + n;
    return m + 1;
}
typedef enum { SCALAR, NAME, ARRAY, DICT, REF } kind;
typedef struct node {
    kind type;
    const uint8_t *raw;
    size_t length;
    struct node *child, *next;
    uint32_t number, generation;
} node;
typedef struct {
    job *j;
    const uint8_t *data;
    size_t size, pos;
} scan;
static bool space(uint8_t c) {
    return c == 0 || c == 9 || c == 10 || c == 12 || c == 13 || c == 32;
}
static bool delim(uint8_t c) { return space(c) || strchr("()<>[]{}/%", c) != nullptr; }
static void ws(scan *s) {
    while (s->pos < s->size) {
        uint8_t c = s->data[s->pos];
        if (space(c))
            s->pos++;
        else if (c == '%') {
            while (s->pos < s->size && s->data[s->pos] != '\n' && s->data[s->pos] != '\r')
                s->pos++;
        } else
            break;
    }
}
static bool word(scan *s, const char *w) {
    ws(s);
    size_t n = strlen(w);
    if (n > s->size - s->pos || memcmp(s->data + s->pos, w, n) ||
        (s->pos + n < s->size && !delim(s->data[s->pos + n])))
        return false;
    s->pos += n;
    return true;
}
static bool integer(scan *s, uint64_t *v) {
    ws(s);
    size_t p = s->pos;
    uint64_t n = 0;
    while (p < s->size && s->data[p] >= '0' && s->data[p] <= '9') {
        unsigned d = (unsigned)(s->data[p++] - '0');
        if (n > (UINT64_MAX - d) / 10)
            return false;
        n = n * 10 + d;
    }
    if (p == s->pos || (p < s->size && !delim(s->data[p])))
        return false;
    s->pos = p;
    *v = n;
    return true;
}
static int hex(uint8_t c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}
static bool named(const node *n, const char *name) {
    if (!n || n->type != NAME)
        return false;
    size_t k = 0;
    for (size_t i = 1; i < n->length; i++) {
        uint8_t c = n->raw[i];
        if (c == '#') {
            if (i + 2 >= n->length)
                return false;
            int a = hex(n->raw[++i]), b = hex(n->raw[++i]);
            if (a < 0 || b < 0)
                return false;
            c = (uint8_t)(a * 16 + b);
        }
        if (!name[k] || c != (uint8_t)name[k++])
            return false;
    }
    return name[k] == 0;
}
static bool same_name(const node *a, const node *b) {
    size_t i = 1, k = 1;
    while (i < a->length && k < b->length) {
        unsigned x = a->raw[i++], y = b->raw[k++];
        if (x == '#') {
            x = (unsigned)(hex(a->raw[i]) * 16 + hex(a->raw[i + 1]));
            i += 2;
        }
        if (y == '#') {
            y = (unsigned)(hex(b->raw[k]) * 16 + hex(b->raw[k + 1]));
            k += 2;
        }
        if (x != y)
            return false;
    }
    return i == a->length && k == b->length;
}
static node *get(node *d, const char *key) {
    if (!d || d->type != DICT)
        return nullptr;
    for (node *n = d->child; n && n->next; n = n->next->next)
        if (named(n, key))
            return n->next;
    return nullptr;
}
static node *parse(scan *s, unsigned depth) {
    if (!tick(s->j))
        return nullptr;
    if (depth >= 64) {
        stop(s->j, RMR_ERR_LIMIT, "PDF object nesting limit");
        return nullptr;
    }
    ws(s);
    if (s->pos >= s->size)
        goto bad;
    node *n = take(s->j, sizeof *n);
    if (!n)
        return nullptr;
    size_t start = s->pos;
    uint8_t c = s->data[s->pos++];
    n->raw = s->data + start;
    if (c == '[' || (c == '<' && s->pos < s->size && s->data[s->pos] == '<')) {
        n->type = c == '[' ? ARRAY : DICT;
        if (n->type == DICT)
            s->pos++;
        node **tail = &n->child;
        size_t count = 0;
        while (s->j->status == RMR_OK) {
            ws(s);
            if (s->pos >= s->size)
                goto bad;
            if (n->type == ARRAY && s->data[s->pos] == ']') {
                s->pos++;
                break;
            }
            if (n->type == DICT && s->size - s->pos >= 2 && s->data[s->pos] == '>' &&
                s->data[s->pos + 1] == '>') {
                if (count % 2)
                    goto bad;
                s->pos += 2;
                break;
            }
            node *v = parse(s, depth + 1);
            if (!v)
                return nullptr;
            if (n->type == DICT && count % 2 == 0) {
                if (v->type != NAME)
                    goto bad;
                for (node *key = n->child; key; key = key->next->next) {
                    if (!tick(s->j))
                        return nullptr;
                    if (same_name(key, v))
                        goto bad;
                }
            }
            *tail = v;
            tail = &v->next;
            count++;
        }
    } else if (c == '(') {
        unsigned nesting = 1;
        while (s->pos < s->size && nesting) {
            c = s->data[s->pos++];
            if (c == '\\') {
                if (s->pos == s->size)
                    goto bad;
                s->pos++;
            } else if (c == '(') {
                if (nesting >= 64)
                    goto bad;
                nesting++;
            } else if (c == ')')
                nesting--;
        }
        if (nesting)
            goto bad;
    } else if (c == '<') {
        while (s->pos < s->size && s->data[s->pos] != '>') {
            c = s->data[s->pos++];
            if (!space(c) && hex(c) < 0)
                goto bad;
        }
        if (s->pos == s->size)
            goto bad;
        s->pos++;
    } else {
        if (c == '/')
            n->type = NAME;
        else if (delim(c))
            goto bad;
        while (s->pos < s->size && !delim(s->data[s->pos]))
            s->pos++;
        if (n->type == NAME) {
            for (size_t i = start + 1; i < s->pos; i++)
                if (s->data[i] == '#') {
                    if (s->pos - i < 3 || hex(s->data[i + 1]) < 0 || hex(s->data[i + 2]) < 0)
                        goto bad;
                    i += 2;
                }
        } else {
            size_t end = s->pos;
            scan t = *s;
            t.pos = start;
            uint64_t a, b;
            if (integer(&t, &a) && integer(&t, &b) && word(&t, "R")) {
                if (a > UINT32_MAX || b > 65535)
                    goto bad;
                n->type = REF;
                n->number = (uint32_t)a;
                n->generation = (uint32_t)b;
                s->pos = t.pos;
            } else {
                bool valid = (end - start == 4 && !memcmp(n->raw, "null", 4)) ||
                             (end - start == 4 && !memcmp(n->raw, "true", 4)) ||
                             (end - start == 5 && !memcmp(n->raw, "false", 5));
                size_t i = start;
                bool digits = false, dot = false;
                if (s->data[i] == '+' || s->data[i] == '-')
                    i++;
                for (; i < end; i++) {
                    uint8_t b0 = s->data[i];
                    if (b0 >= '0' && b0 <= '9')
                        digits = true;
                    else if (b0 == '.' && !dot)
                        dot = true;
                    else {
                        digits = false;
                        break;
                    }
                }
                if (!valid && !digits)
                    goto bad;
            }
        }
    }
    n->length = s->pos - start;
    return n;
bad:
    stop(s->j, RMR_ERR_INVALID_ARGUMENT, "malformed PDF object");
    return nullptr;
}
static bool uint_value(node *n, uint64_t *out) {
    if (!n || n->type != SCALAR)
        return false;
    scan s = {.data = n->raw, .size = n->length};
    return integer(&s, out) && s.pos == s.size;
}
/* zlib state is heap memory charged to the scratch budget and released by
 * inflateEnd; each block records its accounted size in front of the payload. */
typedef struct {
    size_t size;
    max_align_t align;
} zblock;
static voidpf pdf_zalloc(voidpf context, uInt count, uInt size) {
    job *j = context;
    if (j->status != RMR_OK)
        return nullptr;
    if (size && count > (SIZE_MAX - sizeof(zblock)) / size) {
        stop(j, RMR_ERR_LIMIT, "zlib allocation overflow");
        return nullptr;
    }
    size_t n = (size_t)count * size + sizeof(zblock);
    if (n > j->limits.maximum_plan_bytes - j->allocated) {
        stop(j, RMR_ERR_LIMIT, "PDF scratch limit");
        return nullptr;
    }
    zblock *b = rmr_calloc(1, n);
    if (!b) {
        stop(j, RMR_ERR_OUT_OF_MEMORY, "zlib allocation failed");
        return nullptr;
    }
    b->size = n;
    j->allocated += n;
    return b + 1;
}
static void pdf_zfree(voidpf context, voidpf address) {
    job *j = context;
    if (!address)
        return;
    zblock *b = (zblock *)address - 1;
    j->allocated -= b->size;
    free(b);
}
/* Grows an arena block that is not yet linked into the arena list. *held is
 * the size already charged to the scratch budget. */
static bool grow_block(job *j, mem **block, size_t *held, size_t bytes) {
    size_t total = sizeof(mem) + bytes;
    if (total > *held && total - *held > j->limits.maximum_plan_bytes - j->allocated) {
        stop(j, RMR_ERR_LIMIT, "PDF scratch limit");
        return false;
    }
    mem *m = rmr_realloc(*block, total);
    if (!m) {
        stop(j, RMR_ERR_OUT_OF_MEMORY, "PDF allocation failed");
        return false;
    }
    j->allocated = j->allocated - *held + total;
    *block = m;
    *held = total;
    return true;
}
enum { DECODED_STREAM_LIMIT = 64 << 20 };
/* Decode only the filters needed by xref/object streams. Flate output is
 * inflated once into a single growing buffer, bounded per stream and by the
 * caller's per-file decoded-byte budget, then adopted by the request arena. */
static bool decode_stream(job *j, size_t *budget, node *dict, const uint8_t *raw, size_t length,
                          const uint8_t **out, size_t *size) {
    node *filter = get(dict, "Filter");
    *out = raw;
    *size = length;
    if (!filter)
        return true;
    if (filter->type == ARRAY && filter->child && !filter->child->next)
        filter = filter->child;
    if (!named(filter, "FlateDecode")) {
        stop(j, RMR_ERR_UNSUPPORTED, "PDF stream filter");
        return false;
    }
    if (length > UINT_MAX) {
        stop(j, RMR_ERR_LIMIT, "encoded PDF stream limit");
        return false;
    }
    z_stream codec = {.zalloc = pdf_zalloc,
                      .zfree = pdf_zfree,
                      .opaque = j,
                      .next_in = (Bytef *)(uintptr_t)raw,
                      .avail_in = (uInt)length};
    if (inflateInit(&codec) != Z_OK) {
        stop(j, RMR_ERR_OUT_OF_MEMORY, "zlib initialisation failed");
        return false;
    }
    mem *block = nullptr;
    size_t held = 0, capacity = 0;
    int status = Z_OK;
    while (tick(j)) {
        if (!codec.avail_out) {
            size_t next = capacity ? capacity * 2 : length < 1024 ? 1024 : length;
            if (next > DECODED_STREAM_LIMIT)
                next = DECODED_STREAM_LIMIT;
            if (next <= capacity) {
                stop(j, RMR_ERR_LIMIT, "decoded PDF stream limit");
                break;
            }
            if (next > *budget) {
                stop(j, RMR_ERR_LIMIT, "decoded PDF data budget");
                break;
            }
            if (!grow_block(j, &block, &held, next))
                break;
            codec.next_out = (Bytef *)(block + 1) + capacity;
            codec.avail_out = (uInt)(next - capacity);
            capacity = next;
        }
        status = inflate(&codec, Z_NO_FLUSH);
        if (status != Z_OK && !(status == Z_BUF_ERROR && !codec.avail_out))
            break;
    }
    inflateEnd(&codec);
    if (j->status == RMR_OK && status != Z_STREAM_END)
        stop(j, status == Z_MEM_ERROR ? RMR_ERR_OUT_OF_MEMORY : RMR_ERR_INVALID_ARGUMENT,
             "invalid Flate stream");
    if (j->status != RMR_OK) {
        free(block);
        j->allocated -= held;
        return false;
    }
    block->next = j->memory;
    j->memory = block;
    *size = (size_t)codec.total_out;
    *budget -= *size;
    uint8_t *decoded = (uint8_t *)(block + 1);
    *out = decoded;
    node *params = get(dict, "DecodeParms");
    if (params && params->type == ARRAY && params->child && !params->child->next)
        params = params->child;
    if (!params ||
        (params->type == SCALAR && params->length == 4 && !memcmp(params->raw, "null", 4)))
        return true;
    if (params->type != DICT)
        goto bad;
    uint64_t predictor = 1, columns = 1, colors = 1, bits = 8;
    if ((get(params, "Predictor") && !uint_value(get(params, "Predictor"), &predictor)) ||
        (get(params, "Columns") && !uint_value(get(params, "Columns"), &columns)) ||
        (get(params, "Colors") && !uint_value(get(params, "Colors"), &colors)) ||
        (get(params, "BitsPerComponent") && !uint_value(get(params, "BitsPerComponent"), &bits)))
        goto bad;
    if (predictor == 1)
        return true;
    if (bits != 8 || !colors || !columns || columns > SIZE_MAX / colors)
        goto bad;
    size_t width = (size_t)(columns * colors);
    if (!width || width >= 64u * 1024u * 1024u)
        goto bad;
    if (predictor == 2) {
        if (*size % width)
            goto bad;
        for (size_t row = 0; row < *size; row += width) {
            if (!tick(j))
                return false;
            for (size_t x = (size_t)colors; x < width; x++)
                decoded[row + x] = (uint8_t)(decoded[row + x] + decoded[row + x - colors]);
        }
        return true;
    }
    if (predictor < 10 || predictor > 15 || *size % (width + 1))
        goto bad;
    size_t rows = *size / (width + 1);
    for (size_t row = 0; row < rows; row++) {
        if (!tick(j))
            return false;
        unsigned kind = decoded[row * (width + 1)];
        if (kind > 4)
            goto bad;
        for (size_t x = 0; x < width; x++) {
            unsigned raw_value = decoded[row * (width + 1) + 1 + x];
            int left = x >= colors ? decoded[row * width + x - colors] : 0;
            int up = row ? decoded[(row - 1) * width + x] : 0;
            int corner = row && x >= colors ? decoded[(row - 1) * width + x - colors] : 0;
            int prediction = 0;
            if (kind == 1)
                prediction = left;
            if (kind == 2)
                prediction = up;
            if (kind == 3)
                prediction = (left + up) / 2;
            if (kind == 4) {
                int p = left + up - corner;
                int a = abs(p - left), b = abs(p - up), c = abs(p - corner);
                prediction = a <= b && a <= c ? left : b <= c ? up : corner;
            }
            decoded[row * width + x] = (uint8_t)(raw_value + (unsigned)prediction);
        }
    }
    *size = rows * width;
    return true;
bad:
    stop(j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF predictor");
    return false;
}
/* Decoded object stream with its parsed (object number, offset) header. */
typedef struct {
    const uint8_t *data;
    size_t size, first, count;
    uint64_t *slots;
} packed;
/* Sorted cross-reference index: newest free entries shadow older live objects.
 * revision counts sections from the newest; table marks classic-table rows. */
typedef struct entry {
    uint32_t number, generation, mapped, container, index;
    size_t revision;
    size_t offset;
    bool live, reading, visited, table;
    node *object;
    const uint8_t *stream;
    size_t stream_size;
    packed *packed;
} entry;
typedef struct page {
    entry *object;
    node *resources, *media, *crop, *rotate;
    struct page *next;
} page;
/* Each cross-reference stream is decoded once and reused by both index passes. */
typedef struct {
    size_t offset, size;
    node *dict;
    const uint8_t *data;
} xref_stream;
enum { MAXIMUM_REVISIONS = 256, MAXIMUM_XREF_STREAMS = 2 * MAXIMUM_REVISIONS };
static const size_t decoded_file_budget = (size_t)256 << 20;
typedef struct {
    job *j;
    const uint8_t *bytes;
    size_t size, last, next_number;
    bool overlay, xref_streams;
    entry *entries;
    size_t count;
    node *trailer, *root;
    page *pages, *last_page;
    size_t page_count;
    size_t decoded_budget; /* Remaining decoded bytes for this file. */
    xref_stream *streams;
    size_t stream_count;
} file;
static entry *lookup(file *f, uint32_t id) {
    size_t lo = 0, hi = f->count;
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (f->entries[m].number < id)
            lo = m + 1;
        else
            hi = m;
    }
    return lo < f->count && f->entries[lo].number == id ? &f->entries[lo] : nullptr;
}
static node *resolve(file *f, node *n, unsigned depth);
static node *object(file *f, entry *e, unsigned depth);
/* Decodes an object stream and its header once per file. */
static packed *unpack(file *f, entry *container, unsigned depth) {
    node *d = object(f, container, depth);
    uint64_t count, first;
    if (!d || !container || container->container || container->generation ||
        !named(get(d, "Type"), "ObjStm") || !uint_value(get(d, "N"), &count) ||
        !uint_value(get(d, "First"), &first) || !count || count > f->j->limits.maximum_elements)
        goto bad;
    packed *p = take(f->j, sizeof *p);
    if (!p || !decode_stream(f->j, &f->decoded_budget, d, container->stream, container->stream_size,
                             &p->data, &p->size))
        return nullptr;
    /* Every header pair needs at least four bytes ("1 0 "), bounding the table. */
    if (first > p->size || count > (first + 1) / 4)
        goto bad;
    p->first = (size_t)first;
    p->count = (size_t)count;
    p->slots = take(f->j, p->count * 2 * sizeof *p->slots);
    if (!p->slots)
        return nullptr;
    scan header = {f->j, p->data, p->first, 0};
    for (size_t i = 0; i < p->count; i++) {
        if (!tick(f->j))
            return nullptr;
        uint64_t id, offset;
        if (!integer(&header, &id) || !integer(&header, &offset) || !id || id > UINT32_MAX ||
            offset >= p->size - p->first || (i && offset <= p->slots[2 * i - 1]))
            goto bad;
        p->slots[2 * i] = id;
        p->slots[2 * i + 1] = offset;
    }
    container->packed = p;
    return p;
bad:
    stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF object stream");
    return nullptr;
}
static node *object(file *f, entry *e, unsigned depth) {
    if (!e || !e->live) {
        stop(f->j, RMR_ERR_INVALID_ARGUMENT, "missing PDF object");
        return nullptr;
    }
    if (e->object)
        return e->object;
    if (e->reading || depth >= 64) {
        stop(f->j, RMR_ERR_INVALID_ARGUMENT, "cyclic PDF object");
        return nullptr;
    }
    e->reading = true;
    if (e->container) {
        entry *container = lookup(f, e->container);
        packed *p = container ? container->packed : nullptr;
        if (!p && !(p = unpack(f, container, depth + 1)))
            return nullptr;
        if (e->index >= p->count || p->slots[2 * (size_t)e->index] != e->number)
            goto bad;
        uint64_t selected = p->slots[2 * (size_t)e->index + 1];
        uint64_t end = (size_t)e->index + 1 < p->count ? p->slots[2 * (size_t)e->index + 3]
                                                       : p->size - p->first;
        scan body = {f->j, p->data + p->first + selected, (size_t)(end - selected), 0};
        node *n = parse(&body, 0);
        ws(&body);
        if (!n || body.pos != body.size)
            goto bad;
        e->object = n;
        e->reading = false;
        return n;
    }
    scan s = {f->j, f->bytes, f->size, e->offset};
    uint64_t id, gen;
    if (!integer(&s, &id) || !integer(&s, &gen) || id != e->number || gen != e->generation ||
        !word(&s, "obj"))
        goto bad;
    node *n = parse(&s, 0);
    if (!n)
        return nullptr;
    if (n->type == DICT && word(&s, "stream")) {
        if (s.pos < f->size && s.data[s.pos] == '\r')
            s.pos++;
        else if (s.pos >= f->size || s.data[s.pos] != '\n')
            goto bad;
        if (s.pos < f->size && s.data[s.pos] == '\n')
            s.pos++;
        uint64_t length;
        node *len = resolve(f, get(n, "Length"), depth + 1);
        if (!uint_value(len, &length) || length > f->size - s.pos)
            goto bad;
        e->stream = f->bytes + s.pos;
        e->stream_size = (size_t)length;
        s.pos += (size_t)length;
        if (!word(&s, "endstream"))
            goto bad;
    }
    if (!word(&s, "endobj"))
        goto bad;
    e->object = n;
    e->reading = false;
    return n;
bad:
    stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF object or stream length");
    return nullptr;
}
static node *resolve(file *f, node *n, unsigned depth) {
    if (!n || n->type != REF)
        return n;
    if (depth >= 64) {
        stop(f->j, RMR_ERR_LIMIT, "PDF reference depth");
        return nullptr;
    }
    entry *e = lookup(f, n->number);
    if (!e || e->generation != n->generation) {
        stop(f->j, RMR_ERR_INVALID_ARGUMENT, "PDF reference generation mismatch");
        return nullptr;
    }
    node *v = object(f, e, depth);
    return v ? resolve(f, v, depth + 1) : nullptr;
}
/* PDF decimal numbers, without locale-dependent conversion or exponent syntax. */
static bool decimal(node *n, double *out) {
    if (!n || n->type != SCALAR || !n->length)
        return false;
    size_t i = 0;
    bool negative = false, dot = false, digit = false;
    if (n->raw[i] == '+' || n->raw[i] == '-')
        negative = n->raw[i++] == '-';
    double value = 0, fraction = .1;
    for (; i < n->length; i++) {
        unsigned c = n->raw[i];
        if (c == '.' && !dot) {
            dot = true;
            continue;
        }
        if (c < '0' || c > '9')
            return false;
        digit = true;
        if (dot) {
            value += (double)(c - '0') * fraction;
            fraction *= .1;
        } else
            value = value * 10 + (double)(c - '0');
    }
    *out = negative ? -value : value;
    return digit && isfinite(value);
}
static bool equal_boxes(file *f, node *a, node *b) {
    a = resolve(f, a, 0);
    b = resolve(f, b, 0);
    if (!a || !b || a->type != ARRAY || b->type != ARRAY)
        return false;
    a = a->child;
    b = b->child;
    for (size_t i = 0; i < 4; i++) {
        double x, y;
        if (!a || !b || !decimal(resolve(f, a, 0), &x) || !decimal(resolve(f, b, 0), &y) || x != y)
            return false;
        a = a->next;
        b = b->next;
    }
    return !a && !b;
}
static int entry_order(const void *a, const void *b) {
    const entry *x = a, *y = b;
    if (x->number != y->number)
        return x->number < y->number ? -1 : 1;
    if (x->revision != y->revision)
        return x->revision < y->revision ? -1 : 1;
    if (x->table != y->table)
        return x->table ? -1 : 1;
    return (x->mapped > y->mapped) - (x->mapped < y->mapped);
}
static xref_stream *load_xref_stream(file *f, size_t offset) {
    for (size_t i = 0; i < f->stream_count; i++)
        if (f->streams[i].offset == offset)
            return &f->streams[i];
    if (!f->streams && !(f->streams = take(f->j, MAXIMUM_XREF_STREAMS * sizeof *f->streams)))
        return nullptr;
    scan s = {f->j, f->bytes, f->size, offset};
    uint64_t number, gen, length, size;
    if (f->stream_count == MAXIMUM_XREF_STREAMS || !integer(&s, &number) || !number ||
        number > UINT32_MAX || !integer(&s, &gen) || gen > 65535 || !word(&s, "obj"))
        goto bad;
    node *d = parse(&s, 0);
    if (!d || d->type != DICT || !named(get(d, "Type"), "XRef") ||
        !uint_value(get(d, "Length"), &length) || !uint_value(get(d, "Size"), &size) || !size ||
        size > UINT32_MAX || !word(&s, "stream"))
        goto bad;
    if (s.pos >= s.size || (s.data[s.pos] != '\r' && s.data[s.pos] != '\n'))
        goto bad;
    if (s.data[s.pos] == '\r')
        s.pos++;
    if (s.pos < s.size && s.data[s.pos] == '\n')
        s.pos++;
    if (length > s.size - s.pos)
        goto bad;
    xref_stream x = {.offset = offset, .dict = d};
    if (!decode_stream(f->j, &f->decoded_budget, d, s.data + s.pos, (size_t)length, &x.data,
                       &x.size))
        return nullptr;
    s.pos += (size_t)length;
    if (!word(&s, "endstream") || !word(&s, "endobj"))
        goto bad;
    f->streams[f->stream_count] = x;
    return &f->streams[f->stream_count++];
bad:
    stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF cross-reference stream");
    return nullptr;
}
static node *stream_section(file *f, size_t offset, bool fill, size_t *used, size_t revision) {
    xref_stream *x = load_xref_stream(f, offset);
    if (!x)
        return nullptr;
    node *d = x->dict;
    const uint8_t *data = x->data;
    size_t count = x->size;
    uint64_t size;
    if (!uint_value(get(d, "Size"), &size))
        goto bad;
    node *w = get(d, "W");
    if (!w || w->type != ARRAY)
        goto bad;
    uint64_t widths[3];
    node *v = w->child;
    for (unsigned i = 0; i < 3; i++) {
        if (!v || !uint_value(v, &widths[i]) || widths[i] > 8)
            goto bad;
        v = v->next;
    }
    size_t stride = (size_t)(widths[0] + widths[1] + widths[2]);
    if (v || !stride)
        goto bad;
    node *index = get(d, "Index");
    if (index && (index->type != ARRAY || !index->child))
        goto bad;
    node *range = index ? index->child : nullptr;
    size_t cursor = 0;
    do {
        uint64_t first = 0, n = size;
        if (range) {
            if (!uint_value(range, &first) || !range->next || !uint_value(range->next, &n))
                goto bad;
            range = range->next->next;
        }
        if (first > size || n > size - first || n > (count - cursor) / stride)
            goto bad;
        for (uint64_t i = 0; i < n; i++) {
            if (!tick(f->j))
                return nullptr;
            uint64_t fields[3] = {widths[0] ? 0 : 1, 0, 0};
            for (unsigned k = 0; k < 3; k++)
                for (uint64_t j = 0; j < widths[k]; j++)
                    fields[k] = (fields[k] << 8) | data[cursor++];
            entry e = {.number = (uint32_t)(first + i), .revision = revision};
            if (fields[0] == 0) {
                if (fields[2] > 65535)
                    goto bad;
            } else if (fields[0] == 1) {
                if (!fields[1] || fields[1] >= f->size || fields[2] > 65535)
                    goto bad;
                e.live = true;
                e.offset = (size_t)fields[1];
                e.generation = (uint32_t)fields[2];
            } else if (fields[0] == 2) {
                if (!fields[1] || fields[1] >= size || fields[2] > UINT32_MAX)
                    goto bad;
                e.live = true;
                e.container = (uint32_t)fields[1];
                e.index = (uint32_t)fields[2];
            } else
                goto bad;
            if (*used == SIZE_MAX)
                goto bad;
            if (fill) {
                if (*used >= f->count)
                    goto bad;
                f->entries[*used] = e;
            }
            (*used)++;
        }
    } while (range);
    if (cursor != count)
        goto bad;
    return d;
bad:
    stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF cross-reference stream");
    return nullptr;
}
/* Build xrefs in two passes. No allocation proportional to a hostile /Size. */
static bool sections(file *f, bool fill) {
    size_t offset = f->last, used = 0, iterations = 0;
    size_t visited[MAXIMUM_REVISIONS];
    while (f->j->status == RMR_OK) {
        if (++iterations > MAXIMUM_REVISIONS) {
            stop(f->j, RMR_ERR_LIMIT, "PDF revision limit");
            break;
        }
        for (size_t i = 0; i + 1 < iterations; i++)
            if (visited[i] == offset)
                goto bad;
        visited[iterations - 1] = offset;
        scan s = {f->j, f->bytes, f->size, offset};
        node *d = nullptr;
        bool classic = word(&s, "xref");
        if (!classic) {
            f->xref_streams = true;
            d = stream_section(f, offset, fill, &used, iterations);
            if (!d)
                return false;
        }
        while (classic && !word(&s, "trailer")) {
            if (!tick(f->j))
                return false;
            uint64_t first, count;
            if (!integer(&s, &first) || !integer(&s, &count) || first > UINT32_MAX ||
                count > UINT32_MAX - first)
                goto bad;
            if (count > f->j->limits.maximum_elements) {
                stop(f->j, RMR_ERR_LIMIT, "PDF xref entry limit");
                return false;
            }
            for (uint64_t i = 0; i < count; i++) {
                uint64_t at, gen;
                if (!tick(f->j))
                    return false;
                if (!integer(&s, &at) || !integer(&s, &gen) || gen > 65535)
                    goto bad;
                bool live = word(&s, "n");
                if (!live && !word(&s, "f"))
                    goto bad;
                if (live && ((at == 0 && (!f->overlay || gen != 0)) || at >= f->size))
                    goto bad;
                if (used == SIZE_MAX)
                    goto bad;
                if (fill) {
                    if (used >= f->count)
                        goto bad;
                    f->entries[used] = (entry){.number = (uint32_t)(first + i),
                                               .generation = (uint32_t)gen,
                                               .offset = (size_t)at,
                                               .live = live,
                                               .table = true,
                                               .revision = iterations};
                }
                used++;
            }
        }
        if (classic)
            d = parse(&s, 0);
        if (!d || d->type != DICT)
            goto bad;
        if (get(d, "Encrypt")) {
            stop(f->j, RMR_ERR_UNSUPPORTED, "encrypted PDF");
            return false;
        }
        if (classic && get(d, "XRefStm")) {
            f->xref_streams = true;
            uint64_t hybrid;
            if (!uint_value(get(d, "XRefStm"), &hybrid) || !hybrid || hybrid >= f->size ||
                hybrid == offset)
                goto bad;
            /* Companion rows share this revision; open_file prefers the table. */
            node *extra = stream_section(f, (size_t)hybrid, fill, &used, iterations);
            if (!extra)
                return false;
            if (get(extra, "Encrypt")) {
                stop(f->j, RMR_ERR_UNSUPPORTED, "encrypted PDF");
                return false;
            }
        }
        if (fill) {
            if (!f->trailer)
                f->trailer = d;
            else { /* Carry missing trailer values from earlier revisions. */
                for (node *k = d->child; k; k = k->next->next)
                    if ((named(k, "Root") && !get(f->trailer, "Root")) ||
                        (named(k, "Info") && !get(f->trailer, "Info")) ||
                        (named(k, "ID") && !get(f->trailer, "ID"))) {
                        node *key = take(f->j, sizeof *key), *value = take(f->j, sizeof *value);
                        if (!key || !value)
                            return false;
                        *key = *k;
                        *value = *k->next;
                        value->next = f->trailer->child;
                        key->next = value;
                        f->trailer->child = key;
                    }
            }
        }
        uint64_t prev;
        node *p = get(d, "Prev");
        if (!p)
            break;
        if (!uint_value(p, &prev) || prev == 0 || prev >= f->size)
            goto bad;
        offset = (size_t)prev;
    }
    if (!fill)
        f->count = used;
    return f->j->status == RMR_OK;
bad:
    stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF cross-reference table");
    return false;
}
static bool tree(file *f, node *ref, node *resources, node *media, node *crop, node *rotate,
                 unsigned depth) {
    if (!tick(f->j))
        return false;
    if (depth >= 64 || !ref || ref->type != REF)
        goto bad;
    entry *e = lookup(f, ref->number);
    if (!e || e->visited || e->generation != ref->generation)
        goto bad;
    e->visited = true;
    node *d = object(f, e, 0);
    if (!d || d->type != DICT)
        goto bad;
    node *v;
    if ((v = get(d, "Resources")))
        resources = v;
    if ((v = get(d, "MediaBox")))
        media = v;
    if ((v = get(d, "CropBox")))
        crop = v;
    if ((v = get(d, "Rotate")))
        rotate = v;
    if (get(d, "UserUnit")) {
        double unit;
        if (!decimal(resolve(f, get(d, "UserUnit"), 0), &unit) || unit != 1) {
            stop(f->j, RMR_ERR_UNSUPPORTED, "non-unit PDF UserUnit");
            return false;
        }
    }
    if (named(get(d, "Type"), "Pages")) {
        node *kids = resolve(f, get(d, "Kids"), 0);
        if (!kids || kids->type != ARRAY)
            goto bad;
        for (node *k = kids->child; k; k = k->next)
            if (!tree(f, k, resources, media, crop, rotate, depth + 1))
                return false;
    } else if (named(get(d, "Type"), "Page")) {
        if (f->page_count >= f->j->limits.maximum_draws) {
            stop(f->j, RMR_ERR_LIMIT, "PDF page limit");
            return false;
        }
        page *p = take(f->j, sizeof *p);
        if (!p)
            return false;
        *p = (page){e, resources, media, crop, rotate, nullptr};
        if (f->last_page)
            f->last_page->next = p;
        else
            f->pages = p;
        f->last_page = p;
        f->page_count++;
    } else
        goto bad;
    return true;
bad:
    stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid or cyclic PDF page tree");
    return false;
}
static bool open_file(file *f, job *j, const uint8_t *bytes, size_t size, bool overlay) {
    *f = (file){.j = j,
                .bytes = bytes,
                .size = size,
                .overlay = overlay,
                .decoded_budget = decoded_file_budget};
    if (size > j->limits.maximum_pdf_bytes) {
        stop(j, RMR_ERR_LIMIT, "PDF input size limit");
        return false;
    }
    if (!bytes || size < 9 || memcmp(bytes, "%PDF-", 5))
        goto bad;
    size_t at = size - 9;
    bool found = false;
    for (;;) {
        if (!memcmp(bytes + at, "startxref", 9)) {
            found = true;
            break;
        }
        if (at == 0 || size - at > 65536)
            break;
        at--;
    }
    if (!found)
        goto bad;
    scan s = {j, bytes, size, at + 9};
    uint64_t offset;
    if (!integer(&s, &offset) || offset == 0 || offset >= size)
        goto bad;
    f->last = (size_t)offset;
    if (!sections(f, false))
        return false;
    if (f->count > SIZE_MAX / sizeof(entry) || f->count > UINT32_MAX)
        goto bad;
    f->entries = take(j, f->count * sizeof(entry));
    if (!f->entries)
        return false;
    if (!sections(f, true))
        return false;
    for (size_t i = 0; i < f->count; i++)
        f->entries[i].mapped = (uint32_t)i;
    /* Rows sort by object, then newest revision, then classic table before
     * stream rows; a row repeated within one section is adjacent. */
    qsort(f->entries, f->count, sizeof(entry), entry_order);
    size_t unique = 0;
    for (size_t i = 0; i < f->count;) {
        size_t end = i + 1;
        while (end < f->count && f->entries[end].number == f->entries[i].number) {
            if (!tick(j))
                return false;
            if (f->entries[end].revision == f->entries[end - 1].revision &&
                f->entries[end].table == f->entries[end - 1].table)
                goto bad;
            end++;
        }
        /* Hybrid files (ISO 32000-1 7.5.8.4): the table wins; its companion
         * stream supplies an object only where the table marks it free. */
        size_t best = i;
        if (f->entries[i].table && !f->entries[i].live && i + 1 < end &&
            f->entries[i + 1].revision == f->entries[i].revision)
            best = i + 1;
        f->entries[unique] = f->entries[best];
        f->entries[unique++].mapped = 0;
        i = end;
    }
    f->count = unique;
    uint64_t declared;
    if (!uint_value(get(f->trailer, "Size"), &declared) || declared > UINT32_MAX || !declared)
        goto bad;
    f->next_number = (size_t)declared;
    if (unique && f->entries[unique - 1].number >= f->next_number)
        f->next_number = (size_t)f->entries[unique - 1].number + 1;
    f->root = resolve(f, get(f->trailer, "Root"), 0);
    if (!f->root || f->root->type != DICT)
        goto bad;
    if (get(f->root, "Perms")) {
        stop(j, RMR_ERR_UNSUPPORTED,
             "certified/rights-managed PDF requires signature permission handling");
        return false;
    }
    return tree(f, get(f->root, "Pages"), nullptr, nullptr, nullptr, nullptr, 0);
bad:
    stop(j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF file");
    return false;
}
typedef struct update {
    uint32_t id, gen;
    size_t offset;
    struct update *next;
} update;
typedef struct {
    job *j;
    rmr_buffer data;
    size_t capacity, next_number;
    update *updates;
    size_t count;
} writer;
static void put(writer *w, const void *bytes, size_t n) {
    if (w->j->status != RMR_OK || !n)
        return;
    if (n > w->j->limits.maximum_pdf_bytes - w->data.size) {
        stop(w->j, RMR_ERR_LIMIT, "PDF output size limit");
        return;
    }
    size_t needed = w->data.size + n;
    if (needed > w->capacity) {
        size_t cap = w->capacity ? w->capacity : 4096;
        while (cap < needed) {
            if (cap > w->j->limits.maximum_pdf_bytes / 2) {
                cap = w->j->limits.maximum_pdf_bytes;
                break;
            }
            cap *= 2;
        }
        if (cap > w->j->limits.maximum_pdf_bytes)
            cap = w->j->limits.maximum_pdf_bytes;
        void *p = rmr_realloc(w->data.bytes, cap);
        if (!p) {
            stop(w->j, RMR_ERR_OUT_OF_MEMORY, "PDF output allocation");
            return;
        }
        w->data.bytes = p;
        w->capacity = cap;
    }
    memcpy(w->data.bytes + w->data.size, bytes, n);
    w->data.size += n;
}
static void text(writer *w, const char *s) { put(w, s, strlen(s)); }
static void format(writer *w, const char *fmt, ...) {
    char b[512];
    va_list a;
    va_start(a, fmt);
    int n = vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    if (n < 0 || (size_t)n >= sizeof b) {
        stop(w->j, RMR_ERR_LIMIT, "PDF number formatting");
        return;
    }
    text(w, b);
}
/* C numeric locale may use a comma. PDF decimal tokens always use a dot. */
static void real(writer *w, double v) {
    if (!isfinite(v) || fabs(v) > w->j->limits.maximum_coordinate) {
        stop(w->j, RMR_ERR_LIMIT, "PDF coordinate limit");
        return;
    }
    char scientific[64], digits[24], fixed[400];
    int n = snprintf(scientific, sizeof scientific, "%.17e", fabs(v));
    if (n < 0 || (size_t)n >= sizeof scientific) {
        stop(w->j, RMR_ERR_LIMIT, "PDF number formatting");
        return;
    }
    char *exponent = strchr(scientific, 'e');
    if (!exponent) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "PDF numeric locale");
        return;
    }
    size_t nd = 0;
    for (char *p = scientific; p < exponent; p++)
        if (*p >= '0' && *p <= '9') {
            if (nd == sizeof digits) {
                stop(w->j, RMR_ERR_LIMIT, "PDF decimal digits");
                return;
            }
            digits[nd++] = *p;
        }
    int point = 1 + (int)strtol(exponent + 1, nullptr, 10);
    size_t used = 0;
    if (v < 0)
        fixed[used++] = '-';
    if (point <= 0) {
        fixed[used++] = '0';
        fixed[used++] = '.';
        for (int i = 0; i < -point; i++)
            fixed[used++] = '0';
        memcpy(fixed + used, digits, nd);
        used += nd;
    } else {
        for (int i = 0; i < point; i++)
            fixed[used++] = (size_t)i < nd ? digits[i] : '0';
        if ((size_t)point < nd) {
            fixed[used++] = '.';
            memcpy(fixed + used, digits + point, nd - (size_t)point);
            used += nd - (size_t)point;
        }
    }
    fixed[used] = 0;
    if (strchr(fixed, '.')) {
        while (used && fixed[used - 1] == '0')
            fixed[--used] = 0;
        if (used && fixed[used - 1] == '.')
            fixed[--used] = 0;
    }
    text(w, fixed);
}
static uint32_t reserve(writer *w) {
    if (w->next_number >= UINT32_MAX) {
        stop(w->j, RMR_ERR_LIMIT, "PDF object number limit");
        return 0;
    }
    return (uint32_t)w->next_number++;
}
static void begin(writer *w, uint32_t id, uint32_t gen) {
    if (!tick(w->j))
        return;
    update *u = take(w->j, sizeof *u);
    if (!u)
        return;
    *u = (update){id, gen, w->data.size, w->updates};
    w->updates = u;
    w->count++;
    format(w, "%u %u obj\n", id, gen);
}
static void end(writer *w) { text(w, "\nendobj\n"); }
static void value(writer *w, file *import, node *n, unsigned depth) {
    if (!n) {
        text(w, "null");
        return;
    }
    if (!tick(w->j))
        return;
    if (depth >= 64) {
        stop(w->j, RMR_ERR_LIMIT, "PDF write nesting");
        return;
    }
    if (n->type == REF && import) {
        entry *e = lookup(import, n->number);
        if (!e || !e->live || !e->mapped || e->generation != n->generation) {
            stop(w->j, RMR_ERR_INVALID_ARGUMENT, "missing imported reference");
            return;
        }
        format(w, "%u 0 R", e->mapped);
    } else if (n->type == ARRAY || n->type == DICT) {
        text(w, n->type == ARRAY ? "[" : "<<");
        for (node *c = n->child; c; c = c->next) {
            value(w, import, c, depth + 1);
            text(w, " ");
        }
        text(w, n->type == ARRAY ? "]" : ">>");
    } else
        put(w, n->raw, n->length);
}
static void fields(writer *w, file *import, node *d, const char *const *skip, size_t count) {
    if (!d)
        return;
    if (d->type != DICT) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "expected PDF dictionary");
        return;
    }
    for (node *k = d->child; k; k = k->next->next) {
        bool omit = false;
        for (size_t i = 0; i < count; i++)
            if (named(k, skip[i]))
                omit = true;
        if (!omit) {
            value(w, import, k, 0);
            text(w, " ");
            value(w, import, k->next, 0);
            text(w, "\n");
        }
    }
}
static uint32_t stream_text(writer *w, const char *s) {
    uint32_t id = reserve(w);
    begin(w, id, 0);
    format(w, "<< /Length %zu >>\nstream\n", strlen(s));
    text(w, s);
    text(w, "\nendstream");
    end(w);
    return id;
}
/* Imports every object stored in the overlay, including objects packed in
 * object streams. Object stream containers and xref streams are file
 * structure, not content, and are not copied. Quartz reserves unused objects
 * as live generation-zero rows at offset zero; they have no object, and a
 * reference to one is rejected by value() (no mapped number). */
static bool import_objects(writer *w, file *f) {
    for (size_t i = 0; i < f->count && w->j->status == RMR_OK; i++) {
        entry *e = &f->entries[i];
        if (!e->live || (!e->offset && !e->container))
            continue;
        node *n = object(f, e, 0);
        if (!n)
            return false;
        node *type = n->type == DICT ? get(n, "Type") : nullptr;
        if (!e->stream || !(named(type, "ObjStm") || named(type, "XRef")))
            e->mapped = reserve(w);
    }
    for (size_t i = 0; i < f->count && w->j->status == RMR_OK; i++) {
        entry *e = &f->entries[i];
        if (!e->mapped)
            continue;
        begin(w, e->mapped, 0);
        value(w, f, e->object, 0);
        if (e->stream) {
            text(w, "\nstream\n");
            put(w, e->stream, e->stream_size);
            text(w, "\nendstream");
        }
        end(w);
    }
    return w->j->status == RMR_OK;
}
static entry *single_stream(file *f, node *contents) {
    if (!contents)
        return nullptr;
    node *v = resolve(f, contents, 0);
    if (v && v->type == ARRAY) {
        if (!v->child || v->child->next) {
            stop(f->j, RMR_ERR_UNSUPPORTED, "overlay PDF needs one content stream");
            return nullptr;
        }
        contents = v->child;
    }
    if (contents->type != REF) {
        stop(f->j, RMR_ERR_INVALID_ARGUMENT, "content stream must be indirect");
        return nullptr;
    }
    entry *e = lookup(f, contents->number);
    if (!e || e->generation != contents->generation || !object(f, e, 0) || !e->stream) {
        stop(f->j, RMR_ERR_INVALID_ARGUMENT, "invalid content stream");
        return nullptr;
    }
    return e;
}
static uint32_t form(writer *w, file *f) {
    page *p = f->pages;
    uint64_t rotation = 0;
    if (p->rotate && (!uint_value(resolve(f, p->rotate, 0), &rotation) || rotation % 360)) {
        stop(w->j, RMR_ERR_UNSUPPORTED, "rotated overlay PDF");
        return 0;
    }
    entry *e = single_stream(f, get(p->object->object, "Contents"));
    if (!e) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "overlay PDF has no content stream");
        return 0;
    }
    node *box = resolve(f, p->media, 0);
    if (!box || box->type != ARRAY) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "overlay has no media box");
        return 0;
    }
    if (!import_objects(w, f))
        return 0;
    uint32_t id = reserve(w);
    begin(w, id, 0);
    text(w, "<< /Type /XObject /Subtype /Form /FormType 1\n/BBox ");
    value(w, f, box, 0);
    text(w, "\n/Resources ");
    value(w, f, p->resources, 0);
    const char *skip[] = {"Type",      "Subtype", "FormType", "BBox",
                          "Resources", "Matrix",  "Group",    "Length"};
    text(w, "\n");
    fields(w, f, e->object, skip, sizeof skip / sizeof *skip);
    node *group = get(p->object->object, "Group");
    if (group) {
        text(w, "/Group ");
        value(w, f, group, 0);
        text(w, "\n");
    }
    format(w, "/Length %zu >>\nstream\n", e->stream_size);
    put(w, e->stream, e->stream_size);
    text(w, "\nendstream");
    end(w);
    return id;
}
static void contents(writer *w, file *f, node *n) {
    if (!n)
        return;
    node *v = resolve(f, n, 0);
    if (!v || (v->type == SCALAR && v->length == 4 && !memcmp(v->raw, "null", 4)))
        return;
    if (v->type == ARRAY) {
        for (node *c = v->child; c; c = c->next) {
            if (!single_stream(f, c))
                return;
            value(w, nullptr, c, 0);
            text(w, " ");
        }
    } else {
        if (!single_stream(f, n))
            return;
        value(w, nullptr, n, 0);
        text(w, " ");
    }
}
static void rectangle(writer *w, rmr_rect b) {
    text(w, "[");
    real(w, b.x);
    text(w, " ");
    real(w, b.y);
    text(w, " ");
    real(w, b.x + b.width);
    text(w, " ");
    real(w, b.y + b.height);
    text(w, "]");
}
static void overlay(writer *w, file *source, const rmr_pdf_overlay *o) {
    page *p = source->pages;
    for (size_t i = 0; i < o->page && p; i++) {
        if (!tick(w->j))
            return;
        p = p->next;
    }
    if (!p) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "overlay page outside source");
        return;
    }
    file imported;
    if (!open_file(&imported, w->j, o->pdf, o->size, true))
        return;
    if (imported.page_count != 1) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "overlay PDF must have one page");
        return;
    }
    uint32_t form_id = form(w, &imported);
    if (w->j->status != RMR_OK)
        return;
    node *resources = resolve(source, p->resources, 0),
         *objects = resolve(source, get(resources, "XObject"), 0);
    if ((resources && resources->type != DICT) || (objects && objects->type != DICT)) {
        stop(w->j, RMR_ERR_INVALID_ARGUMENT, "invalid page resources");
        return;
    }
    char name[80];
    unsigned suffix = 0;
    do {
        if (!tick(w->j))
            return;
        snprintf(name, sizeof name, "RMR%u_%u", form_id, suffix++);
    } while (get(objects, name));
    /* The overlay lives in its own Form resource scope, so its gs/image/font
     * names can never replace names used by the original page. */
    uint32_t invoke = reserve(w);
    writer content = {.j = w->j};
    text(&content, "q\n");
    double m[] = {o->transform.a, o->transform.b,  o->transform.c,
                  o->transform.d, o->transform.tx, o->transform.ty};
    for (size_t i = 0; i < 6; i++) {
        real(&content, m[i]);
        text(&content, " ");
    }
    format(&content, "cm\n/%s Do\nQ\n", name);
    begin(w, invoke, 0);
    format(w, "<< /Length %zu >>\nstream\n", content.data.size);
    put(w, content.data.bytes, content.data.size);
    text(w, "\nendstream");
    end(w);
    free(content.data.bytes);
    uint32_t open = stream_text(w, "q\n"), close = stream_text(w, "Q\n");
    begin(w, p->object->number, p->object->generation);
    text(w, "<<\n");
    const char *skip[] = {"Contents", "Resources", "MediaBox", "CropBox", "ArtBox"};
    fields(w, nullptr, p->object->object, skip, o->replace_box ? 5 : 2);
    if (o->replace_box) {
        text(w, "/MediaBox ");
        rectangle(w, o->box);
        text(w, "\n/CropBox ");
        if (p->crop && !equal_boxes(source, p->crop, p->media))
            value(w, nullptr, p->crop, 0);
        else
            rectangle(w, o->box);
        if (p->media) {
            text(w, "\n/ArtBox ");
            value(w, nullptr, p->media, 0);
        }
        text(w, "\n");
    }
    format(w, "/Contents [%u 0 R ", open);
    contents(w, source, get(p->object->object, "Contents"));
    format(w, "%u 0 R %u 0 R]\n/Resources <<\n", close, invoke);
    const char *xskip[] = {"XObject"};
    fields(w, nullptr, resources, xskip, 1);
    text(w, "/XObject <<\n");
    fields(w, nullptr, objects, nullptr, 0);
    format(w, "/%s %u 0 R\n>>\n>>\n>>", name, form_id);
    end(w);
}
/* A new trailer or xref stream dictionary carries only document identity
 * from the source trailer; stream keys such as /F or /DL never transfer. */
static void trailer_fields(writer *w, node *trailer) {
    static const char *const keep[] = {"Root", "Info", "ID"};
    for (node *k = trailer->child; k; k = k->next->next)
        for (size_t i = 0; i < sizeof keep / sizeof *keep; i++)
            if (named(k, keep[i])) {
                value(w, nullptr, k, 0);
                text(w, " ");
                value(w, nullptr, k->next, 0);
                text(w, "\n");
            }
}
static int update_order(const void *a, const void *b) {
    const update *x = a, *y = b;
    return (x->id > y->id) - (x->id < y->id);
}
static void finish(writer *w, file *f) {
    size_t offset = w->data.size;
    bool hybrid = get(f->trailer, "XRefStm") != nullptr;
    size_t hybrid_offset = offset;
    if (f->xref_streams)
        begin(w, reserve(w), 0);
    if (w->count > SIZE_MAX / sizeof(update)) {
        stop(w->j, RMR_ERR_LIMIT, "xref allocation limit");
        return;
    }
    update *list = take(w->j, w->count * sizeof *list);
    if (!list)
        return;
    size_t i = 0;
    for (update *u = w->updates; u; u = u->next)
        list[i++] = *u;
    qsort(list, w->count, sizeof *list, update_order);
    if (f->xref_streams) {
        if (w->count > SIZE_MAX / 11) {
            stop(w->j, RMR_ERR_LIMIT, "xref stream length");
            return;
        }
        format(w, "<< /Type /XRef /Size %zu ", w->next_number);
        if (!hybrid)
            format(w, "/Prev %zu ", f->last);
        text(w, "/W [1 8 2] /Index [");
        for (i = 0; i < w->count; i++)
            format(w, "%u 1 ", list[i].id);
        format(w, "] /Length %zu\n", w->count * 11);
        trailer_fields(w, f->trailer);
        text(w, ">>\nstream\n");
        for (i = 0; i < w->count; i++) {
            uint8_t row[11] = {1};
            uint64_t at = list[i].offset;
            for (unsigned k = 0; k < 8; k++)
                row[8 - k] = (uint8_t)(at >> (k * 8));
            row[9] = (uint8_t)(list[i].gen >> 8);
            row[10] = (uint8_t)list[i].gen;
            put(w, row, sizeof row);
        }
        text(w, "\nendstream");
        end(w);
        if (!hybrid) {
            format(w, "startxref\n%zu\n%%%%EOF\n", offset);
            return;
        }
        offset = w->data.size;
    }
    text(w, "xref\n");
    for (i = 0; i < w->count; i++) {
        if (list[i].offset > 9999999999ULL) {
            stop(w->j, RMR_ERR_LIMIT, "classic xref offset limit");
            return;
        }
        format(w, "%u 1\n%010zu %05u n \n", list[i].id, list[i].offset, list[i].gen);
    }
    format(w, "trailer\n<< /Size %zu /Prev %zu\n", w->next_number, f->last);
    if (hybrid)
        format(w, "/XRefStm %zu\n", hybrid_offset);
    trailer_fields(w, f->trailer);
    format(w, ">>\nstartxref\n%zu\n%%%%EOF\n", offset);
}
static void release(job *j) {
    while (j->memory) {
        mem *next = j->memory->next;
        free(j->memory);
        j->memory = next;
    }
}
rmr_status rmr_pdf_incremental(const uint8_t *source, size_t size, const rmr_pdf_overlay *overlays,
                               size_t count, const rmr_limits *given, rmr_cancel_fn cancel,
                               void *context, rmr_buffer *out, rmr_pdf_incremental_report *report,
                               rmr_error *error) {
    if (!source || !size || !out || (count && !overlays))
        return rmr_fail(error, RMR_ERR_INVALID_ARGUMENT, 0, "missing PDF update input");
    job j = {.limits = given ? *given : rmr_limits_default(), .cancel = cancel, .context = context};
    writer w = {.j = &j};
    file f;
    if (count > j.limits.maximum_draws)
        stop(&j, RMR_ERR_LIMIT, "overlay count limit");
    if (tick(&j) && open_file(&f, &j, source, size, false)) {
        w.next_number = f.next_number;
        put(&w, source, size);
        if (count)
            text(&w, "\n");
        for (size_t i = 0; i < count && tick(&j); i++) {
            const rmr_pdf_overlay *o = &overlays[i];
            if (!o->pdf || !o->size ||
                !isfinite(o->transform.a * o->transform.d - o->transform.b * o->transform.c) ||
                o->transform.a * o->transform.d == o->transform.b * o->transform.c ||
                (o->replace_box &&
                 (!isfinite(o->box.x) || !isfinite(o->box.y) || !isfinite(o->box.width) ||
                  !isfinite(o->box.height) || o->box.width <= 0 || o->box.height <= 0))) {
                stop(&j, RMR_ERR_INVALID_ARGUMENT, "invalid overlay transform/box");
                break;
            }
            for (size_t k = 0; k < i && tick(&j); k++)
                if (overlays[k].page == o->page)
                    stop(&j, RMR_ERR_INVALID_ARGUMENT, "duplicate page overlay");
            if (j.status == RMR_OK)
                overlay(&w, &f, o);
        }
        if (count && j.status == RMR_OK)
            finish(&w, &f);
    }
    if (j.status == RMR_OK) {
        *out = w.data;
        if (report)
            *report = (rmr_pdf_incremental_report){count, size, true};
    } else
        free(w.data.bytes);
    release(&j);
    return j.status == RMR_OK
               ? RMR_OK
               : rmr_fail(error, j.status, 0, j.reason ? j.reason : "PDF update failed");
}
static bool geometry(file *f, const page *p, rmr_pdf_page_geometry *out) {
    *out = (rmr_pdf_page_geometry){0};
    if (p->media) {
        node *box = resolve(f, p->media, 0), *c = box && box->type == ARRAY ? box->child : nullptr;
        double v[4];
        for (size_t k = 0; k < 4; k++, c = c->next)
            if (!c || !decimal(resolve(f, c, 0), &v[k]))
                return false;
        if (c)
            return false;
        out->has_media = true;
        out->media =
            (rmr_rect){fmin(v[0], v[2]), fmin(v[1], v[3]), fabs(v[2] - v[0]), fabs(v[3] - v[1])};
    }
    if (p->rotate) {
        double r;
        if (!decimal(resolve(f, p->rotate, 0), &r) || fabs(r) > 1e9 || fmod(r, 90) != 0)
            return false;
        long quarter = (long)r % 360;
        out->rotate = (int32_t)(quarter < 0 ? quarter + 360 : quarter);
    }
    return true;
}
rmr_status rmr_pdf_pages(const uint8_t *source, size_t size, const rmr_limits *given,
                         rmr_pdf_page_geometry *pages, size_t capacity, size_t *count,
                         rmr_error *error) {
    if (!source || !size || !count || (capacity && !pages))
        return rmr_fail(error, RMR_ERR_INVALID_ARGUMENT, 0, "missing PDF page input");
    job j = {.limits = given ? *given : rmr_limits_default()};
    file f;
    if (tick(&j) && open_file(&f, &j, source, size, false)) {
        size_t i = 0;
        for (page *p = f.pages; p && tick(&j); p = p->next, i++) {
            rmr_pdf_page_geometry g;
            if (!geometry(&f, p, &g))
                stop(&j, RMR_ERR_INVALID_ARGUMENT, "invalid PDF page box or rotation");
            else if (i < capacity)
                pages[i] = g;
        }
        if (j.status == RMR_OK)
            *count = f.page_count;
    }
    release(&j);
    return j.status == RMR_OK
               ? RMR_OK
               : rmr_fail(error, j.status, 0, j.reason ? j.reason : "PDF inspection failed");
}
