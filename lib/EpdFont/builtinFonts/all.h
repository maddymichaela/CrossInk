#pragma once

// Built-in reading fonts are fixed at 10, 12, 14, and 16 pt. The default
// variant includes emoji/symbol and PHM CJK fallbacks; noemoji includes only
// the primary fonts.
#ifdef OMIT_EMOJI_FONTS
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/noemoji/name.h>
#else
#define BUILTIN_READING_FONT_HEADER(name) <builtinFonts/name.h>
#endif

#include BUILTIN_READING_FONT_HEADER(bitter_10_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_10_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_10_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_12_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_12_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_12_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_14_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_14_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_14_regular)
#include BUILTIN_READING_FONT_HEADER(bitter_16_bold)
#include BUILTIN_READING_FONT_HEADER(bitter_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(bitter_16_italic)
#include BUILTIN_READING_FONT_HEADER(bitter_16_regular)

#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_10_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_12_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_14_regular)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_bold)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_bolditalic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_italic)
#include BUILTIN_READING_FONT_HEADER(lexenddeca_16_regular)

// AvesO3's compact phone-like reading option. Only 10pt is bundled to keep
// enough OTA partition headroom for CrossInk's newer subsystems.
#include <builtinFonts/notosans_10_bold.h>
#include <builtinFonts/notosans_10_bolditalic.h>
#include <builtinFonts/notosans_10_italic.h>
#include <builtinFonts/notosans_10_regular.h>

#undef BUILTIN_READING_FONT_HEADER

// UI fonts - no emoji or PHM variants.
#include <builtinFonts/inter_10_bold.h>
#include <builtinFonts/inter_10_regular.h>
#include <builtinFonts/inter_12_bold.h>
#include <builtinFonts/inter_12_regular.h>
#include <builtinFonts/inter_8_regular.h>
