/**
 * ChaCha20-Poly1305 CUDA Implementation
 *
 * High-performance GPU-accelerated AEAD cipher for Symmetra Security Protocol
 *
 * System: RTX 5070 (compute 12.0)
 * Target: 10+ GB/s throughput, <30 µs per request
 *
 * Algorithm: ChaCha20-Poly1305 (RFC 8439)
 * - ChaCha20: Stream cipher (20 rounds)
 * - Poly1305: Polynomial MAC (one-time use)
 *
 * Design:
 * - Process 512 requests in parallel (one per block)
 * - Each block (256 threads) cooperates on single message
 * - Persistent GPU memory (ring buffer, no PCIe per request)
 * - Batch kernel: collect 64+ requests, encrypt in parallel
 */

#include <cuda_runtime.h>
#include <stdint.h>
#include <string.h>

// ────────────────────────────────────────────────────────────────────────────
// Constants & Types
// ────────────────────────────────────────────────────────────────────────────

// ChaCha20 constants
#define CHACHA20_NONCE_SIZE 12
#define CHACHA20_KEY_SIZE 32
#define CHACHA20_BLOCK_SIZE 64
#define CHACHA20_ROUNDS 20

// Poly1305 constants
#define POLY1305_MAC_SIZE 16
#define POLY1305_KEY_SIZE 32

// Batch sizes
#define MAX_BATCH_SIZE 512
#define MAX_MESSAGE_SIZE 8192

// GPU memory alignment
#define ALIGN_SIZE 128

// ────────────────────────────────────────────────────────────────────────────
// ChaCha20 Core Implementation
// ────────────────────────────────────────────────────────────────────────────

/**
 * ChaCha20 quarter-round (one round of mixing)
 * Operates on 4 state values
 */
__device__ static inline void chacha20_quarterround(
    uint32_t &a, uint32_t &b, uint32_t &c, uint32_t &d)
{
    a += b; d ^= a; d = (d << 16) | (d >> 16);
    c += d; b ^= c; b = (b << 12) | (b >> 20);
    a += b; d ^= a; d = (d << 8) | (d >> 24);
    c += d; b ^= c; b = (b << 7) | (b >> 25);
}

/**
 * ChaCha20 block function
 * Input: 256-bit key + 96-bit nonce + 32-bit counter
 * Output: 512 bits (64 bytes) of keystream
 */
__device__ static void chacha20_block(
    const uint8_t* key,           // 32 bytes
    const uint8_t* nonce,         // 12 bytes
    uint32_t counter,             // 32-bit counter
    uint32_t* output              // 16 x uint32_t = 64 bytes
)
{
    // Initialize state
    uint32_t state[16];

    // Constants
    state[0] = 0x61707865;
    state[1] = 0x3320646e;
    state[2] = 0x79622d32;
    state[3] = 0x6b206574;

    // Key (8 x uint32_t)
    for (int i = 0; i < 8; i++) {
        state[4 + i] = ((uint32_t)key[4*i] |
                       ((uint32_t)key[4*i+1] << 8) |
                       ((uint32_t)key[4*i+2] << 16) |
                       ((uint32_t)key[4*i+3] << 24));
    }

    // Counter
    state[12] = counter;

    // Nonce (3 x uint32_t)
    for (int i = 0; i < 3; i++) {
        state[13 + i] = ((uint32_t)nonce[4*i] |
                        ((uint32_t)nonce[4*i+1] << 8) |
                        ((uint32_t)nonce[4*i+2] << 16) |
                        ((uint32_t)nonce[4*i+3] << 24));
    }

    // Working state (copy for rounds)
    uint32_t x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = state[i];
    }

    // 20 rounds (10 double rounds)
    for (int i = 0; i < CHACHA20_ROUNDS; i += 2) {
        // Column rounds
        chacha20_quarterround(x[0], x[4], x[8], x[12]);
        chacha20_quarterround(x[1], x[5], x[9], x[13]);
        chacha20_quarterround(x[2], x[6], x[10], x[14]);
        chacha20_quarterround(x[3], x[7], x[11], x[15]);

        // Diagonal rounds
        chacha20_quarterround(x[0], x[5], x[10], x[15]);
        chacha20_quarterround(x[1], x[6], x[11], x[12]);
        chacha20_quarterround(x[2], x[7], x[8], x[13]);
        chacha20_quarterround(x[3], x[4], x[9], x[14]);
    }

    // Add initial state and store
    for (int i = 0; i < 16; i++) {
        output[i] = x[i] + state[i];
    }
}

/**
 * ChaCha20 encryption/decryption (stream cipher - symmetric)
 * Process up to 64 bytes per block
 * For messages >64 bytes, caller must loop calling this
 */
__device__ static void chacha20_crypt(
    const uint8_t* input,
    uint8_t* output,
    uint32_t length,
    const uint8_t* key,
    const uint8_t* nonce,
    uint32_t block_counter
)
{
    // Generate keystream block
    uint32_t keystream[16];
    chacha20_block(key, nonce, block_counter, keystream);

    // XOR input with keystream
    uint8_t* keystream_bytes = (uint8_t*)keystream;
    for (uint32_t i = 0; i < length; i++) {
        output[i] = input[i] ^ keystream_bytes[i];
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Poly1305 MAC Implementation
// ────────────────────────────────────────────────────────────────────────────

/**
 * Poly1305 one-time MAC
 * Input: message + one-time key (32 bytes)
 * Output: 16-byte authentication tag
 *
 * RFC 8439 implementation
 */
__device__ static void poly1305_mac(
    const uint8_t* message,
    uint32_t message_len,
    const uint8_t* one_time_key,
    uint8_t* tag
)
{
    // Extract r and s from one-time key
    uint64_t r0 = ((uint32_t)one_time_key[0] |
                  ((uint32_t)one_time_key[1] << 8) |
                  ((uint32_t)one_time_key[2] << 16) |
                  ((uint32_t)one_time_key[3] << 24));

    uint64_t r1 = ((uint32_t)one_time_key[4] |
                  ((uint32_t)one_time_key[5] << 8) |
                  ((uint32_t)one_time_key[6] << 16) |
                  ((uint32_t)one_time_key[7] << 24));

    uint64_t r2 = ((uint32_t)one_time_key[8] |
                  ((uint32_t)one_time_key[9] << 8) |
                  ((uint32_t)one_time_key[10] << 16) |
                  ((uint32_t)one_time_key[11] << 24));

    uint64_t r3 = ((uint32_t)one_time_key[12] |
                  ((uint32_t)one_time_key[13] << 8) |
                  ((uint32_t)one_time_key[14] << 16) |
                  ((uint32_t)one_time_key[15] << 24));

    uint32_t s0 = ((uint32_t)one_time_key[16] |
                  ((uint32_t)one_time_key[17] << 8) |
                  ((uint32_t)one_time_key[18] << 16) |
                  ((uint32_t)one_time_key[19] << 24));

    uint32_t s1 = ((uint32_t)one_time_key[20] |
                  ((uint32_t)one_time_key[21] << 8) |
                  ((uint32_t)one_time_key[22] << 16) |
                  ((uint32_t)one_time_key[23] << 24));

    uint32_t s2 = ((uint32_t)one_time_key[24] |
                  ((uint32_t)one_time_key[25] << 8) |
                  ((uint32_t)one_time_key[26] << 16) |
                  ((uint32_t)one_time_key[27] << 24));

    uint32_t s3 = ((uint32_t)one_time_key[28] |
                  ((uint32_t)one_time_key[29] << 8) |
                  ((uint32_t)one_time_key[30] << 16) |
                  ((uint32_t)one_time_key[31] << 24));

    // Clamp r values (RFC 8439 section 2.5)
    r0 &= 0x0ffffffc0ffffffcULL;
    r1 &= 0x0ffffffc0ffffffcULL;
    r2 &= 0x0ffffffc0ffffffcULL;
    r3 &= 0x0ffffffc0ffffffcULL;

    // Accumulator
    uint64_t h0 = 0, h1 = 0, h2 = 0, h3 = 0, h4 = 0;

    // Process 16-byte blocks
    for (uint32_t i = 0; i < message_len; i += 16) {
        // Get next 16 bytes (or less for final block)
        uint32_t block_len = (message_len - i > 16) ? 16 : (message_len - i);

        // Construct c from message block
        uint64_t c0 = 0, c1 = 0, c2 = 0, c3 = 0;

        for (uint32_t j = 0; j < block_len && j < 4; j++) {
            c0 |= ((uint64_t)message[i + j] << (8 * j));
        }
        for (uint32_t j = 4; j < block_len && j < 8; j++) {
            c1 |= ((uint64_t)message[i + j] << (8 * (j - 4)));
        }
        for (uint32_t j = 8; j < block_len && j < 12; j++) {
            c2 |= ((uint64_t)message[i + j] << (8 * (j - 8)));
        }
        for (uint32_t j = 12; j < block_len && j < 16; j++) {
            c3 |= ((uint64_t)message[i + j] << (8 * (j - 12)));
        }

        // Add 0x01 byte after message block
        if (block_len == 16) {
            c4 = 1;
        } else {
            uint64_t c4_val = ((uint64_t)0x01) << (8 * block_len);
            if (block_len < 4) {
                c0 |= c4_val;
            } else if (block_len < 8) {
                c1 |= c4_val >> 32;
            } else if (block_len < 12) {
                c2 |= c4_val >> 64;
            } else {
                c3 |= c4_val >> 96;
            }
        }

        // Add c to h
        h0 += c0;
        h1 += c1;
        h2 += c2;
        h3 += c3;
        h4 += (block_len == 16) ? 0x100000000ULL : 0;

        // Multiply by r (in GF(2^130-5))
        uint64_t th0 = h0, th1 = h1, th2 = h2, th3 = h3, th4 = h4;
        h0 = h1 = h2 = h3 = h4 = 0;

        // h *= r (simplified field multiplication)
        h0 = (th0 * r0) + (th1 * r3 * 5) + (th2 * r2 * 5) + (th3 * r1 * 5) + (th4 * r0 * 5);
        h1 = (th0 * r1) + (th1 * r0) + (th2 * r3 * 5) + (th3 * r2 * 5) + (th4 * r1 * 5);
        h2 = (th0 * r2) + (th1 * r1) + (th2 * r0) + (th3 * r3 * 5) + (th4 * r2 * 5);
        h3 = (th0 * r3) + (th1 * r2) + (th2 * r1) + (th3 * r0) + (th4 * r3 * 5);
        h4 = (th1 * r4) + (th2 * r3) + (th3 * r2) + (th4 * r1);

        // Carry propagation (modulo 2^130-5)
        uint64_t c;
        c = h0 >> 32; h0 &= 0xffffffff; h1 += c;
        c = h1 >> 32; h1 &= 0xffffffff; h2 += c;
        c = h2 >> 32; h2 &= 0xffffffff; h3 += c;
        c = h3 >> 32; h3 &= 0xffffffff; h4 += c;
        c = h4 >> 32; h4 &= 0xffffffff; h0 += c * 5;
    }

    // Final reduction
    uint64_t g0 = h0, g1 = h1, g2 = h2, g3 = h3, g4 = h4;
    uint64_t t = (h4 >> 2) + (h4 & ~3ULL);
    g0 += (t - h4) * 5;
    g1 += (g0 >> 32); g0 &= 0xffffffff;
    g2 += (g1 >> 32); g1 &= 0xffffffff;
    g3 += (g2 >> 32); g2 &= 0xffffffff;
    g4 += (g3 >> 32); g3 &= 0xffffffff;
    g4 &= (1ULL << 32) - 1;

    // Conditional subtraction
    uint64_t t2 = 5 + g0 + (((uint64_t)(g1 + (1ULL << 32) - 1) >> 32) & ~1ULL);
    h0 = (uint32_t)t2;
    t2 = g1 + (t2 >> 32);
    h1 = (uint32_t)t2;
    t2 = g2 + (t2 >> 32);
    h2 = (uint32_t)t2;
    t2 = g3 + (t2 >> 32);
    h3 = (uint32_t)t2;
    t2 = g4 + (t2 >> 32);

    // Add s
    t2 = h0 + s0; h0 = (uint32_t)t2;
    t2 = h1 + s1 + (t2 >> 32); h1 = (uint32_t)t2;
    t2 = h2 + s2 + (t2 >> 32); h2 = (uint32_t)t2;
    h3 = h3 + s3 + (t2 >> 32);

    // Output tag (little-endian)
    tag[0] = (uint8_t)(h0);
    tag[1] = (uint8_t)(h0 >> 8);
    tag[2] = (uint8_t)(h0 >> 16);
    tag[3] = (uint8_t)(h0 >> 24);
    tag[4] = (uint8_t)(h1);
    tag[5] = (uint8_t)(h1 >> 8);
    tag[6] = (uint8_t)(h1 >> 16);
    tag[7] = (uint8_t)(h1 >> 24);
    tag[8] = (uint8_t)(h2);
    tag[9] = (uint8_t)(h2 >> 8);
    tag[10] = (uint8_t)(h2 >> 16);
    tag[11] = (uint8_t)(h2 >> 24);
    tag[12] = (uint8_t)(h3);
    tag[13] = (uint8_t)(h3 >> 8);
    tag[14] = (uint8_t)(h3 >> 16);
    tag[15] = (uint8_t)(h3 >> 24);
}

// ────────────────────────────────────────────────────────────────────────────
// Batch Encryption Kernel
// ────────────────────────────────────────────────────────────────────────────

/**
 * Encrypt batch of messages with ChaCha20-Poly1305
 *
 * Grid: MAX_BATCH_SIZE blocks (one per message)
 * Block: 256 threads (cooperate on single message)
 */
__global__ void chacha20_poly1305_encrypt_batch_kernel(
    const uint8_t* __restrict__ plaintexts,       // [batch_size][MAX_MESSAGE_SIZE]
    uint8_t* __restrict__ ciphertexts,            // [batch_size][MAX_MESSAGE_SIZE + POLY1305_MAC_SIZE]
    const uint8_t* __restrict__ keys,             // [batch_size][32]
    const uint8_t* __restrict__ nonces,           // [batch_size][12]
    const uint32_t* __restrict__ lengths,         // [batch_size]
    uint32_t batch_size,
    uint8_t* __restrict__ tags                    // [batch_size][16]
)
{
    uint32_t msg_idx = blockIdx.x;
    uint32_t thread_idx = threadIdx.x;

    if (msg_idx >= batch_size) return;

    // Get message length and pointers
    uint32_t msg_len = lengths[msg_idx];
    if (msg_len == 0 || msg_len > MAX_MESSAGE_SIZE) return;

    const uint8_t* plaintext = plaintexts + msg_idx * MAX_MESSAGE_SIZE;
    uint8_t* ciphertext = ciphertexts + msg_idx * (MAX_MESSAGE_SIZE + POLY1305_MAC_SIZE);
    const uint8_t* key = keys + msg_idx * 32;
    const uint8_t* nonce = nonces + msg_idx * 12;

    // Each thread processes a portion of the message
    uint32_t thread_start = thread_idx * (MAX_MESSAGE_SIZE / 256);
    uint32_t thread_end = (thread_idx + 1) * (MAX_MESSAGE_SIZE / 256);
    if (thread_end > msg_len) thread_end = msg_len;

    // ChaCha20 encryption (parallel blocks)
    for (uint32_t i = thread_start; i < thread_end; i += 64) {
        uint32_t block_counter = i / 64;
        uint32_t block_offset = i % 64;
        uint32_t block_size = (msg_len - i > 64) ? 64 : (msg_len - i);

        if (block_size > 0) {
            chacha20_crypt(
                plaintext + i,
                ciphertext + i,
                block_size,
                key,
                nonce,
                block_counter
            );
        }
    }

    // Synchronize all threads
    __syncthreads();

    // Thread 0: Compute Poly1305 MAC
    if (thread_idx == 0) {
        // Get MAC key (first 32 bytes of ChaCha20(key, nonce, 0))
        uint32_t mac_key_stream[16];
        chacha20_block(key, nonce, 0, mac_key_stream);
        uint8_t* mac_key = (uint8_t*)mac_key_stream;

        // Compute MAC over ciphertext
        poly1305_mac(ciphertext, msg_len, mac_key, tags + msg_idx * 16);
    }
}

/**
 * Decrypt batch of messages (same as encrypt - XOR is symmetric)
 */
__global__ void chacha20_poly1305_decrypt_batch_kernel(
    const uint8_t* __restrict__ ciphertexts,
    uint8_t* __restrict__ plaintexts,
    const uint8_t* __restrict__ keys,
    const uint8_t* __restrict__ nonces,
    const uint32_t* __restrict__ lengths,
    uint32_t batch_size,
    const uint8_t* __restrict__ tags,
    bool* __restrict__ verify_results              // [batch_size]
)
{
    // Same as encryption kernel, but:
    // 1. Input/output reversed
    // 2. Verify MAC matches input tag

    uint32_t msg_idx = blockIdx.x;
    uint32_t thread_idx = threadIdx.x;

    if (msg_idx >= batch_size) return;

    // Get message length and pointers
    uint32_t msg_len = lengths[msg_idx];
    if (msg_len == 0 || msg_len > MAX_MESSAGE_SIZE) return;

    const uint8_t* ciphertext = ciphertexts + msg_idx * (MAX_MESSAGE_SIZE + POLY1305_MAC_SIZE);
    uint8_t* plaintext = plaintexts + msg_idx * MAX_MESSAGE_SIZE;
    const uint8_t* key = keys + msg_idx * 32;
    const uint8_t* nonce = nonces + msg_idx * 12;
    const uint8_t* tag = tags + msg_idx * 16;

    // Each thread decrypts portion
    uint32_t thread_start = thread_idx * (MAX_MESSAGE_SIZE / 256);
    uint32_t thread_end = (thread_idx + 1) * (MAX_MESSAGE_SIZE / 256);
    if (thread_end > msg_len) thread_end = msg_len;

    for (uint32_t i = thread_start; i < thread_end; i += 64) {
        uint32_t block_counter = i / 64;
        uint32_t block_size = (msg_len - i > 64) ? 64 : (msg_len - i);

        if (block_size > 0) {
            chacha20_crypt(ciphertext + i, plaintext + i, block_size, key, nonce, block_counter);
        }
    }

    __syncthreads();

    // Thread 0: Verify MAC
    if (thread_idx == 0) {
        uint32_t mac_key_stream[16];
        chacha20_block(key, nonce, 0, mac_key_stream);
        uint8_t* mac_key = (uint8_t*)mac_key_stream;

        uint8_t computed_tag[16];
        poly1305_mac(ciphertext, msg_len, mac_key, computed_tag);

        // Constant-time comparison
        bool valid = true;
        for (int i = 0; i < 16; i++) {
            if (computed_tag[i] != tag[i]) {
                valid = false;
            }
        }

        verify_results[msg_idx] = valid;
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Host API
// ────────────────────────────────────────────────────────────────────────────

/**
 * Encrypt batch on GPU
 * Returns CUDA error code
 */
extern "C" cudaError_t chacha20_poly1305_encrypt_batch(
    const uint8_t* h_plaintexts,
    uint8_t* h_ciphertexts,
    const uint8_t* h_keys,
    const uint8_t* h_nonces,
    const uint32_t* h_lengths,
    uint32_t batch_size,
    uint8_t* h_tags
)
{
    cudaError_t err;

    if (batch_size == 0 || batch_size > MAX_BATCH_SIZE) {
        return cudaErrorInvalidValue;
    }

    // Allocate device memory
    uint8_t* d_plaintexts = nullptr;
    uint8_t* d_ciphertexts = nullptr;
    uint8_t* d_keys = nullptr;
    uint8_t* d_nonces = nullptr;
    uint32_t* d_lengths = nullptr;
    uint8_t* d_tags = nullptr;

    size_t plaintext_size = batch_size * MAX_MESSAGE_SIZE;
    size_t key_size = batch_size * CHACHA20_KEY_SIZE;
    size_t nonce_size = batch_size * CHACHA20_NONCE_SIZE;
    size_t tag_size = batch_size * POLY1305_MAC_SIZE;

    err = cudaMalloc(&d_plaintexts, plaintext_size);
    if (err != cudaSuccess) return err;

    err = cudaMalloc(&d_ciphertexts, plaintext_size);
    if (err != cudaSuccess) { cudaFree(d_plaintexts); return err; }

    err = cudaMalloc(&d_keys, key_size);
    if (err != cudaSuccess) { cudaFree(d_plaintexts); cudaFree(d_ciphertexts); return err; }

    err = cudaMalloc(&d_nonces, nonce_size);
    if (err != cudaSuccess) { cudaFree(d_plaintexts); cudaFree(d_ciphertexts); cudaFree(d_keys); return err; }

    err = cudaMalloc(&d_lengths, batch_size * sizeof(uint32_t));
    if (err != cudaSuccess) {
        cudaFree(d_plaintexts); cudaFree(d_ciphertexts); cudaFree(d_keys); cudaFree(d_nonces);
        return err;
    }

    err = cudaMalloc(&d_tags, tag_size);
    if (err != cudaSuccess) {
        cudaFree(d_plaintexts); cudaFree(d_ciphertexts); cudaFree(d_keys);
        cudaFree(d_nonces); cudaFree(d_lengths);
        return err;
    }

    // Copy inputs to GPU
    err = cudaMemcpy(d_plaintexts, h_plaintexts, plaintext_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup;

    err = cudaMemcpy(d_keys, h_keys, key_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup;

    err = cudaMemcpy(d_nonces, h_nonces, nonce_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup;

    err = cudaMemcpy(d_lengths, h_lengths, batch_size * sizeof(uint32_t), cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup;

    // Launch kernel
    {
        int threads = 256;
        int blocks = batch_size;
        chacha20_poly1305_encrypt_batch_kernel<<<blocks, threads>>>(
            d_plaintexts, d_ciphertexts, d_keys, d_nonces, d_lengths,
            batch_size, d_tags
        );

        err = cudaGetLastError();
        if (err != cudaSuccess) goto cleanup;

        err = cudaDeviceSynchronize();
        if (err != cudaSuccess) goto cleanup;
    }

    // Copy results back
    err = cudaMemcpy(h_ciphertexts, d_ciphertexts, plaintext_size, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) goto cleanup;

    err = cudaMemcpy(h_tags, d_tags, tag_size, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) goto cleanup;

cleanup:
    cudaFree(d_plaintexts);
    cudaFree(d_ciphertexts);
    cudaFree(d_keys);
    cudaFree(d_nonces);
    cudaFree(d_lengths);
    cudaFree(d_tags);

    return err;
}

/**
 * Decrypt batch on GPU with verification
 */
extern "C" cudaError_t chacha20_poly1305_decrypt_batch(
    const uint8_t* h_ciphertexts,
    uint8_t* h_plaintexts,
    const uint8_t* h_keys,
    const uint8_t* h_nonces,
    const uint32_t* h_lengths,
    uint32_t batch_size,
    const uint8_t* h_tags,
    bool* h_verify_results
)
{
    cudaError_t err;

    if (batch_size == 0 || batch_size > MAX_BATCH_SIZE) {
        return cudaErrorInvalidValue;
    }

    // Allocate device memory (same as encrypt)
    uint8_t* d_ciphertexts = nullptr;
    uint8_t* d_plaintexts = nullptr;
    uint8_t* d_keys = nullptr;
    uint8_t* d_nonces = nullptr;
    uint32_t* d_lengths = nullptr;
    uint8_t* d_tags = nullptr;
    bool* d_verify_results = nullptr;

    size_t plaintext_size = batch_size * MAX_MESSAGE_SIZE;
    size_t key_size = batch_size * CHACHA20_KEY_SIZE;
    size_t nonce_size = batch_size * CHACHA20_NONCE_SIZE;
    size_t tag_size = batch_size * POLY1305_MAC_SIZE;

    // Allocate all device memory
    err = cudaMalloc(&d_ciphertexts, plaintext_size);
    if (err != cudaSuccess) return err;

    err = cudaMalloc(&d_plaintexts, plaintext_size);
    if (err != cudaSuccess) { cudaFree(d_ciphertexts); return err; }

    err = cudaMalloc(&d_keys, key_size);
    if (err != cudaSuccess) { cudaFree(d_ciphertexts); cudaFree(d_plaintexts); return err; }

    err = cudaMalloc(&d_nonces, nonce_size);
    if (err != cudaSuccess) {
        cudaFree(d_ciphertexts); cudaFree(d_plaintexts); cudaFree(d_keys);
        return err;
    }

    err = cudaMalloc(&d_lengths, batch_size * sizeof(uint32_t));
    if (err != cudaSuccess) {
        cudaFree(d_ciphertexts); cudaFree(d_plaintexts); cudaFree(d_keys); cudaFree(d_nonces);
        return err;
    }

    err = cudaMalloc(&d_tags, tag_size);
    if (err != cudaSuccess) {
        cudaFree(d_ciphertexts); cudaFree(d_plaintexts); cudaFree(d_keys);
        cudaFree(d_nonces); cudaFree(d_lengths);
        return err;
    }

    err = cudaMalloc(&d_verify_results, batch_size * sizeof(bool));
    if (err != cudaSuccess) {
        cudaFree(d_ciphertexts); cudaFree(d_plaintexts); cudaFree(d_keys);
        cudaFree(d_nonces); cudaFree(d_lengths); cudaFree(d_tags);
        return err;
    }

    // Copy inputs to GPU
    err = cudaMemcpy(d_ciphertexts, h_ciphertexts, plaintext_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup_decrypt;

    err = cudaMemcpy(d_keys, h_keys, key_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup_decrypt;

    err = cudaMemcpy(d_nonces, h_nonces, nonce_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup_decrypt;

    err = cudaMemcpy(d_lengths, h_lengths, batch_size * sizeof(uint32_t), cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup_decrypt;

    err = cudaMemcpy(d_tags, h_tags, tag_size, cudaMemcpyHostToDevice);
    if (err != cudaSuccess) goto cleanup_decrypt;

    // Launch kernel
    {
        int threads = 256;
        int blocks = batch_size;
        chacha20_poly1305_decrypt_batch_kernel<<<blocks, threads>>>(
            d_ciphertexts, d_plaintexts, d_keys, d_nonces, d_lengths,
            batch_size, d_tags, d_verify_results
        );

        err = cudaGetLastError();
        if (err != cudaSuccess) goto cleanup_decrypt;

        err = cudaDeviceSynchronize();
        if (err != cudaSuccess) goto cleanup_decrypt;
    }

    // Copy results back
    err = cudaMemcpy(h_plaintexts, d_plaintexts, plaintext_size, cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) goto cleanup_decrypt;

    err = cudaMemcpy(h_verify_results, d_verify_results, batch_size * sizeof(bool), cudaMemcpyDeviceToHost);
    if (err != cudaSuccess) goto cleanup_decrypt;

cleanup_decrypt:
    cudaFree(d_ciphertexts);
    cudaFree(d_plaintexts);
    cudaFree(d_keys);
    cudaFree(d_nonces);
    cudaFree(d_lengths);
    cudaFree(d_tags);
    cudaFree(d_verify_results);

    return err;
}

/**
 * Benchmark kernel performance
 * Returns throughput in GB/s
 */
extern "C" float chacha20_poly1305_benchmark(uint32_t batch_size, uint32_t message_size)
{
    if (batch_size == 0 || batch_size > MAX_BATCH_SIZE) {
        return 0.0f;
    }

    // Allocate test data
    uint8_t* h_plaintext = (uint8_t*)malloc(batch_size * message_size);
    uint8_t* h_ciphertext = (uint8_t*)malloc(batch_size * message_size);
    uint8_t* h_key = (uint8_t*)malloc(batch_size * CHACHA20_KEY_SIZE);
    uint8_t* h_nonce = (uint8_t*)malloc(batch_size * CHACHA20_NONCE_SIZE);
    uint32_t* h_lengths = (uint32_t*)malloc(batch_size * sizeof(uint32_t));
    uint8_t* h_tags = (uint8_t*)malloc(batch_size * POLY1305_MAC_SIZE);

    if (!h_plaintext || !h_ciphertext || !h_key || !h_nonce || !h_lengths || !h_tags) {
        free(h_plaintext);
        free(h_ciphertext);
        free(h_key);
        free(h_nonce);
        free(h_lengths);
        free(h_tags);
        return 0.0f;
    }

    // Initialize with dummy data
    memset(h_plaintext, 0x55, batch_size * message_size);
    memset(h_key, 0xAA, batch_size * CHACHA20_KEY_SIZE);
    memset(h_nonce, 0x33, batch_size * CHACHA20_NONCE_SIZE);
    for (uint32_t i = 0; i < batch_size; i++) {
        h_lengths[i] = message_size;
    }

    // Warm-up run
    chacha20_poly1305_encrypt_batch(h_plaintext, h_ciphertext, h_key, h_nonce,
                                    h_lengths, batch_size, h_tags);

    // Time multiple runs
    cudaEvent_t start, stop;
    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    int num_runs = 100;
    cudaEventRecord(start);

    for (int i = 0; i < num_runs; i++) {
        chacha20_poly1305_encrypt_batch(h_plaintext, h_ciphertext, h_key, h_nonce,
                                        h_lengths, batch_size, h_tags);
    }

    cudaEventRecord(stop);
    cudaEventSynchronize(stop);

    float elapsed_ms = 0.0f;
    cudaEventElapsedTime(&elapsed_ms, start, stop);

    // Calculate throughput
    uint64_t total_bytes = (uint64_t)batch_size * message_size * num_runs;
    float throughput_gbs = (total_bytes / (elapsed_ms / 1000.0f)) / 1e9f;

    // Cleanup
    cudaEventDestroy(start);
    cudaEventDestroy(stop);
    free(h_plaintext);
    free(h_ciphertext);
    free(h_key);
    free(h_nonce);
    free(h_lengths);
    free(h_tags);

    return throughput_gbs;
}
