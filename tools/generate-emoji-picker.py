#!/usr/bin/env python3
"""Generate the offline chat picker from pinned category/order facts.

The committed Discord snapshot contains only Unicode/category facts and canonical
names already licensed through shortcodes.txt. It contains no Discord artwork,
client code, or additional Discord aliases. Unicode data is downloaded only by
this developer tool, hash checked, and cached outside the shipped asset folder.
"""

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
import unicodedata
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "amnesia/fonts/twemoji"
SNAPSHOT = Path(__file__).with_name("emoji-picker-discord-order.json")
SNAPSHOT_SHA256 = "2b9da1896d0fb437bba2348c29c104850105b7f1e95a0e6f6451c5d332c61471"
CATEGORIES = ("people", "nature", "food", "activity", "travel", "objects", "symbols", "flags")
UNICODE_URL = "https://unicode.org/Public/17.0.0/emoji/emoji-test.txt"
UNICODE_SHA256 = "1d8a944f88d7952f7ef7c5167fef3c67995bcae24543949710231b03a201acda"
LICENSE_URL = "https://www.unicode.org/license.txt"
LICENSE_SHA256 = "e7a93b009565cfce55919a381437ac4db883e9da2126fa28b91d12732bc53d96"
MAPPING_SHA256 = "079cff1ae341f56b7bee99aff233e8197879ab1e8591489af59cfaa6951e86df"
SHORTCODES_SHA256 = "45b35997cf83ecdf6f00bbe12a1f206e562347ead83e5bd11cb3dca7c15b5440"
TONE_DATA_COMMIT = "13ee711e222ea17fe537bfea953c687866f16411"
TONE_DATA_URL = f"https://raw.githubusercontent.com/iamcal/emoji-data/{TONE_DATA_COMMIT}/emoji.json"
TONE_DATA_SHA256 = "8736d721991e48c41c57acc46b65e5d32258a45e4bcdebcc6012f420f81e775d"
TONE_LICENSE_URL = f"https://raw.githubusercontent.com/iamcal/emoji-data/{TONE_DATA_COMMIT}/LICENSE"
TONE_LICENSE_SHA256 = "ee9953a79bf2132b59b1342b217f1c377b3d03d9e7713006f6c3b89eb159f1db"
GROUP_CATEGORIES = {
    "Smileys & Emotion": "people", "People & Body": "people", "Component": "people",
    "Animals & Nature": "nature", "Food & Drink": "food", "Activities": "activity",
    "Travel & Places": "travel", "Objects": "objects", "Symbols": "symbols", "Flags": "flags",
}
TONE_NAMES = {
    0x1F3FB: "light_skin_tone", 0x1F3FC: "medium_light_skin_tone",
    0x1F3FD: "medium_skin_tone", 0x1F3FE: "medium_dark_skin_tone", 0x1F3FF: "dark_skin_tone",
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def normalized(sequence):
    return tuple(point for point in sequence if point != 0xFE0F)


def points(sequence):
    return tuple(int(point, 16) for point in sequence.split("-"))


def checked_bytes(path, expected, normalize_newlines=False):
    data = path.read_bytes()
    actual = digest(data.replace(b"\r\n", b"\n") if normalize_newlines else data)
    if actual != expected:
        raise ValueError(f"Pinned input hash mismatch for {path}: expected {expected}, got {actual}")
    return data


def cached_source(directory, filename, url, expected, offline):
    path = directory / filename
    if not path.exists():
        if offline:
            raise ValueError(f"Missing pinned source {path}; run once without --offline to fetch it")
        directory.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(url, timeout=30) as response:
            data = response.read()
        if digest(data) != expected:
            raise ValueError(f"Downloaded source changed at {url}; review and explicitly repin before using it")
        path.write_bytes(data)
    return checked_bytes(path, expected)


def canonical_description(description):
    ascii_description = unicodedata.normalize("NFKD", description).encode("ascii", "ignore").decode("ascii")
    return re.sub(r"[^a-z0-9]+", "_", ascii_description.lower()).strip("_")


def extension_record(sequence):
    """Explicitly classify the 30 Twemoji assets outside the Emoji 17 RGI list."""
    without_tone = tuple(point for point in normalized(sequence) if point not in TONE_NAMES)
    tone = next((TONE_NAMES[point] for point in sequence if point in TONE_NAMES), "")
    if without_tone in ((0x1F468, 0x200D, 0x1F384), (0x1F469, 0x200D, 0x1F384)):
        category = "people"
        name = "man_santa" if without_tone[0] == 0x1F468 else "woman_santa"
    elif without_tone in ((0x1F574, 0x200D, 0x2640), (0x1F574, 0x200D, 0x2642)):
        category = "activity"
        name = "woman_levitating" if without_tone[-1] == 0x2640 else "man_levitating"
    elif without_tone == (0x26F7,) and tone:
        category, name = "activity", "skier"
    elif without_tone == (0xE50A,):
        # The upstream PNG depicts the Shibuya 109 department-store building.
        return "travel", "shibuya_109"
    else:
        raise ValueError(f"Unclassified upstream extension: {sequence}")
    return category, name + ("_" + tone if tone else "")


def tone_families(atlas, ordered_targets, source_data):
    """Use explicit source family links; never invent a sequence by insertion."""
    reverse = {}
    families = {}
    source_variants = set()

    def register(base, variant, tone):
        if base not in atlas or variant not in atlas:
            raise ValueError("Tone family references an unavailable atlas glyph")
        if any(point in TONE_NAMES for point in base):
            raise ValueError("Tone family base must be an unmodified default emoji")
        actual_tones = {point - 0x1F3FA for point in variant if point in TONE_NAMES}
        if (tone and actual_tones != {tone}) or (not tone and len(actual_tones) < 2):
            raise ValueError("Uniform/mixed skin-tone slot disagrees with the actual Unicode glyph")
        if variant in reverse:
            raise ValueError("Duplicate or ambiguous reverse skin-tone family membership")
        reverse[variant] = (base, tone)
        family = families.setdefault(base, {})
        if tone:
            if tone in family:
                raise ValueError("Duplicate uniform skin-tone slot in a family")
            family[tone] = variant

    for entry in json.loads(source_data):
        base = normalized(points(entry["unified"]))
        if base not in atlas:
            continue
        for key, record in entry.get("skin_variations", {}).items():
            variant = normalized(points(record["unified"]))
            if variant not in atlas:
                continue
            modifier_points = points(key)
            if any(point not in TONE_NAMES for point in modifier_points):
                raise ValueError("Unknown skin variation key in pinned emoji-data source")
            distinct = set(modifier_points)
            tone = modifier_points[0] - 0x1F3FA if len(distinct) == 1 else 0
            register(base, variant, tone)
            source_variants.add(variant)

    # Five Twemoji extension families have existing real base and variant tiles
    # but are intentionally outside the official Emoji 17 RGI family data.
    extension_bases = {
        (0x1F468, 0x200D, 0x1F384), (0x1F469, 0x200D, 0x1F384),
        (0x1F574, 0x200D, 0x2640), (0x1F574, 0x200D, 0x2642), (0x26F7,),
    }
    extension_variants = set()
    for variant in atlas:
        actual_tones = [point for point in variant if point in TONE_NAMES]
        if not actual_tones or variant in reverse:
            continue
        base = tuple(point for point in variant if point not in TONE_NAMES)
        if base in extension_bases:
            if len(actual_tones) != 1:
                raise ValueError("Unexpected multi-tone upstream extension glyph")
            register(base, variant, actual_tones[0] - 0x1F3FA)
            extension_variants.add(variant)

    for base, family in families.items():
        if set(family) != {1, 2, 3, 4, 5}:
            raise ValueError(f"Incomplete five-tone family: {atlas[base]}")
    toned_targets = {target for target in atlas if any(point in TONE_NAMES for point in target)}
    standalone_modifiers = {(point,) for point in TONE_NAMES}
    if set(reverse) != toned_targets - standalone_modifiers:
        raise ValueError("Tone families must cover all skin-tone glyphs except the five standalone modifier components")
    if len(families) != 335 or len(source_variants) != 2030 or len(extension_variants) != 25:
        raise ValueError("Pinned tone family counts changed unexpectedly")
    rows = []
    for base in ordered_targets:
        if base not in families:
            continue
        family = families[base]
        rows.extend((atlas[base], tone, atlas[family[tone]]) for tone in range(1, 6))
        mixed = sorted(atlas[variant] for variant, membership in reverse.items() if membership == (base, 0))
        rows.extend((atlas[base], 0, variant) for variant in mixed)
    if len(rows) != len(reverse):
        raise ValueError("Tone output omitted or duplicated a family relationship")
    header = (
        "# Offline skin-tone families; generated by tools/generate-emoji-picker.py.\n"
        "# Format: exact base atlas sequence TAB tone integer TAB exact variant atlas sequence.\n"
        "# Tones1..5 select light through dark; tone0 marks mixed reverse membership only.\n"
        "# Default/yellow is always the base glyph, never a tone0 variant row.\n"
        "# Source: MIT iamcal/emoji-data skin_variations at " + TONE_DATA_COMMIT + ".\n"
        "# Source JSON SHA-256: " + TONE_DATA_SHA256 + ".\n"
        "# Includes25 actual Twemoji extension variants in five explicit families.\n"
    )
    output = header + "".join(f"{base}\t{tone}\t{variant}\n" for base, tone, variant in rows)
    return output.encode("utf-8"), {
        "format": "UTF-8 TSV with # comment header; base atlas sequence TAB tone integer TAB variant atlas sequence. Tones 1..5 select uniform light through dark; tone 0 records mixed-variant reverse family membership only. Default yellow is the base glyph itself.",
        "source": "iamcal/emoji-data skin_variations", "source_commit": TONE_DATA_COMMIT,
        "source_url": TONE_DATA_URL, "source_sha256": TONE_DATA_SHA256,
        "license": "MIT", "license_url": TONE_LICENSE_URL, "license_sha256": TONE_LICENSE_SHA256,
        "attribution": "Copyright (c) 2013 Cal Henderson",
        "family_count": len(families), "variant_count": len(reverse),
        "uniform_variant_count": sum(bool(tone) for _, tone in reverse.values()),
        "mixed_variant_count": sum(not tone for _, tone in reverse.values()),
        "source_variant_count": len(source_variants), "upstream_extension_variant_count": len(extension_variants),
        "standalone_modifier_components": 5, "picker_tones_txt_sha256": digest(output.encode("utf-8")),
        "validation": "All 335 families have five real uniform atlas variants; all 2055 modified emoji glyphs have one unambiguous reverse family; five standalone modifier components are preserved separately.",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ROOT / "bld/emoji-picker-source")
    parser.add_argument("--offline", action="store_true", help="Use only previously cached pinned Unicode sources")
    parser.add_argument("--check", action="store_true", help="Validate checked-in outputs without changing them")
    args = parser.parse_args()
    atlas_rows = checked_bytes(ASSETS / "mapping.txt", MAPPING_SHA256, normalize_newlines=True).decode("utf-8").splitlines()[1:]
    atlas = {normalized(points(row.split("\t")[0])): row.split("\t")[0] for row in atlas_rows}
    if len(atlas) != len(atlas_rows) or len(atlas) != 4009:
        raise ValueError("Atlas mapping must contain exactly 4009 distinct normalized Unicode targets")
    aliases_by_glyph = {}
    alias_targets = {}
    for row in checked_bytes(ASSETS / "shortcodes.txt", SHORTCODES_SHA256, normalize_newlines=True).decode("utf-8").splitlines():
        alias, sequence = row.split("\t")
        target = normalized(points(sequence))
        alias_targets[alias] = target
        aliases_by_glyph.setdefault(target, []).append(alias)
    unicode_data = cached_source(args.source_dir, "emoji-test-17.0.txt", UNICODE_URL, UNICODE_SHA256, args.offline)
    # The published license URL is updated periodically. Prefer the checked-in
    # pinned copy so a future copyright-year update cannot break regeneration.
    packaged_license = ASSETS / "LICENSE-UNICODE"
    license_data = (checked_bytes(packaged_license, LICENSE_SHA256) if packaged_license.exists()
                    else cached_source(args.source_dir, "Unicode-LICENSE", LICENSE_URL, LICENSE_SHA256, args.offline))
    tone_source_data = cached_source(args.source_dir, "emoji-data.json", TONE_DATA_URL, TONE_DATA_SHA256, args.offline)
    packaged_tone_license = ASSETS / "LICENSE-EMOJI-DATA"
    tone_license_data = (checked_bytes(packaged_tone_license, TONE_LICENSE_SHA256) if packaged_tone_license.exists()
                         else cached_source(args.source_dir, "EmojiData-LICENSE", TONE_LICENSE_URL, TONE_LICENSE_SHA256, args.offline))
    unicode_records = {}
    group = None
    for row in unicode_data.decode("utf-8-sig").splitlines():
        if row.startswith("# group: "):
            group = row[len("# group: "):]
        match = re.match(r"^([0-9A-F ]+)\s*;\s*(fully-qualified|component)\s*#\s*\S+\s+E[0-9.]+\s+(.+)", row)
        if match:
            target = normalized(tuple(int(point, 16) for point in match[1].split()))
            if target in unicode_records:
                raise ValueError(f"Duplicate fully-qualified Unicode record: {target}")
            unicode_records[target] = (GROUP_CATEGORIES[group], canonical_description(match[3]))
    snapshot_data = checked_bytes(SNAPSHOT, SNAPSHOT_SHA256)
    snapshot = json.loads(snapshot_data)
    if tuple(snapshot["categories"]) != CATEGORIES:
        raise ValueError("Discord category list does not match the pinned display order")
    records = {category: [] for category in CATEGORIES}
    used = set()
    assigned_categories = {}
    # Discord can place activities inside a different category from Unicode's
    # broad People & Body group. New tone variants inherit the existing family
    # base category (notably all 75 Emoji 17 wrestling variants).
    tone_family_bases = {
        normalized(points(variant["unified"])): normalized(points(entry["unified"]))
        for entry in json.loads(tone_source_data)
        for variant in entry.get("skin_variations", {}).values()
    }
    discord_counts = Counter()
    for category, name, sequence in snapshot["records"]:
        target = normalized(points(sequence))
        if category not in records or target not in atlas or target in used:
            raise ValueError(f"Invalid or duplicate pinned Discord target: {category}, {sequence}")
        if name:
            if alias_targets.get(name) != target:
                raise ValueError(f"Canonical Discord name is not an existing licensed alias: {name}")
        else:
            name = unicode_records[target][1]
        records[category].append((name, atlas[target]))
        used.add(target)
        assigned_categories[target] = category
        discord_counts[category] += 1
    unicode_counts = Counter()
    for target, (category, description) in unicode_records.items():
        if target not in atlas or target in used:
            continue
        category = assigned_categories.get(tone_family_bases.get(target), category)
        # Existing MIT aliases remain preferred whenever the glyph already has one.
        names = aliases_by_glyph.get(target, [])
        name = next((name for name in names if ":" not in name), description)
        records[category].append((name, atlas[target]))
        used.add(target)
        assigned_categories[target] = category
        unicode_counts[category] += 1
    extension_counts = Counter()
    for target, sequence in atlas.items():
        if target in used:
            continue
        category, name = extension_record(points(sequence))
        records[category].append((name, sequence))
        used.add(target)
        assigned_categories[target] = category
        extension_counts[category] += 1
    if used != set(atlas):
        raise ValueError("Picker metadata does not cover the complete atlas")
    header = (
        "# Offline emoji picker metadata; generated by tools/generate-emoji-picker.py.\n"
        "# Format: category TAB canonical display/search name TAB exact atlas sequence.\n"
        "# Categories: " + ", ".join(CATEGORIES) + ".\n"
        "# Discord category/order facts: Emzi0767 Canary snapshot " + snapshot["source_snapshot"] + ".\n"
        "# Discord source bundle: " + snapshot["source_bundle"] + ".\n"
        "# Discord source JSON SHA-256: " + snapshot["source_json_sha256"] + ".\n"
        "# Unicode 17 CLDR fallback: " + UNICODE_URL + ".\n"
        "# Unicode source SHA-256: " + UNICODE_SHA256 + ".\n"
        "# Existing names: MIT DSharpPlus; Unicode fallback: LICENSE-UNICODE.\n"
        "# Fallback names are display/search labels; unregistered shortcodes insert literal Unicode.\n"
    )
    output = header + "".join(f"{category}\t{name}\t{sequence}\n" for category in CATEGORIES for name, sequence in records[category])
    output_data = output.encode("utf-8")
    ordered_targets = [normalized(points(sequence)) for category in CATEGORIES for _, sequence in records[category]]
    tone_output_data, tone_metadata = tone_families(atlas, ordered_targets, tone_source_data)
    for row in tone_output_data.decode("utf-8").splitlines():
        if row.startswith("#"):
            continue
        base, _, variant = row.split("\t")
        if assigned_categories[normalized(points(base))] != assigned_categories[normalized(points(variant))]:
            raise ValueError("A tone variant's category disagrees with its family base")
    counts = {category: len(records[category]) for category in CATEGORIES}
    metadata_path = ASSETS / "metadata.json"
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    picker_metadata = {
        "format": "UTF-8 TSV with # comment header; category TAB canonical display/search name TAB exact atlas hexadecimal sequence.",
        "input_text_hash_normalization": "Existing mapping.txt and shortcodes.txt inputs are checked after CRLF-to-LF normalization so either platform's Git text checkout regenerates identical metadata.",
        "mapping_lf_sha256": MAPPING_SHA256, "shortcodes_lf_sha256": SHORTCODES_SHA256,
        "categories": list(CATEGORIES), "entry_count": len(used), "category_counts": counts,
        "order": "Discord Canary definition-array order within category, deduplicated by atlas glyph; missing Emoji 17 entries appended in Unicode CLDR order using their known family base's category when available; 30 upstream extension assets appended deterministically.",
        "name_sources": "Existing MIT-licensed DSharpPlus shortcode names preferred; Unicode 17 descriptions or explicit labels for upstream extensions otherwise. Fallback labels do not add registered shortcodes.",
        "source_url": snapshot["source_url"], "source_snapshot": snapshot["source_snapshot"],
        "source_bundle": snapshot["source_bundle"], "source_json_sha256": snapshot["source_json_sha256"],
        "source_definition_count": snapshot["source_definition_count"],
        "source_unique_atlas_targets": sum(discord_counts.values()),
        "reduced_snapshot": "tools/emoji-picker-discord-order.json", "reduced_snapshot_sha256": digest(snapshot_data),
        "unicode_source": UNICODE_URL, "unicode_source_sha256": UNICODE_SHA256,
        "unicode_fallback_entry_count": sum(unicode_counts.values()),
        "upstream_extension_entry_count": sum(extension_counts.values()),
        "extension_notes": "12 gendered Santa sequences, 12 gendered levitation sequences, 5 skier skin tones, and private-use U+E50A Shibuya 109 building.",
        "unicode_data_license": "Unicode-3.0", "unicode_license_url": LICENSE_URL,
        "unicode_license_sha256": digest(license_data), "picker_txt_sha256": digest(output_data),
        "generator": "tools/generate-emoji-picker.py",
        "runtime_network_requests": False,
        "skin_tone_families": tone_metadata,
    }
    if args.check:
        if (ASSETS / "picker.txt").read_bytes() != output_data:
            raise ValueError("picker.txt differs from the reproducible pinned output")
        if (ASSETS / "LICENSE-UNICODE").read_bytes() != license_data:
            raise ValueError("LICENSE-UNICODE differs from the pinned license")
        if (ASSETS / "LICENSE-EMOJI-DATA").read_bytes() != tone_license_data:
            raise ValueError("LICENSE-EMOJI-DATA differs from the pinned license")
        if (ASSETS / "picker-tones.txt").read_bytes() != tone_output_data:
            raise ValueError("picker-tones.txt differs from the reproducible pinned output")
        if metadata.get("picker") != picker_metadata:
            raise ValueError("metadata.json picker provenance/counts/hashes differ from the pinned output")
    else:
        (ASSETS / "picker.txt").write_bytes(output_data)
        (ASSETS / "LICENSE-UNICODE").write_bytes(license_data)
        (ASSETS / "LICENSE-EMOJI-DATA").write_bytes(tone_license_data)
        (ASSETS / "picker-tones.txt").write_bytes(tone_output_data)
        metadata["picker"] = picker_metadata
        metadata_path.write_text(json.dumps(metadata, indent=2, ensure_ascii=True) + "\n", encoding="utf-8")
    print(json.dumps({"entries": len(used), "categories": counts,
                      "discord_entries": sum(discord_counts.values()),
                      "unicode_fallback_entries": sum(unicode_counts.values()),
                      "extension_entries": sum(extension_counts.values()),
                      "picker_sha256": digest(output_data), "tone_families": tone_metadata}, indent=2))
    print("PASS: all 4009 atlas glyphs categorized once; 335 complete tone families and 2055 unique variant relationships; source hashes, existing aliases, Unicode selectors and deterministic outputs verified")


if __name__ == "__main__":
    main()
