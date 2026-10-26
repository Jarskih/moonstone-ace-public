// rt/sdl/audio_settings - see audio_settings.hpp.
#include "rt/sdl/audio_settings.hpp"

#include <charconv>
#include <stdio.h>
#include <string.h>

#include "rt/platform/log.hpp"

namespace rt {
namespace sdl {

namespace {

constexpr const char *kFilterNames[] = {"off", "a500", "a500led", "a1200", "a1200led"};
constexpr const char *kRenderNames[] = {"box", "blep"};

char lower(char c) {
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool sameName(std::string_view sA, std::string_view sB) {
	if(sA.size() != sB.size()) {
		return false;
	}
	for(size_t i = 0; i < sA.size(); ++i) {
		if(lower(sA[i]) != lower(sB[i])) {
			return false;
		}
	}
	return true;
}

std::string_view trim(std::string_view s) {
	while(!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r')) {
		s.remove_prefix(1);
	}
	while(!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
		s.remove_suffix(1);
	}
	return s;
}

}  // namespace

const char *audioFilterName(uint8_t ubFilter) {
	return ubFilter < sizeof(kFilterNames) / sizeof(kFilterNames[0]) ? kFilterNames[ubFilter] : nullptr;
}

const char *audioRenderName(uint8_t ubRender) {
	return ubRender < sizeof(kRenderNames) / sizeof(kRenderNames[0]) ? kRenderNames[ubRender] : nullptr;
}

bool audioFilterFromName(std::string_view sName, uint8_t &ubFilter) {
	for(uint8_t i = 0; i < sizeof(kFilterNames) / sizeof(kFilterNames[0]); ++i) {
		if(sameName(sName, kFilterNames[i])) {
			ubFilter = i;
			return true;
		}
	}
	return false;
}

bool audioRenderFromName(std::string_view sName, uint8_t &ubRender) {
	for(uint8_t i = 0; i < sizeof(kRenderNames) / sizeof(kRenderNames[0]); ++i) {
		if(sameName(sName, kRenderNames[i])) {
			ubRender = i;
			return true;
		}
	}
	return false;
}

uint32_t audioSettingsParse(std::string_view sText, AudioOptions &sOptions, uint8_t ubKeep) {
	uint32_t ulApplied = 0;
	while(!sText.empty()) {
		const size_t nEnd = sText.find('\n');
		std::string_view sLine = trim(sText.substr(0, nEnd));
		sText = nEnd == std::string_view::npos ? std::string_view() : sText.substr(nEnd + 1);
		const size_t nEq = sLine.find('=');
		if(sLine.empty() || sLine.front() == '#' || nEq == std::string_view::npos) {
			continue;
		}
		const std::string_view sKey = trim(sLine.substr(0, nEq));
		const std::string_view sValue = trim(sLine.substr(nEq + 1));
		if(sameName(sKey, "separation") && (ubKeep & kAudioSetSeparation) == 0) {
			unsigned uSep = 0;
			const auto sResult = std::from_chars(sValue.data(), sValue.data() + sValue.size(), uSep);
			if(sResult.ec == std::errc() && sResult.ptr == sValue.data() + sValue.size() && uSep <= 100) {
				sOptions.ubSeparation = static_cast<uint8_t>(uSep);
				++ulApplied;
			}
		}
		else if(sameName(sKey, "filter") && (ubKeep & kAudioSetFilter) == 0) {
			ulApplied += audioFilterFromName(sValue, sOptions.ubFilter) ? 1u : 0u;
		}
		else if(sameName(sKey, "render") && (ubKeep & kAudioSetRender) == 0) {
			ulApplied += audioRenderFromName(sValue, sOptions.ubRender) ? 1u : 0u;
		}
	}
	return ulApplied;
}

size_t audioSettingsFormat(const AudioOptions &sOptions, char *pOut, size_t nOut) {
	const char *szFilter = audioFilterName(sOptions.ubFilter);
	const char *szRender = audioRenderName(sOptions.ubRender);
	const int lLen = snprintf(pOut, nOut,
		"# Moonstone audio settings.  Command-line options (--audio-render, --audio-separation, --audio-filter) override this file.\n"
		"# render:     blep (band-limited steps, no aliasing) or box (the exact box filter)\n"
		"# separation: 0..100, 100 = Amiga hard panning (left: channels 0 and 3, right: 1 and 2), 0 = mono; 70 is natural on headphones\n"
		"# filter:     off, a500, a500led, a1200 or a1200led (the analogue output stage of the machine)\n"
		"render=%s\nseparation=%u\nfilter=%s\n",
		szRender != nullptr ? szRender : "blep", static_cast<unsigned>(sOptions.ubSeparation > 100 ? 100 : sOptions.ubSeparation),
		szFilter != nullptr ? szFilter : "a1200");
	return lLen > 0 ? static_cast<size_t>(lLen) : 0;
}

bool audioSettingsLoadSave(const char *szPrefDir, AudioOptions &sOptions, uint8_t ubSetOnCommandLine, bool isSave) {
	char szPath[1024];
	snprintf(szPath, sizeof(szPath), "%saudio.txt", szPrefDir != nullptr ? szPrefDir : "");
	char acText[4096];
	size_t nRead = 0;
	if(FILE *pFile = fopen(szPath, "rb")) {
		nRead = fread(acText, 1, sizeof(acText) - 1, pFile);
		fclose(pFile);
	}
	(void)audioSettingsParse(std::string_view(acText, nRead), sOptions, ubSetOnCommandLine);
	if(!isSave) {
		return true;
	}
	const size_t nOut = audioSettingsFormat(sOptions, acText, sizeof(acText));
	FILE *pFile = fopen(szPath, "wb");
	const bool isOk = pFile != nullptr && fwrite(acText, 1, nOut, pFile) == nOut;
	if(pFile != nullptr) {
		fclose(pFile);
	}
	platform::logFormat(isOk ? "audio: settings saved to %s\n" : "audio: cannot write %s\n", szPath);
	return isOk;
}

}  // namespace sdl
}  // namespace rt
