file(REMOVE_RECURSE
  "CMakeFiles/gen_encrypted_kernels"
  "generated/genesis.ptx"
  "generated/genesis_enc.h"
  "generated/http.ptx"
  "generated/http_enc.h"
  "generated/io.ptx"
  "generated/io_enc.h"
  "generated/text.ptx"
  "generated/text_enc.h"
)

# Per-language clean rules from dependency scanning.
foreach(lang )
  include(CMakeFiles/gen_encrypted_kernels.dir/cmake_clean_${lang}.cmake OPTIONAL)
endforeach()
