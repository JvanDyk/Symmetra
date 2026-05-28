#include <iostream>
#include <cassert>
#include <cstring>
#include <array>
#include "protect/Aes256.h"

namespace symm {

// Test fixtures
struct TlsSession {
    bool tlsEnabled = false;
    bool handshakeDone = false;
    std::array<uint8_t, 32> clientKey{};
    std::array<uint8_t, 32> serverKey{};
    uint64_t clientSeq = 0;
    uint64_t serverSeq = 0;
};

// Mock implementations for testing
void mockTlsDeriveKeys(TlsSession& sess, const uint8_t* serverRandom, const uint8_t* clientRandom) {
    // Simplified: just XOR nonces with a test PSK
    std::array<uint8_t, 32> testPsk{};
    for (int i = 0; i < 32; i++) testPsk[i] = 0xAA;  // Simple test PSK

    for (int i = 0; i < 32; i++) {
        sess.clientKey[i] = testPsk[i] ^ serverRandom[i % 16] ^ clientRandom[i % 16];
        sess.serverKey[i] = testPsk[i] ^ clientRandom[i % 16] ^ serverRandom[i % 16];
    }
    sess.handshakeDone = true;
}

void runTlsTests() {
    std::cout << "\n=== TLS Implementation Tests ===\n";

    // Test 1: Key derivation consistency
    {
        std::cout << "Test 1: Key derivation consistency... ";

        TlsSession sess1, sess2;
        uint8_t serverRandom[16] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                                   0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};
        uint8_t clientRandom[16] = {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                                   0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20};

        mockTlsDeriveKeys(sess1, serverRandom, clientRandom);
        mockTlsDeriveKeys(sess2, serverRandom, clientRandom);

        // Same inputs should produce same keys
        bool keysMatch = (memcmp(sess1.clientKey.data(), sess2.clientKey.data(), 32) == 0) &&
                        (memcmp(sess1.serverKey.data(), sess2.serverKey.data(), 32) == 0);

        assert(keysMatch && "Keys should be deterministic");
        assert(sess1.handshakeDone && "Handshake should be marked complete");
        std::cout << "✓ PASS\n";
    }

    // Test 2: Client and server keys should be different
    {
        std::cout << "Test 2: Client vs server keys differ... ";

        TlsSession sess;
        uint8_t serverRandom[16] = {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
                                   0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,0x10};
        uint8_t clientRandom[16] = {0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,
                                   0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f,0x20};

        mockTlsDeriveKeys(sess, serverRandom, clientRandom);

        // Keys should be different (XOR with different random orderings)
        bool keysDiffer = (memcmp(sess.clientKey.data(), sess.serverKey.data(), 32) != 0);
        assert(keysDiffer && "Client and server keys should differ");
        std::cout << "✓ PASS\n";
    }

    // Test 3: AES-256-CTR encryption round-trip
    {
        std::cout << "Test 3: AES-256-CTR encrypt/decrypt round-trip... ";

        uint8_t key[32];
        for (int i = 0; i < 32; i++) key[i] = i;

        protect::Aes256 cipher(key);

        const char* plaintext = "Hello, World! This is a test message.";
        size_t len = strlen(plaintext);

        uint8_t nonce[12] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b};

        // Encrypt
        std::vector<uint8_t> ciphertext(len);
        cipher.ctr(reinterpret_cast<const uint8_t*>(plaintext), ciphertext.data(), len, nonce);

        // Ciphertext should not equal plaintext (randomness through nonce)
        bool encrypted = (memcmp(ciphertext.data(), plaintext, len) != 0);
        assert(encrypted && "Encryption should change plaintext");

        // Decrypt (CTR mode is symmetric)
        std::vector<uint8_t> decrypted(len);
        cipher.ctr(ciphertext.data(), decrypted.data(), len, nonce);

        // Decrypted should match plaintext
        bool decryptionWorks = (memcmp(decrypted.data(), plaintext, len) == 0);
        assert(decryptionWorks && "Decryption should recover plaintext");

        std::cout << "✓ PASS\n";
    }

    // Test 4: Different nonces produce different ciphertexts
    {
        std::cout << "Test 4: Different nonces → different ciphertexts... ";

        uint8_t key[32];
        for (int i = 0; i < 32; i++) key[i] = 0x42;

        protect::Aes256 cipher(key);

        const char* plaintext = "Secret message";
        size_t len = strlen(plaintext);

        uint8_t nonce1[12] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x01};
        uint8_t nonce2[12] = {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x02};

        std::vector<uint8_t> ct1(len), ct2(len);
        cipher.ctr(reinterpret_cast<const uint8_t*>(plaintext), ct1.data(), len, nonce1);
        cipher.ctr(reinterpret_cast<const uint8_t*>(plaintext), ct2.data(), len, nonce2);

        bool differentNonces = (memcmp(ct1.data(), ct2.data(), len) != 0);
        assert(differentNonces && "Different nonces should produce different ciphertexts");
        std::cout << "✓ PASS\n";
    }

    // Test 5: Large message encryption
    {
        std::cout << "Test 5: Large message (16 KB)... ";

        uint8_t key[32];
        for (int i = 0; i < 32; i++) key[i] = (i * 7) & 0xFF;

        protect::Aes256 cipher(key);

        // Create 16 KB test message
        size_t len = 16 * 1024;
        std::vector<uint8_t> plaintext(len), ciphertext(len), decrypted(len);

        for (size_t i = 0; i < len; i++) plaintext[i] = (i % 256);

        uint8_t nonce[12] = {0};

        cipher.ctr(plaintext.data(), ciphertext.data(), len, nonce);
        cipher.ctr(ciphertext.data(), decrypted.data(), len, nonce);

        bool largeMessageWorks = (memcmp(plaintext.data(), decrypted.data(), len) == 0);
        assert(largeMessageWorks && "Large message encryption should work");
        std::cout << "✓ PASS\n";
    }

    // Test 6: Sequence number tracking
    {
        std::cout << "Test 6: Sequence number tracking... ";

        TlsSession sess;
        uint8_t dummyRandom[16] = {0};
        mockTlsDeriveKeys(sess, dummyRandom, dummyRandom);

        assert(sess.clientSeq == 0 && "Initial client sequence should be 0");
        assert(sess.serverSeq == 0 && "Initial server sequence should be 0");

        sess.clientSeq++;
        sess.serverSeq += 10;

        assert(sess.clientSeq == 1 && "Client sequence should increment");
        assert(sess.serverSeq == 10 && "Server sequence should track");
        std::cout << "✓ PASS\n";
    }

    // Test 7: HTTP record layer format
    {
        std::cout << "Test 7: TLS record layer format... ";

        // Simulate: [content_type:1][nonce:12][ciphertext:N]
        std::vector<uint8_t> record;
        record.push_back(0x17);  // application_data

        uint8_t nonce[12] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
                            0x08,0x09,0x0a,0x0b};
        record.insert(record.end(), nonce, nonce + 12);

        // Mock ciphertext
        std::vector<uint8_t> ct = {0xaa, 0xbb, 0xcc, 0xdd};
        record.insert(record.end(), ct.begin(), ct.end());

        assert(record.size() == 1 + 12 + 4 && "Record should have correct layout");
        assert(record[0] == 0x17 && "Content type should be application_data");

        // Extract nonce
        const uint8_t* extracted_nonce = record.data() + 1;
        bool nonceCorrect = (memcmp(extracted_nonce, nonce, 12) == 0);
        assert(nonceCorrect && "Nonce extraction should work");

        std::cout << "✓ PASS\n";
    }

    std::cout << "\n✓ All TLS tests passed!\n";
    std::cout << "\n⚠️  WARNING: These tests verify basic functionality, NOT cryptographic security.\n";
    std::cout << "     See TLS_SECURITY_ASSESSMENT.md for security issues.\n";
}

} // namespace symm

int main() {
    try {
        symm::runTlsTests();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Test failed: " << e.what() << std::endl;
        return 1;
    }
}
