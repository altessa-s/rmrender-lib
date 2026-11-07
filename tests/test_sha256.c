/* Copyright 2026 ALTESSA SOLUTIONS INC.
 * Use of this source code is governed by an MIT-style license that can be
 * found in the LICENSE file. */

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "rmr_sha256.h"

static void hex_digest(rmr_sha256_digest d, char out[65]) {
  for (size_t i = 0; i < 32; i++) snprintf(out + 2 * i, 3, "%02x", d.bytes[i]);
}

static void expect(rmr_sha256_digest d, const char* digest) {
  char got[65];
  hex_digest(d, got);
  if (strcmp(got, digest)) {
    fprintf(stderr, "sha256 %s, expected %s\n", got, digest);
    abort();
  }
}

/* FIPS 180-4 / NIST CSHA example vectors, each taken at once and in pieces
 * of every size from 1 to 130 bytes. */
static void vectors(void) {
  static const struct {
    const char *text, *digest;
  } cases[] = {
      {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
      {"abc",
       "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
      {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
       "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
      {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmn"
       "opjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
       "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
      {"The quick brown fox jumps over the lazy dog",
       "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
  };
  for (size_t c = 0; c < sizeof cases / sizeof *cases; c++) {
    const char* text = cases[c].text;
    size_t n = strlen(text);
    expect(rmr_sha256_of(text, n), cases[c].digest);
    for (size_t piece = 1; piece <= 130; piece++) {
      rmr_sha256 h;
      rmr_sha256_init(&h);
      for (size_t at = 0; at < n; at += piece)
        rmr_sha256_update(&h, text + at, n - at < piece ? n - at : piece);
      expect(rmr_sha256_final(&h), cases[c].digest);
    }
  }
}

/* One million 'a's, at once and streamed in uneven pieces across blocks. */
static void million(void) {
  const char* digest =
      "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0";
  enum { N = 1000000 };
  char* a = malloc(N);
  assert(a);
  memset(a, 'a', N);
  expect(rmr_sha256_of(a, N), digest);
  static const size_t pieces[] = {1, 3, 55, 56, 63, 64, 65, 127, 4099};
  for (size_t p = 0; p < sizeof pieces / sizeof *pieces; p++) {
    rmr_sha256 h;
    rmr_sha256_init(&h);
    size_t at = 0, step = 0;
    while (at < N) {
      size_t n = pieces[(p + step++) % (sizeof pieces / sizeof *pieces)];
      if (n > N - at) n = N - at;
      rmr_sha256_update(&h, a + at, n);
      at += n;
    }
    expect(rmr_sha256_final(&h), digest);
  }
  free(a);
}

int main(void) {
  vectors();
  million();
  puts("SHA-256 vectors passed");
  return 0;
}
