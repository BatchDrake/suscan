/*********************************************************************
* Filename:   sha256.h
* Author:     Brad Conte (brad AT bradconte.com)
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding SHA1 implementation.
*********************************************************************/

#ifndef _UTIL_SHA256_H
#define _UTIL_SHA256_H

/*************************** HEADER FILES ***************************/
#include <stddef.h>
#include <stdint.h>

/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32            // SHA256 outputs a 32 byte digest

typedef struct {
        uint8_t data[64];
        uint32_t datalen;
        unsigned long long bitlen;
        uint32_t state[8];
} SHA256_CTX;

/*********************** FUNCTION DECLARATIONS **********************/
void suscan_sha256_init(SHA256_CTX *ctx);
void suscan_sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);
void suscan_sha256_final(SHA256_CTX *ctx, uint8_t hash[]);

#endif   /* _UTIL_SHA256_H */
