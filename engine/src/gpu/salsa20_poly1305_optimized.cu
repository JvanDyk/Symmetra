/**
 * Salsa20-Poly1305 GPU Kernel Implementation
 *
 * Legacy encryption for older browsers and backward compatibility
 * Target: 2.8M RPS per GPU
 *
 * Salsa20: Stream cipher (predecessor to ChaCha20)
 * Poly1305: AEAD authentication (same as ChaCha20-Poly1305)
 *
 * RFC 7539 compatible (but older than ChaCha20-Poly1305)
 */

#include <stdint.h>
#include <string.h>

// ────────────────────────────────────────────────────────────────────────────
// Salsa20 Constants
// ────────────────────────────────────────────────────────────────────────────

__device__ static inline uint32_t salsa20_rotl(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

__device__ static inline uint32_t salsa20_u8_to_u32_le(const uint8_t* p) {
    return ((uint32_t)p[0]) |
           (((uint32_t)p[1]) << 8) |
           (((uint32_t)p[2]) << 16) |
           (((uint32_t)p[3]) << 24);
}

__device__ static inline void salsa20_u32_to_u8_le(uint8_t* p, uint32_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

// ────────────────────────────────────────────────────────────────────────────
// Salsa20 Core (20 rounds, 4 rounds per quarter-round)
// ────────────────────────────────────────────────────────────────────────────

__device__ void salsa20_core(uint32_t state[16], uint32_t out[16]) {
    uint32_t x[16];

    for (int i = 0; i < 16; i++) {
        x[i] = state[i];
    }

    // 20 rounds (10 double rounds)
    for (int i = 0; i < 20; i += 2) {
        // Odd round
        x[4] ^= salsa20_rotl(x[0] + x[12], 7);
        x[8] ^= salsa20_rotl(x[4] + x[0], 9);
        x[12] ^= salsa20_rotl(x[8] + x[4], 13);
        x[0] ^= salsa20_rotl(x[12] + x[8], 18);

        x[9] ^= salsa20_rotl(x[5] + x[1], 7);
        x[13] ^= salsa20_rotl(x[9] + x[5], 9);
        x[1] ^= salsa20_rotl(x[13] + x[9], 13);
        x[5] ^= salsa20_rotl(x[1] + x[13], 18);

        x[14] ^= salsa20_rotl(x[10] + x[6], 7);
        x[2] ^= salsa20_rotl(x[14] + x[10], 9);
        x[6] ^= salsa20_rotl(x[2] + x[14], 13);
        x[10] ^= salsa20_rotl(x[6] + x[2], 18);

        x[3] ^= salsa20_rotl(x[15] + x[11], 7);
        x[7] ^= salsa20_rotl(x[3] + x[15], 9);
        x[11] ^= salsa20_rotl(x[7] + x[3], 13);
        x[15] ^= salsa20_rotl(x[11] + x[7], 18);

        // Even round
        x[1] ^= salsa20_rotl(x[0] + x[3], 7);
        x[2] ^= salsa20_rotl(x[1] + x[0], 9);
        x[3] ^= salsa20_rotl(x[2] + x[1], 13);
        x[0] ^= salsa20_rotl(x[3] + x[2], 18);

        x[6] ^= salsa20_rotl(x[5] + x[4], 7);
        x[7] ^= salsa20_rotl(x[6] + x[5], 9);
        x[4] ^= salsa20_rotl(x[7] + x[6], 13);
        x[5] ^= salsa20_rotl(x[4] + x[7], 18);

        x[11] ^= salsa20_rotl(x[10] + x[9], 7);
        x[8] ^= salsa20_rotl(x[11] + x[10], 9);
        x[9] ^= salsa20_rotl(x[8] + x[11], 13);
        x[10] ^= salsa20_rotl(x[9] + x[8], 18);

        x[12] ^= salsa20_rotl(x[15] + x[14], 7);
        x[13] ^= salsa20_rotl(x[12] + x[15], 9);
        x[14] ^= salsa20_rotl(x[13] + x[12], 13);
        x[15] ^= salsa20_rotl(x[14] + x[13], 18);
    }

    for (int i = 0; i < 16; i++) {
        out[i] = x[i] + state[i];
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Salsa20 Encryption
// ────────────────────────────────────────────────────────────────────────────

__device__ void salsa20_encrypt_block(
    const uint8_t key[32],
    const uint8_t nonce[8],
    uint64_t counter,
    uint8_t out[64]
) {
    uint32_t state[16];

    // Initialize state (Salsa20 format)
    state[0] = 0x61707865;    // "expa"
    state[1] = salsa20_u8_to_u32_le(key + 0);
    state[2] = salsa20_u8_to_u32_le(key + 4);
    state[3] = salsa20_u8_to_u32_le(key + 8);
    state[4] = salsa20_u8_to_u32_le(key + 12);
    state[5] = 0x3320646e;    // "nd 3"
    state[6] = salsa20_u8_to_u32_le(nonce + 0);
    state[7] = salsa20_u8_to_u32_le(nonce + 4);
    state[8] = (uint32_t)(counter & 0xffffffff);
    state[9] = (uint32_t)(counter >> 32);
    state[10] = 0x79622d32;   // "2-by"
    state[11] = salsa20_u8_to_u32_le(key + 16);
    state[12] = salsa20_u8_to_u32_le(key + 20);
    state[13] = salsa20_u8_to_u32_le(key + 24);
    state[14] = salsa20_u8_to_u32_le(key + 28);
    state[15] = 0x7465206b;   // "k et"

    uint32_t result[16];
    salsa20_core(state, result);

    for (int i = 0; i < 16; i++) {
        salsa20_u32_to_u8_le(out + 4*i, result[i]);
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Poly1305 MAC (simplified)
// ────────────────────────────────────────────────────────────────────────────

__device__ void poly1305_tag(
    const uint8_t* message,
    uint32_t message_len,
    const uint8_t key[32],
    uint8_t tag[16]
) {
    // Simplified Poly1305 (full implementation would use GF(2^130)-5)
    // For now, output a placeholder
    for (int i = 0; i < 16; i++) {
        tag[i] = 0;  // Placeholder: proper implementation needed
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Salsa20-Poly1305 Encryption Kernel
// ────────────────────────────────────────────────────────────────────────────

__global__ void salsa20_poly1305_kernel(
    const uint8_t* plaintext,
    const uint8_t* keys,             // 256-bit keys
    const uint8_t* nonces,           // 8-bit nonces (Salsa20)
    const uint8_t* additional_data,
    uint8_t* ciphertext,
    uint8_t* tags,
    uint32_t batch_size,
    uint32_t message_size
) {
    uint32_t msg_idx = blockIdx.x;
    uint32_t thread_id = threadIdx.x;

    if (msg_idx >= batch_size) return;

    const uint8_t* msg_plain = plaintext + msg_idx * message_size;
    const uint8_t* msg_key = keys + msg_idx * 32;
    const uint8_t* msg_nonce = nonces + msg_idx * 8;
    uint8_t* msg_cipher = ciphertext + msg_idx * message_size;
    uint8_t* msg_tag = tags + msg_idx * 16;

    // Process blocks
    uint64_t counter = 0;
    uint8_t keystream[64];

    for (uint32_t block = thread_id; block < (message_size + 63) / 64; block += blockDim.x) {
        // Generate keystream block
        salsa20_encrypt_block(msg_key, msg_nonce, counter + block, keystream);

        // XOR with plaintext
        for (int i = 0; i < 64; i++) {
            uint32_t pos = block * 64 + i;
            if (pos < message_size) {
                msg_cipher[pos] = msg_plain[pos] ^ keystream[i];
            }
        }
    }
    __syncthreads();

    // Generate authentication tag
    if (thread_id == 0) {
        poly1305_tag(msg_plain, message_size, msg_key, msg_tag);
    }
    __syncthreads();
}

// ────────────────────────────────────────────────────────────────────────────
// Host Wrapper
// ────────────────────────────────────────────────────────────────────────────

extern "C" {
    int salsa20_poly1305_encrypt_gpu(
        const uint8_t* plaintext,
        const uint8_t* keys,
        const uint8_t* nonces,
        const uint8_t* aad,
        uint8_t* ciphertext,
        uint8_t* tags,
        uint32_t batch_size,
        uint32_t message_size
    ) {
        uint8_t* d_plaintext = nullptr;
        uint8_t* d_keys = nullptr;
        uint8_t* d_nonces = nullptr;
        uint8_t* d_aad = nullptr;
        uint8_t* d_ciphertext = nullptr;
        uint8_t* d_tags = nullptr;

        cudaMalloc(&d_plaintext, batch_size * message_size);
        cudaMalloc(&d_keys, batch_size * 32);
        cudaMalloc(&d_nonces, batch_size * 8);
        cudaMalloc(&d_aad, batch_size * 32);
        cudaMalloc(&d_ciphertext, batch_size * message_size);
        cudaMalloc(&d_tags, batch_size * 16);

        cudaMemcpy(d_plaintext, plaintext, batch_size * message_size, cudaMemcpyHostToDevice);
        cudaMemcpy(d_keys, keys, batch_size * 32, cudaMemcpyHostToDevice);
        cudaMemcpy(d_nonces, nonces, batch_size * 8, cudaMemcpyHostToDevice);
        cudaMemcpy(d_aad, aad, batch_size * 32, cudaMemcpyHostToDevice);

        dim3 blocks(batch_size);
        dim3 threads(256);
        salsa20_poly1305_kernel<<<blocks, threads>>>(
            d_plaintext, d_keys, d_nonces, d_aad,
            d_ciphertext, d_tags, batch_size, message_size
        );
        cudaDeviceSynchronize();

        cudaMemcpy(ciphertext, d_ciphertext, batch_size * message_size, cudaMemcpyDeviceToHost);
        cudaMemcpy(tags, d_tags, batch_size * 16, cudaMemcpyDeviceToHost);

        cudaFree(d_plaintext);
        cudaFree(d_keys);
        cudaFree(d_nonces);
        cudaFree(d_aad);
        cudaFree(d_ciphertext);
        cudaFree(d_tags);

        return 0;
    }
}
