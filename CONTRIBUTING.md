# Contributing

Thanks for helping! Translations are the easiest way to contribute — no C++ needed.

## Adding or improving a translation

1. Copy [`lang/template.lang.txt`](lang/template.lang.txt) to `lang/<code>.lang`, e.g. `lang/pl.lang`
   (`<code>` is a short language code: `pl`, `uk`, `pt-br`, …). Encoding: **UTF-8**.
2. Set the header:
   ```
   language.name = Polski        # shown in the language list, in the language itself
   language.code = pl            # must be unique
   ```
3. Translate the values (right of `=`). Rules:
   - keep placeholders like `{w}`, `{h}`, `{n}`, `{error}`, `{name}`, `{version}`, `{folder}` exactly
     as they are (you may move them inside the sentence);
   - `\n` is a line break;
   - lines starting with `#` are comments;
   - a string you delete falls back to English (texture flag names such as *Clamp S* are usually
     left in English, like in Valve's tools).
4. Check the file:
   ```sh
   python tools/check_lang.py
   ```
   It reports unknown keys (typos), changed placeholders and duplicate language codes.
   The same check runs automatically on every pull request.
5. Try it without building: put the file into the `VTFExport_lang` folder next to `VTFExport.8bi`
   and open the save dialog — the language appears in the list (the dialog re-reads the folder
   every time). Make sure the texts fit in the dialog.
6. Open a pull request with just your `.lang` file.

To **fix a few strings** of an existing language, edit `lang/<code>.lang` the same way.

### When new strings are added to the plug-in

New strings are added in English to `src/Lang.cpp` and to `lang/template.lang.txt`.
`check_lang.py` lists the untranslated strings of every language, so translators can catch up.
Until then the English text is shown.

## Code changes

- Build: see *Building from source* in [README.md](README.md) (needs the Photoshop SDK).
- Run the tests before a pull request:
  ```sh
  pip install srctools pillow
  tests/run_tests.sh            # core tests
  tests/run_tests.sh --wine     # + plug-in tests under Wine (MinGW + Wine)
  ```
- `src/VtfWriter.*`, `src/VtfReader.*` and `src/Resample.*` don't depend on Windows or the SDK —
  keep it that way, so they stay testable on any platform.
- New user-visible text: add the key with its English text to `kStrings` in `src/Lang.cpp`,
  regenerate the template (`tests/lang_test.cpp --template`, or add the line by hand) and add the
  Russian text to `lang/ru.lang` if you can.
- Please describe what you tested in Photoshop: the fake hosts check the plug-in against our
  understanding of the SDK, real Photoshop is the final word.

## Releases (maintainers)

1. Bump the version in `src/Version.h`, add a section to `CHANGELOG.md`.
2. Build **Release | x64**.
3. `powershell -ExecutionPolicy Bypass -File tools\make_release.ps1` → `dist\VTFExport-<version>.zip`.
4. Create a GitHub release with the tag `v<version>`, paste the changelog section, attach the zip.
