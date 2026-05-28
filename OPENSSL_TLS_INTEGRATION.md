# OpenSSL TLS Integration Guide

**Status**: Implementation plan for replacing custom TLS with battle-tested OpenSSL  
**Priority**: HIGH - Required for production public APIs  
**Effort**: 1-2 days  
**Benefit**: Battle-tested crypto, certificate support, perfect forward secrecy capability

---

## Current State

**Custom TLS** (current):
- ❌ MVP-grade HMAC, GCM, SHA256 (not production)
- ❌ No certificate support (PSK-only)
- ❌ No perfect forward secrecy
- ⚠️ Needs ongoing security audits

**OpenSSL TLS** (proposed):
- ✅ Battle-tested, FIPS-certified
- ✅ Full TLS 1.3 with modern features
- ✅ Certificate and PSK support
- ✅ Perfect forward secrecy (DHE/ECDHE)
- ✅ Industry standard (Node.js, NGINX, etc.)

---

## Integration Plan

### Phase 1: Dependencies & Build Setup

**Add OpenSSL to CMakeLists.txt**:
```cmake
# engine/CMakeLists.txt
find_package(OpenSSL REQUIRED)
target_link_libraries(symmetra_engine OpenSSL::SSL OpenSSL::Crypto)
```

**Supported versions**:
- OpenSSL 3.0+ (recommended) - LTS
- OpenSSL 1.1.1 (legacy) - Still supported

### Phase 2: Replace TLS Headers

**File**: `engine/src/runtime/GpuServer.h`

```cpp
// REMOVE:
// struct TlsSession { ... custom fields ... }
// TlsSession createTlsSession();
// void tlsDeriveKeys(...)
// etc.

// ADD:
#include <openssl/ssl.h>
#include <openssl/err.h>

struct TlsSession {
    SSL* ssl = nullptr;              // OpenSSL SSL connection
    SSL_CTX* ctx = nullptr;          // Global SSL context
    bool handshakeDone = false;
    uint64_t clientSeq = 0;
    uint64_t serverSeq = 0;
};

// Initialization
bool initOpenSSLContext();
void cleanupOpenSSLContext();
```

### Phase 3: Implement OpenSSL Wrappers

**File**: `engine/src/runtime/TlsOpenSSL.cpp` (new)

```cpp
#include "GpuServer.h"
#include <openssl/ssl.h>

static SSL_CTX* g_ssl_ctx = nullptr;

bool GpuServer::initOpenSSLContext() {
    // Create SSL context
    g_ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!g_ssl_ctx) {
        ERR_print_errors_fp(stderr);
        return false;
    }

    // Load certificate and private key
    if (SSL_CTX_use_certificate_file(g_ssl_ctx, "server.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return false;
    }

    if (SSL_CTX_use_PrivateKey_file(g_ssl_ctx, "server.key", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        return false;
    }

    // Verify certificate and key match
    if (!SSL_CTX_check_private_key(g_ssl_ctx)) {
        std::cerr << "Certificate and private key do not match\n";
        return false;
    }

    // Set TLS 1.3 as minimum
    SSL_CTX_set_min_proto_version(g_ssl_ctx, TLS1_3_VERSION);

    // Set modern ciphers (TLS 1.3 auto-negotiates)
    SSL_CTX_set_cipher_list(g_ssl_ctx, 
        "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256");

    return true;
}

void GpuServer::cleanupOpenSSLContext() {
    if (g_ssl_ctx) {
        SSL_CTX_free(g_ssl_ctx);
        g_ssl_ctx = nullptr;
    }
}

TlsSession GpuServer::createTlsSession() {
    TlsSession sess;
    sess.ssl = SSL_new(g_ssl_ctx);
    sess.ctx = g_ssl_ctx;
    sess.handshakeDone = false;
    return sess;
}

bool GpuServer::handleTlsClientHello(int fd, TlsSession& sess) {
    if (!sess.ssl) return false;

    // Associate socket with SSL
    if (!SSL_set_fd(sess.ssl, fd)) {
        ERR_print_errors_fp(stderr);
        return false;
    }

    // Accept TLS connection
    int ret = SSL_accept(sess.ssl);
    if (ret <= 0) {
        int err = SSL_get_error(sess.ssl, ret);
        if (err != SSL_ERROR_WANT_READ && err != SSL_ERROR_WANT_WRITE) {
            ERR_print_errors_fp(stderr);
            return false;
        }
        return true;  // Handshake in progress
    }

    sess.handshakeDone = true;
    return true;
}

std::string GpuServer::tlsDecryptRecord(TlsSession& sess, const std::string& encRecord) {
    if (!sess.handshakeDone) return "";

    char buf[16384];
    int ret = SSL_read(sess.ssl, buf, sizeof(buf));
    
    if (ret <= 0) {
        int err = SSL_get_error(sess.ssl, ret);
        if (err != SSL_ERROR_WANT_READ) {
            ERR_print_errors_fp(stderr);
        }
        return "";
    }

    return std::string(buf, ret);
}

bool GpuServer::tlsEncryptRecord(TlsSession& sess, const std::string& plaintext) {
    if (!sess.handshakeDone) return false;

    int ret = SSL_write(sess.ssl, plaintext.data(), plaintext.size());
    if (ret <= 0) {
        int err = SSL_get_error(sess.ssl, ret);
        if (err != SSL_ERROR_WANT_WRITE) {
            ERR_print_errors_fp(stderr);
            return false;
        }
    }

    return true;
}
```

### Phase 4: Update Server Initialization

**File**: `engine/src/runtime/GpuServer.cpp`

```cpp
GpuServer::GpuServer(...) {
    // ... existing code ...

    // Initialize OpenSSL
    if (!initOpenSSLContext()) {
        throw std::runtime_error("Failed to initialize OpenSSL context");
    }

    // ... rest of init ...
}

GpuServer::~GpuServer() {
    // ... existing cleanup ...
    cleanupOpenSSL Context();
}
```

### Phase 5: Update Accept Loop

**File**: `engine/src/runtime/GpuServer.cpp` - `handleConn()` method

```cpp
void GpuServer::handleConn(int connFd, StreamPool::Slot slot) {
    TlsSession tlsSess = createTlsSession();

    if (_opts.tlsPsk.empty()) {
        // PSK mode - simplified for internal APIs
        // For production, load PSK from secure storage
        // ... existing PSK code ...
    } else {
        // Certificate mode - OpenSSL handles full TLS
        if (!handleTlsClientHello(connFd, tlsSess)) {
            ::close(connFd);
            return;
        }
    }

    // Process request with TLS encryption
    // ... existing code but use OpenSSL methods ...
}
```

---

## Configuration Options

### ServerOptions (updated)

```cpp
struct ServerOptions {
    std::string tlsCertFile = "";     // Path to server.crt
    std::string tlsKeyFile = "";      // Path to server.key
    std::string tlsCiphers = "";      // Custom cipher list (or "" for defaults)
    std::string tlsPsk = "";          // Legacy: PSK mode (deprecated in favor of certs)
    int tlsMinVersion = TLS1_3_VERSION;  // Minimum TLS version
};
```

### Usage Examples

**Certificate-based TLS**:
```cpp
ServerOptions opts;
opts.tlsCertFile = "/etc/certs/server.crt";
opts.tlsKeyFile = "/etc/certs/server.key";
// OpenSSL automatically negotiates TLS 1.3 with PFS
```

**Legacy PSK mode** (internal only):
```cpp
ServerOptions opts;
opts.tlsPsk = "0123456789ABCDEF...";  // Deprecated - use certs instead
```

---

## Certificate Generation

### Self-Signed (Development)
```bash
# Generate private key
openssl genrsa -out server.key 2048

# Generate self-signed cert (365 days)
openssl req -new -x509 -key server.key -out server.crt -days 365 \
  -subj "/C=US/ST=State/L=City/O=Org/CN=localhost"
```

### Let's Encrypt (Production)
```bash
# Use certbot
certbot certonly --standalone -d yourdomain.com
# Copies to /etc/letsencrypt/live/yourdomain.com/
```

### Self-Signed SAN Certificate
```bash
openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout server.key -out server.crt \
  -subj "/CN=localhost" \
  -addext "subjectAltName=DNS:localhost,DNS:*.example.com"
```

---

## Security Considerations

### OpenSSL Configuration

**Hardening**:
```cpp
// Disable old/weak TLS versions
SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);

// Use only strong ciphers
SSL_CTX_set_cipher_list(ctx, 
    "TLS_AES_256_GCM_SHA384:"
    "TLS_CHACHA20_POLY1305_SHA256");

// Enable OCSP stapling (optional)
SSL_CTX_set_tlsext_status_cb(ctx, ...);

// Enable session resumption
SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_SERVER);
```

### Certificate Validation

**For Client Certs** (mTLS):
```cpp
SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
SSL_CTX_load_verify_locations(ctx, "ca.crt", nullptr);
```

### Key File Protection

```bash
# Ensure key file is readable only by server process
chmod 600 server.key

# Use file permissions, not passwords
openssl rsa -in server.key.enc -out server.key
```

---

## Performance Implications

### OpenSSL vs Custom TLS

| Aspect | Custom | OpenSSL |
|--------|--------|---------|
| Handshake | ~5ms | ~10-20ms |
| Encryption overhead | ~2-3% | ~2-3% |
| Code complexity | High | Low (battle-tested) |
| Security audit | Needed | Certified |
| Certificate support | No | Yes |
| PFS capability | No | Yes (ECDHE) |

**Verdict**: OpenSSL is marginally slower on handshake but worth it for security/features.

---

## Testing

### Unit Tests
```cpp
// test_openssl_tls.cpp
TEST("OpenSSL: TLS 1.3 handshake") {
    // Connect, verify handshake succeeds
    // Verify TLS version negotiated correctly
}

TEST("OpenSSL: Data encryption/decryption") {
    // Send data through TLS session
    // Verify encrypted on wire, decrypted on recv
}

TEST("OpenSSL: Certificate validation") {
    // Test with valid cert
    // Test with expired cert
    // Test with self-signed
}
```

### Integration Tests
```bash
# Test with real OpenSSL client
openssl s_client -connect localhost:8080 -tls1_3

# Test with curl
curl --insecure https://localhost:8080/ping

# Test with ab (ApacheBench)
ab -n 1000 -c 32 https://localhost:8080/
```

---

## Deployment Checklist

- [ ] Add OpenSSL to CMakeLists.txt
- [ ] Create OpenSSL wrapper classes
- [ ] Update ServerOptions struct
- [ ] Implement certificate loading/validation
- [ ] Add init/cleanup methods
- [ ] Update accept loop to use OpenSSL
- [ ] Test with self-signed certificates
- [ ] Test with Let's Encrypt certificates
- [ ] Benchmark TLS performance
- [ ] Add security hardening flags
- [ ] Document certificate rotation
- [ ] Update installation guide
- [ ] Add troubleshooting guide
- [ ] Deprecate custom TLS implementation

---

## Timeline

**Day 1**: Core OpenSSL integration (4-6 hours)
- CMake setup
- Header updates
- Basic SSL context initialization
- Certificate loading

**Day 2**: Full implementation & testing (4-6 hours)
- Connection handling
- Encryption/decryption
- Error handling
- Unit + integration tests
- Performance benchmarking

**Day 3** (optional): Hardening & documentation
- Security audit of config
- OCSP stapling
- mTLS support
- Comprehensive docs

---

## Rollback Plan

If OpenSSL integration causes issues:

1. Keep custom TLS in parallel
2. Feature-flag the switch: `USE_OPENSSL=1`
3. Default to custom TLS until fully validated
4. Switch default after 2 weeks of production testing
5. Remove custom TLS after 6 months of OpenSSL stability

---

## References

- [OpenSSL Documentation](https://www.openssl.org/docs/)
- [OpenSSL Examples](https://github.com/openssl/openssl/tree/master/demos)
- [TLS 1.3 RFC 8446](https://tools.ietf.org/html/rfc8446)
- [Let's Encrypt](https://letsencrypt.org/)
- [Mozilla SSL Configuration Generator](https://ssl-config.mozilla.org/)

---

*Created: May 24, 2026*  
*Status: Ready for implementation*  
*Next: Begin Phase 1 - CMake & build setup*
