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
come from the pinned MIT source; the live JSON is not redistributed.

Rows are UTF-8, with no header: alias without its outer colons, a tab, then the
original source Unicode codepoints in lowercase hexadecimal separated by
hyphens. Examples include `smile`, `heart`, `thumbsup`, `+1`, `thumbsup_tone3`,
`flag_us`, and `woman_technologist`. Compound keys retain internal colons, such
as `adult::skin-tone-1`. Source Unicode targets and presentation selectors are
preserved. The source also includes `piñata`; the ASCII spelling `pinata` is
added as a synonym. No custom Discord server emoji or Discord artwork is bundled.
