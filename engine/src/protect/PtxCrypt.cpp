#include "PtxCrypt.h"
#include "Aes256.h"
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
  #include <windows.h>
  #define SECURE_ZERO(p,n) SecureZeroMemory(p, n)
#else
  #include <string.h>
  #define SECURE_ZERO(p,n) explicit_bzero(p, n)
#endif

namespace symm::protect {

static const uint8_t KEY[32] = { SYMM_PTX_KEY };
static const uint8_t IV[12]  = { SYMM_PTX_IV  };

std::string PtxCrypt::decrypt(const uint8_t* blob, size_t blobLen) {
    if (blobLen < 4) throw std::runtime_error("PtxCrypt: blob too small");

    // First 4 bytes: original length
    uint32_t origLen =
        ((uint32_t)blob[0] << 24) |
        ((uint32_t)blob[1] << 16) |
        ((uint32_t)blob[2] <<  8) |
         (uint32_t)blob[3];

    const uint8_t* cipher = blob + 4;
    size_t cipherLen = blobLen - 4;

    if (origLen > 64 * 1024 * 1024) // sanity: max 64 MB PTX
        throw std::runtime_error("PtxCrypt: suspicious origLen");

    std::string result(origLen, '\0');
    Aes256 aes(KEY);
    aes.ctr(cipher, reinterpret_cast<uint8_t*>(result.data()), cipherLen, IV);
    result.resize(origLen);
    return result;
}

std::vector<uint8_t> PtxCrypt::encrypt(const std::string& ptx) {
    size_t n = ptx.size();
    std::vector<uint8_t> out(4 + n);
    out[0] = (n >> 24) & 0xff;
    out[1] = (n >> 16) & 0xff;
    out[2] = (n >>  8) & 0xff;
    out[3] =  n        & 0xff;

    Aes256 aes(KEY);
    aes.ctr(reinterpret_cast<const uint8_t*>(ptx.data()),
            out.data() + 4, n, IV);
    return out;
}

void PtxCrypt::secureZero(void* buf, size_t n) {
    SECURE_ZERO(buf, n);
}

} // namespace symm::protect
