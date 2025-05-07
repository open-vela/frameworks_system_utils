/*
 * Copyright (C) 2025 Xiaomi Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <string.h>
#include <murmurhash.h>

uint32_t murmurhash(const char *name)
{
    uint32_t c1 = 0xcc9e2d51;
    uint32_t c2 = 0x1b873593;
    uint32_t n = 0xe6546b64;
    uint32_t m = 5;
    uint32_t h = 0;
    uint32_t k = 0;
    uint32_t r1 = 15;
    uint32_t r2 = 13;
    int i = 0, seed = 0;
    uint8_t *d = (uint8_t *) name; // 32 bit extract from `key'
    const uint32_t *chunks = NULL;
    const uint8_t *tail = NULL; // tail - last 8 bytes
    int len = strlen(name);
    int l = len / 4; // chunk length

    h = seed;

    chunks = (const uint32_t *) (d + l * 4); // body
    tail = (const uint8_t *) (d + l * 4); // last 8 byte chunk of `key'

    // for each 4 byte chunk of `key'
    for (i = -l; i != 0; ++i) {
      // next 4 byte chunk of `key'
      k = chunks[i];

      // encode next 4 byte chunk of `key'
      k *= c1;
      k = (k << r1) | (k >> (32 - r1));
      k *= c2;

      // append to hash
      h ^= k;
      h = (h << r2) | (h >> (32 - r2));
      h = h * m + n;
    }

    k = 0;

    // remainder
    switch (len & 3) { // `len % 4'
      case 3: k ^= (tail[2] << 16);
      case 2: k ^= (tail[1] << 8);
      case 1:
        k ^= tail[0];
        k *= c1;
        k = (k << r1) | (k >> (32 - r1));
        k *= c2;
        h ^= k;
    }

    h ^= len;

    h ^= (h >> 16);
    h *= 0x85ebca6b;
    h ^= (h >> 13);
    h *= 0xc2b2ae35;
    h ^= (h >> 16);

    return h;

}

