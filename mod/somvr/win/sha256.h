/* sha256.h - SHA-256 (FIPS 180-4), for the exe gate (win/somvr_win_main.c); platform-neutral, tested on Linux */
#ifndef SOMVR_SHA256_H
#define SOMVR_SHA256_H
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t h[8]; uint64_t n; unsigned char buf[64]; size_t k; } somvr_sha256;
void somvr_sha256_init(somvr_sha256 *s);
void somvr_sha256_add(somvr_sha256 *s, const void *p, size_t n);
void somvr_sha256_hex(somvr_sha256 *s, char out[65]); /* finishes s */
#endif
