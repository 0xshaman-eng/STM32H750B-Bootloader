/*********************************************************************
* Filename:   sha256.h
* Author:     Shaman Karunaratne
* Based on:   Brad Conte (brad AT bradconte.com) - public-domain SHA-256 implementation
* Copyright:
* Disclaimer: This code is presented "as is" without any guarantees.
* Details:    Defines the API for the corresponding SHA256 implementation.
*********************************************************************/

#ifndef SHA256_H
#define SHA256_H

/*************************** HEADER FILES ***************************/
#include <stddef.h>
#include <stdint.h>

/****************************** MACROS ******************************/
#define SHA256_BLOCK_SIZE 32            // SHA256 outputs a 32 byte digest

/**************************** DATA TYPES ****************************/
typedef struct {
	uint8_t data[64];
	uint32_t datalen;
	unsigned long long bitlen;
	uint32_t state[8];
} SHA256_CTX;

/*********************** FUNCTION DECLARATIONS **********************/
/**
  * @brief  Initialise a SHA-256 context.
  * @param  ctx  Context to initialise.
  */
void sha256_init(SHA256_CTX *ctx);

/**
  * @brief  Feed data into the hash. May be called repeatedly.
  * @param  ctx   Initialised context.
  * @param  data  Input bytes.
  * @param  len   Number of input bytes.
  */
void sha256_update(SHA256_CTX *ctx, const uint8_t data[], size_t len);

/**
  * @brief  Finish the hash and write the digest.
  * @param  ctx   Context fed via sha256_update().
  * @param  hash  Output buffer of ::SHA256_BLOCK_SIZE (32) bytes.
  */
void sha256_final(SHA256_CTX *ctx, uint8_t hash[]);

#endif   // SHA256_H
