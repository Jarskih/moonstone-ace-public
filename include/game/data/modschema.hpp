// game/data/modschema - the generated schema tables (build/gen/mod_schema.cpp, from tools/mod_schema/*.yaml by
// tools/gen_moddata.py, ROADMAP 9.4b).  One SectionDesc per `[kind]` of the data files, grouped by file.
#pragma once
#include "game/data/modparse.hpp"

namespace ms {
namespace game {

struct ModFileDesc {
	const char *pName;    // "rules.ini"
	uint8_t ubFirst;      // first section in kModSections
	uint8_t ubCount;
};

extern const SectionDesc kModSections[];
extern const uint8_t kModSectionCount;
extern const ModFileDesc kModFiles[];
extern const uint8_t kModFileCount;

}  // namespace game
}  // namespace ms
