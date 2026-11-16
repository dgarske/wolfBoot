/* versal_enckey.c
 *
 * Image-encryption key provider for the AMD Versal disk-boot configurations
 * (CUSTOM_ENCRYPT_KEY). This one returns a fixed demonstration key: replace
 * wolfBoot_get_encrypt_key() with a read from eFuse, BBRAM or a PUF-wrapped
 * store before using encrypted images in a product.
 *
 * Copyright (C) 2026 wolfSSL Inc.
 *
 * This file is part of wolfBoot.
 *
 * wolfBoot is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * wolfBoot is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1335, USA
 */

#include <stdint.h>
#include <string.h>
#include "wolfboot/wolfboot.h"
#include "image.h"

#if defined(EXT_ENCRYPTED) && defined(CUSTOM_ENCRYPT_KEY)

#if (ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE) != 48
#error The demo key below is sized for AES-256 (32-byte key, 16-byte IV)
#endif

/* Same bytes as the key file given to the sign tool: key || nonce. */
static const uint8_t versal_demo_key[ENCRYPT_KEY_SIZE + ENCRYPT_NONCE_SIZE] =
    "0123456789abcdef0123456789abcdef0123456789abcdef";

int RAMFUNCTION wolfBoot_get_encrypt_key(uint8_t *key, uint8_t *nonce)
{
    if (key == NULL || nonce == NULL)
        return -1;
    memcpy(key, versal_demo_key, ENCRYPT_KEY_SIZE);
    memcpy(nonce, versal_demo_key + ENCRYPT_KEY_SIZE, ENCRYPT_NONCE_SIZE);
    return 0;
}

/* The key is fixed at build time: nothing to store or erase. */
int RAMFUNCTION wolfBoot_set_encrypt_key(const uint8_t *key,
    const uint8_t *nonce)
{
    (void)key;
    (void)nonce;
    return -1;
}

int RAMFUNCTION wolfBoot_erase_encrypt_key(void)
{
    return -1;
}

#endif /* EXT_ENCRYPTED && CUSTOM_ENCRYPT_KEY */
