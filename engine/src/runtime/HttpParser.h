#pragma once
#include <string>
#include <cstring>
#include <unordered_map>

namespace symm {

// ============================================================================
// Zero-Copy HTTP Parser
// ============================================================================
// Parses HTTP headers without copying - returns views into original buffer
// Extracts only what GPU needs: method, path, headers, body pointer
// Reduces memory pressure and CPU cycles on hot path

struct HttpHeadersView {
    // String views into the original buffer (no copy)
    const char* method;      // "GET", "POST", etc.
    size_t methodLen;

    const char* path;        // URL path
    size_t pathLen;

    const char* query;       // Query string (after ?)
    size_t queryLen;

    const char* body;        // Request body
    size_t bodyLen;

    // Common headers (pointers into buffer, no copy)
    const char* contentType;
    size_t contentTypeLen;

    const char* contentLength;
    size_t contentLengthLen;

    const char* userAgent;
    size_t userAgentLen;

    const char* authorization;   // "Bearer <token>", "Basic ...", etc.
    size_t authorizationLen;

    int httpVersion;         // 10 (HTTP/1.0), 11 (HTTP/1.1)
};

class ZeroCopyHttpParser {
public:
    // Parse HTTP request without copying headers
    // Returns success/failure and fills HttpHeadersView with pointers into buffer
    // buffer: raw HTTP bytes received from socket
    // bufferLen: number of bytes in buffer
    // view: output parameter, filled with parsed header views
    // Returns: number of bytes consumed (0 if incomplete, -1 if invalid)
    static int parseHttpHeaders(const char* buffer, size_t bufferLen, HttpHeadersView& view) {
        if (!buffer || bufferLen == 0) {
            return -1;
        }

        std::memset(&view, 0, sizeof(view));

        // Find request line end (CRLF)
        const char* lineEnd = findCrlf(buffer, bufferLen, 0);
        if (!lineEnd) {
            return 0;  // Incomplete
        }

        size_t lineLen = lineEnd - buffer;

        // Parse request line: METHOD PATH VERSION
        const char* space1 = std::strchr(buffer, ' ');
        if (!space1 || space1 >= lineEnd) {
            return -1;  // Invalid
        }

        view.method = buffer;
        view.methodLen = space1 - buffer;

        const char* space2 = std::strchr(space1 + 1, ' ');
        if (!space2 || space2 >= lineEnd) {
            return -1;  // Invalid
        }

        // Parse path with query string
        view.path = space1 + 1;
        size_t pathWithQueryLen = space2 - view.path;

        // Split path and query
        const char* queryStart = static_cast<const char*>(std::memchr(view.path, '?', pathWithQueryLen));
        if (queryStart) {
            view.pathLen = queryStart - view.path;
            view.query = queryStart + 1;
            view.queryLen = pathWithQueryLen - view.pathLen - 1;
        } else {
            view.pathLen = pathWithQueryLen;
            view.query = nullptr;
            view.queryLen = 0;
        }

        // Parse HTTP version
        const char* versionStr = space2 + 1;
        if (std::strncmp(versionStr, "HTTP/1.1", 8) == 0) {
            view.httpVersion = 11;
        } else if (std::strncmp(versionStr, "HTTP/1.0", 8) == 0) {
            view.httpVersion = 10;
        } else {
            view.httpVersion = 11;  // Default
        }

        // Parse headers
        size_t contentLength = 0;
        size_t headerStart = lineLen + 2;  // Skip first CRLF

        while (headerStart < bufferLen) {
            const char* headerLineEnd = findCrlf(buffer, bufferLen, headerStart);
            if (!headerLineEnd) {
                return 0;  // Incomplete headers
            }

            size_t headerLineLen = headerLineEnd - (buffer + headerStart);

            // Empty line = end of headers
            if (headerLineLen == 0) {
                size_t bodyStart = headerStart + headerLineEnd - (buffer + headerStart) + 2;
                view.body = buffer + bodyStart;
                view.bodyLen = bufferLen - bodyStart;
                return static_cast<int>(bodyStart + contentLength);
            }

            // Parse header: NAME: VALUE
            const char* headerLine = buffer + headerStart;
            const char* colon = static_cast<const char*>(std::memchr(headerLine, ':', headerLineLen));

            if (colon) {
                std::string headerName(headerLine, colon - headerLine);

                // Normalize to lowercase for comparison
                for (char& c : headerName) {
                    c = std::tolower(static_cast<unsigned char>(c));
                }

                const char* valueStart = colon + 1;
                while (valueStart < headerLineEnd && *valueStart == ' ') {
                    valueStart++;
                }

                size_t valueLen = headerLineEnd - valueStart;

                // Store common headers
                if (headerName == "content-type") {
                    view.contentType = valueStart;
                    view.contentTypeLen = valueLen;
                } else if (headerName == "content-length") {
                    view.contentLength = valueStart;
                    view.contentLengthLen = valueLen;
                    contentLength = parseContentLength(valueStart, valueLen);
                } else if (headerName == "user-agent") {
                    view.userAgent = valueStart;
                    view.userAgentLen = valueLen;
                } else if (headerName == "authorization") {
                    view.authorization = valueStart;
                    view.authorizationLen = valueLen;
                }
            }

            headerStart += headerLineLen + 2;  // Skip line + CRLF
        }

        return 0;  // Incomplete
    }

    // Validate method is allowed
    static bool isValidMethod(const HttpHeadersView& view) {
        const char* validMethods[] = {"GET", "POST", "PUT", "DELETE", "PATCH", "HEAD", "OPTIONS"};
        for (const char* method : validMethods) {
            size_t methodLen = std::strlen(method);
            if (view.methodLen == methodLen &&
                std::memcmp(view.method, method, methodLen) == 0) {
                return true;
            }
        }
        return false;
    }

    // Validate path length
    static bool isValidPath(const HttpHeadersView& view) {
        return view.pathLen > 0 && view.pathLen <= 4096;
    }

    // Validate body length
    static bool isValidBodyLength(const HttpHeadersView& view) {
        return view.bodyLen <= (64 * 1024);  // 64 KB max
    }

private:
    static const char* findCrlf(const char* buffer, size_t bufferLen, size_t startIdx) {
        for (size_t i = startIdx; i + 1 < bufferLen; i++) {
            if (buffer[i] == '\r' && buffer[i + 1] == '\n') {
                return buffer + i;
            }
        }
        return nullptr;
    }

    static size_t parseContentLength(const char* str, size_t len) {
        size_t result = 0;
        for (size_t i = 0; i < len && std::isdigit(static_cast<unsigned char>(str[i])); i++) {
            result = result * 10 + (str[i] - '0');
            if (result > 64 * 1024) {
                return 64 * 1024;  // Cap at max
            }
        }
        return result;
    }
};

}  // namespace symm
