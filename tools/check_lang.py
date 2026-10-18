"""Checks language files against the English strings in src/Lang.cpp.

Usage: python3 tools/check_lang.py [files or folders ...]   (default: lang/)

Errors (exit code 1):
  - missing language.name / language.code, or a code used by two files
  - unknown keys (typos, or keys removed from the plug-in)
  - placeholders ({w}, {error}, ...) that differ from the English text
Warnings: duplicated keys. Info: untranslated keys (they fall back to English).
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def english_strings():
    src = (ROOT / "src" / "Lang.cpp").read_text(encoding="utf-8")
    table = src[src.index("const Builtin kStrings[] = {"):src.index("};", src.index("const Builtin kStrings[] = {"))]
    out = {}
    for key, literals in re.findall(r'\{"([a-z0-9_.]+)",\s*((?:L"(?:[^"\\]|\\.)*"\s*)+)\}', table):
        text = "".join(re.findall(r'L"((?:[^"\\]|\\.)*)"', literals))
        out[key] = text
    if not out:
        sys.exit("cannot parse src/Lang.cpp")
    return out


def placeholders(text):
    return sorted(set(re.findall(r"\{([a-z]+)\}", text)))


def parse(path):
    entries, dups = {}, []
    text = path.read_text(encoding="utf-8-sig")
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line[0] in "#;" or "=" not in line:
            continue
        key, value = (s.strip() for s in line.split("=", 1))
        if key in entries:
            dups.append((n, key))
        entries[key] = value
    return entries, dups


def main():
    en = english_strings()
    targets = [Path(a) for a in sys.argv[1:]] or [ROOT / "lang"]
    files = []
    for t in targets:
        files += sorted(t.glob("*.lang")) if t.is_dir() else [t]

    errors = 0
    codes = {}
    for f in files:
        try:
            entries, dups = parse(f)
        except UnicodeDecodeError:
            print(f"ERROR {f.name}: not UTF-8")
            errors += 1
            continue
        name, code = entries.pop("language.name", ""), entries.pop("language.code", "").lower()
        problems = []
        if not name:
            problems.append("language.name is missing")
        if not code:
            problems.append("language.code is missing")
        elif code in codes:
            problems.append(f"language.code '{code}' is also used by {codes[code]}")
        else:
            codes[code] = f.name
        for key, value in entries.items():
            if key not in en:
                problems.append(f"unknown key '{key}'")
            elif placeholders(value) != placeholders(en[key]):
                problems.append(f"'{key}': placeholders {placeholders(value)} != English {placeholders(en[key])}")
        for line, key in dups:
            print(f"WARN  {f.name}:{line}: '{key}' defined twice (the last one wins)")
        missing = [k for k in en if k not in entries and not k.startswith("flag.")]
        status = "ERROR" if problems else "OK   "
        print(f"{status} {f.name}: {name or '?'} ({code or '?'}), {len(entries)}/{len(en)} strings"
              + (f", untranslated: {', '.join(missing)}" if missing else ""))
        for p in problems:
            print(f"      - {p}")
        errors += bool(problems)

    print(f"\n{len(files)} file(s), {errors} with errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
