#pragma once
#include <string>

namespace symm {

// Invokes esbuild as a subprocess to strip TypeScript types from a .ts file,
// returning the resulting plain JavaScript source. This is the only step that
// touches the file system after startup — esbuild runs once, then exits.
class EsbuildBridge {
public:
    // Transform a TypeScript file → plain JavaScript string.
    // Returns empty string on failure. Check stderr for details.
    std::string transform(const std::string& tsFilePath);

    // Find the esbuild binary (bundled or PATH).
    static std::string findEsbuild();
};

} // namespace symm
