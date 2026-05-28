#include "GpuServer.h"
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <iostream>

namespace symm {

// Global SSL context (initialized once at startup, shared across all connections)
static SSL_CTX* g_ssl_ctx = nullptr;

bool GpuServer::initOpenSSLContext() {
    // Initialize OpenSSL library
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();

    // Create SSL context using TLS 1.3 server method
    g_ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!g_ssl_ctx) {
        ERR_print_errors_fp(stderr);
        return false;
    }

    // For PSK-only mode (internal APIs):
    // We still use OpenSSL but without certificate files.
    // In production with certificates, load them like this:
    // if (SSL_CTX_use_certificate_file(g_ssl_ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) { ... }
    // if (SSL_CTX_use_PrivateKey_file(g_ssl_ctx, "server.key", SSL_FILETYPE_PEM) <= 0) { ... }
    // if (!SSL_CTX_check_private_key(g_ssl_ctx)) { ... }

    // Set minimum TLS version to 1.3
    if (!SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_3_VERSION)) {
        ERR_print_errors_fp(stderr);
        SSL_CTX_free(g_ssl_ctx);
        g_ssl_ctx = nullptr;
        return false;
    }

    // For TLS 1.3, cipher selection is automatic; we can optionally restrict to strong ciphers:
    // SSL_CTX_set_cipher_list(g_ssl_ctx, "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");

    // Enable session caching for performance (optional)
    SSL_CTX_set_session_cache_mode(g_ssl_ctx, SSL_SESS_CACHE_SERVER);

    return true;
}

void GpuServer::cleanupOpenSSLContext() {
    if (g_ssl_ctx) {
        SSL_CTX_free(g_ssl_ctx);
        g_ssl_ctx = nullptr;
    }
    ERR_free_strings();
    EVP_cleanup();
}

TlsSession GpuServer::createTlsSession() {
    TlsSession sess;
    if (!g_ssl_ctx) {
        sess.tlsEnabled = false;
        return sess;
    }

    // Create new SSL connection object
    SSL* ssl = SSL_new(g_ssl_ctx);
    if (!ssl) {
        ERR_print_errors_fp(stderr);
        sess.tlsEnabled = false;
        return sess;
    }

    sess.ssl = ssl;
    sess.ctx = g_ssl_ctx;
    sess.tlsEnabled = !_tlsPsk.empty();
    sess.handshakeDone = false;

    return sess;
}

bool GpuServer::handleTlsClientHello(int fd, TlsSession& sess, const std::string& clientIp) {
    if (!sess.tlsEnabled || !sess.ssl) return true;

    // Set socket file descriptor for the SSL connection
    if (!SSL_set_fd(sess.ssl, fd)) {
        ERR_print_errors_fp(stderr);
        return false;
    }

    // Set non-blocking mode for the SSL connection
    // (actual socket is already non-blocking if needed by server)

    // Perform TLS handshake
    int ret = SSL_accept(sess.ssl);
    if (ret <= 0) {
        int err = SSL_get_error(sess.ssl, ret);
        if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
            // Handshake in progress, will continue on next call
            return true;
        }
        // Handshake failed
        ERR_print_errors_fp(stderr);
        return false;
    }

    // Handshake completed successfully
    sess.handshakeDone = true;
    return true;
}

std::string GpuServer::tlsEncryptRecord(const TlsSession& sess, const uint8_t* plaintext, size_t len) {
    if (!sess.tlsEnabled || !sess.handshakeDone || !sess.ssl) return "";
    if (len > TLS_MAX_RECORD) return "";

    // OpenSSL's SSL_write handles encryption automatically
    int ret = SSL_write(sess.ssl, plaintext, static_cast<int>(len));
    if (ret <= 0) {
        int err = SSL_get_error(sess.ssl, ret);
        if (err != SSL_ERROR_WANT_WRITE) {
            ERR_print_errors_fp(stderr);
        }
        return "";
    }

    // Note: OpenSSL buffers the encrypted data internally.
    // For this simple version, we'd need to flush BIO buffers or use BIO pairs.
    // A complete implementation would use SSL_write with memory BIOs for non-socket I/O.

    // For now, return empty and let OpenSSL handle the network I/O directly.
    // In production, use BIO_s_mem() for memory-based encryption/decryption.

    return "";  // TODO: implement with memory BIOs
}

std::string GpuServer::tlsDecryptRecord(TlsSession& sess, const std::string& encRecord) {
    if (!sess.tlsEnabled || !sess.handshakeDone || !sess.ssl) return "";

    // OpenSSL's SSL_read handles decryption automatically
    char buf[TLS_MAX_RECORD];
    int ret = SSL_read(sess.ssl, buf, static_cast<int>(sizeof(buf)));

    if (ret <= 0) {
        int err = SSL_get_error(sess.ssl, ret);
        if (err != SSL_ERROR_WANT_READ) {
            ERR_print_errors_fp(stderr);
        }
        return "";
    }

    return std::string(buf, ret);
}

void GpuServer::tlsDeriveKeys(TlsSession& sess, const uint8_t* serverRandom, const uint8_t* clientRandom) {
    // With OpenSSL, key derivation is handled internally by the SSL/TLS layer.
    // This function is no longer needed for OpenSSL integration.
    // For PSK support, use SSL_CTX_set_psk_server_callback() or SSL_CTX_use_psk_identity_hint().
}

// Helper: Free TLS session resources
void GpuServer::freeTlsSession(TlsSession& sess) {
    if (sess.ssl) {
        SSL_free(sess.ssl);
        sess.ssl = nullptr;
    }
}

} // namespace symm
