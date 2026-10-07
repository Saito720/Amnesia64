# Twemoji artwork for multiplayer chat

Twemoji graphics by Twitter, Inc. and other contributors; maintained by Jason Sofonia,
Justine De Caires, and the Twemoji community.

This package contains all 4,009 PNG emoji graphics from the pinned
[Twemoji 17.0.3 release](https://github.com/jdecked/twemoji/releases/tag/v17.0.3),
which supports Unicode Emoji 17.0. Artwork is licensed under
[Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/).
The full license is included in `LICENSE-GRAPHICS`. These graphics retain their
CC-BY-4.0 license independently of the game's source-code license.

The original 72x72 artwork was resized to 32x32 with Pillow's LANCZOS filter and
packed into `atlas.png`. Each tile has a two-pixel transparent gutter on all sides
to prevent neighboring emoji bleeding during texture filtering. The resulting
RGBA atlas is 4096x2048 pixels. The unused bottom 752 rows are transparent padding
to keep texture dimensions compatible with the engine. No online requests are
needed at runtime.

`mapping.txt` starts with the atlas width and height separated by a space. Every
remaining line contains five tab-separated fields: upstream emoji sequence,
pixel x, pixel y, tile width, and tile height. The sequence consists of lowercase
hexadecimal Unicode codepoints separated by hyphens, matching upstream PNG
filenames. Rows are sorted by that ASCII sequence. Coordinates refer to the
32x32 artwork inside its gutters, with the origin in the image's top-left corner.
Variation selector U+FE0F appears in upstream multi-codepoint filenames; the
renderer may ignore that optional emoji selector when matching sequences.

`metadata.json` records the pinned source archive, its SHA-256 hash, processing
details, asset count, atlas dimensions, and hashes of the generated files.
Only artwork is included; no upstream JavaScript parser or other library code
is incorporated.

## Discord-style shortcodes

`shortcodes.txt` contains 7,861 aliases for 3,474 distinct emoji targets. Aliases
are derived from the MIT-licensed generated `UnicodeEmojis` dictionary in
[DSharpPlus at revision 16c04c5518b7d3d9f00f6b0b633a938bd23b92e0](https://github.com/DSharpPlus/DSharpPlus/blob/16c04c5518b7d3d9f00f6b0b633a938bd23b92e0/DSharpPlus/Entities/Emoji/DiscordEmoji.EmojiUtils.cs).
Copyright (c) 2015 Mike Santiago; copyright (c) 2016-2025 DSharpPlus Development
Team. The complete MIT license is included in `LICENSE-DSHARPPLUS`.

That source credits the Discord Emoji Map generator by Emzi0767 and identifies
its source snapshot as April 7, 2025 (3,799 definitions). The user-referenced
[canary JSON endpoint](https://emzi0767.mzgit.io/discord-emoji/discordEmojiMap-canary.min.json)
was also checked: the September 30, 2026 snapshot has 3,807 definitions. All
shared aliases agree after optional U+FE0F normalization. The distributed aliases
come from the pinned MIT source; the full live JSON is not redistributed.

Rows are UTF-8, with no header: alias without its outer colons, a tab, then the
original source Unicode codepoints in lowercase hexadecimal separated by
hyphens. Examples include `smile`, `heart`, `thumbsup`, `+1`, `thumbsup_tone3`,
`flag_us`, and `woman_technologist`. Compound keys retain internal colons, such
as `adult::skin-tone-1`. Source Unicode targets and presentation selectors are
preserved. The source also includes `piñata`; the ASCII spelling `pinata` is
added as a synonym. No custom Discord server emoji or Discord artwork is bundled.

## Picker categories and order

`picker.txt` contains one entry for each of the 4,009 atlas tiles. It has a `#`
comment header followed by three tab-separated fields: category, canonical
display/search name, and the exact hexadecimal sequence from `mapping.txt`.
Names without a registered shortcode are labels for search and hover details;
choosing those entries inserts the literal Unicode emoji.

The picker uses Discord's eight category groups in this order: People, Nature,
Food, Activity, Travel, Objects, Symbols, and Flags. Category membership and the
order of the first 3,482 distinct glyphs come from the factual definition array
in the October 1, 2026 Emzi0767 Canary snapshot, generated from Discord's
`vnd-emoji.ca4ac71ece8afd45.js` bundle. Duplicate definitions that resolve to the
same atlas glyph are collapsed. The full source JSON, its artwork links, client
code, and new alias names are not bundled. A reduced, pinned category/Unicode
snapshot is retained in `tools/emoji-picker-discord-order.json`; names in that
snapshot are only existing MIT-licensed aliases from `shortcodes.txt`.

Another 497 atlas glyphs are appended to their categories using the group and
CLDR order in the official [Unicode Emoji 17.0 test data](https://unicode.org/Public/17.0.0/emoji/emoji-test.txt).
New tone variants inherit their known family's category, keeping wrestling
variants in Discord's Activity category instead of Unicode's broader People
and Body group.
Unicode descriptions provide readable labels when existing shortcode names are
unavailable. Copyright © 1991-2026 Unicode, Inc. This data is used under the
Unicode License v3, included as `LICENSE-UNICODE`.

The remaining 30 upstream Twemoji graphics are outside the Emoji 17 recommended
interchange list: 12 gendered Santa sequences (People), 12 gendered levitation
sequences and five skier skin tones (Activity), and the private-use U+E50A
Shibuya 109 building (Travel). These entries have explicit labels and deterministic
positions at the end of their categories. Skin-tone variants retain their
categories and remain searchable; the category view shows one variant per
family using the selected global skin tone.
The atlas itself and the registered shortcode file are unchanged.

`picker-tones.txt` records explicit relationships between default emoji and
their existing atlas variants. Each non-comment row contains three fields:
base atlas sequence, numeric tone, and variant atlas sequence. Tones 1 through 5
select light, medium light, medium, medium dark, and dark skin; tone 0 marks a
mixed-tone variant's family membership for reverse lookup only. The default
yellow choice always resolves to the base glyph itself. No Unicode sequence is
constructed by inserting modifiers into an arbitrary emoji.

The family relationships come from `skin_variations` in
[iamcal/emoji-data at revision 13ee711e222ea17fe537bfea953c687866f16411](https://github.com/iamcal/emoji-data/blob/13ee711e222ea17fe537bfea953c687866f16411/emoji.json).
Copyright (c) 2013 Cal Henderson; the MIT license is included as
`LICENSE-EMOJI-DATA`. Only the family data is used, with no additional artwork.
This covers 2,030 variants, including legacy couple bases and their alternate
joined representations, plus mixed-tone handshakes. Twenty-five existing
Twemoji extension variants complete another five explicit families. All 335
families have five actual uniform-tone tiles; 380 mixed-tone tiles also retain
an unambiguous family. The five standalone tone-modifier components remain
separate glyphs.

To regenerate the picker, run `python tools/generate-emoji-picker.py` from the
repository root. The script verifies the pinned atlas, shortcode, snapshot,
Unicode, family, and license inputs before writing output. It downloads the
pinned Unicode and emoji-data sources into `bld/emoji-picker-source` when they
are not already cached;
this developer step does not make game runtime requests. Once cached,
`python tools/generate-emoji-picker.py --offline --check` validates all 4,009
unique targets, category coverage, all five choices in every family, 2,055 unique
variant links, reproducible bytes, provenance, and hashes without rewriting the
asset files. Both MSBuild's `ChatEmojiAsset` wildcard and CMake's directory
deployment copy the picker metadata and licenses with the existing chat assets.
Scoped `.gitattributes` rules preserve generated metadata, snapshot, and license
files as LF text so their byte hashes survive checkouts. The existing atlas
mapping and shortcode inputs are verified with CRLF-to-LF normalization, making
regeneration identical on Windows and Unix without changing either source file.
