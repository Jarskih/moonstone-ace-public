// rt/sdl/audio_settings - the audio options the player keeps in <user directory>/audio.txt (PC only).
//
// A plain key=value text file, one setting per line, '#' starts a comment, keys and values are case-insensitive:
//   render=blep            blep | box      (band-limited steps, the default, or the exact box filter)
//   separation=70          0..100          (stereo separation; 100 = Amiga hard panning, 0 = mono)
//   filter=a1200           off | a500 | a500led | a1200 | a1200led   (the analogue output stage, engine/outfilter)
// A missing file, a missing key or a value that does not parse leaves that setting at its default; the rest of the file still applies.  The
// command line (--audio-render / --audio-separation / --audio-filter) overrides the file for that run; --audio-save writes the settings in
// force (file + command line) back, so a choice made once on the command line is remembered.  Nothing else writes the file: a plain launch
// and every headless test run leave the user directory alone.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string_view>

#include "rt/sdl/audio.hpp"

namespace rt {
namespace sdl {

constexpr uint8_t kAudioSetSeparation = 1;      // bits of the "set on the command line" mask
constexpr uint8_t kAudioSetFilter = 2;
constexpr uint8_t kAudioSetRender = 4;

// Applies the text of an audio.txt to sOptions for the settings whose bit in ubKeep is NOT set (those came from the command line).
// Returns how many lines it applied.
[[nodiscard]] uint32_t audioSettingsParse(std::string_view sText, AudioOptions &sOptions, uint8_t ubKeep);

// The text audioSettingsParse reads back to the same options (ASCII, LF line ends).
[[nodiscard]] size_t audioSettingsFormat(const AudioOptions &sOptions, char *pOut, size_t nOut);

// Name of a filter / renderer as written in the file and in the log; nullptr for a value out of range.
const char *audioFilterName(uint8_t ubFilter);
const char *audioRenderName(uint8_t ubRender);

// Parses a name of the above (the command line uses the same words); false when it is none of them.
[[nodiscard]] bool audioFilterFromName(std::string_view sName, uint8_t &ubFilter);
[[nodiscard]] bool audioRenderFromName(std::string_view sName, uint8_t &ubRender);

// <dir>/audio.txt: reads it into sOptions (see audioSettingsParse; a missing file is fine) and, with isSave, writes the options in force back.
// Returns false only when isSave could not write the file.
bool audioSettingsLoadSave(const char *szPrefDir, AudioOptions &sOptions, uint8_t ubSetOnCommandLine, bool isSave);

}  // namespace sdl
}  // namespace rt
