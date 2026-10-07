// Linked build stand-in for the generated harness-layout symbol header (include/ms/gen/mog_syms.hpp).
// Lifted code gets its addresses from MS_SYM/MS_HUNK (include/ms/linked.hpp), so the 6,000 constexpr
// addresses are not pulled into every translation unit (they bloat -O0 objects by ~30 MB).
#pragma once
