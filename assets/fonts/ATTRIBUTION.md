# Bundled UI font assets

Portsmouth Rapids is the repository owner's own typeface. GUI.Forms bundles
two Portsmouth Rapids 1.0 faces and uses them for all control and title roles:

- `PortsmouthRapids.ttf` — regular, SHA-256
  `88988bea222852c30e08a3629d9f929baacfec4844c85bb978dcc68e6f4d4add`
- `PortsmouthRapids-Bold.ttf` — bold, SHA-256
  `f97d702778f5933b4ae138064a348499a6dba90d9e2e6bf75a6ab81a6730c03a`

Licence: MIT, copyright the repository owner; the exact text is retained as
`LICENSE-PortsmouthRapids.txt`. Embedded metadata identifies the family as
`Portsmouth Rapids`, version 1.0. The current faces were supplied 2026-08-06
with the interface-symbol rebuild. Full source provenance is retained in the
owner's Portsmouth source project (`portsmouth/PROJECT.md`).

The 2026-08-06 faces add the protected `← ↑ → ▼` navigation/disclosure set
requested by GUI.Forms while retaining the previous 330 cmap entries, glyph
IDs, advances, and outlines exactly. The editable construction and acceptance
reports live in the cited Portsmouth source project.

## The rest of the Portsmouth 1.0 family

On 2026-10-13 the owner asked for every Portsmouth face to be included and
confirmed that the MIT grant in `LICENSE-PortsmouthRapids.txt` covers them
all. They come from the owner's published 1.0 downloads
(`Portsmouth-Family-1.0.zip`, `Portsmouth-Rapids-Family-1.0.zip` and
`Portsmouth-Mono-1.0.zip` in `falseywinchnet/paymenottowork` at `3dbc684`,
`public/instruments/portsmouth/assets/downloads/`), built on 2026-08-06 in the
same run as the two faces above, which are byte-identical to that release.
Every face embeds version 1.0 and the same 349-entry cmap.

| File | Family / style | SHA-256 |
|---|---|---|
| `PortsmouthRapids-Light.ttf` | Portsmouth Rapids Light | `1afe07a8d89be75a5a9fb5befca4938009422e6719a0d6d7b117d95d8345e61a` |
| `PortsmouthRapids-Italic.ttf` | Portsmouth Rapids Italic | `cd806b808e80df27b16694295ad06066c7479c6f0699e22b3c1e3cf04ec9e375` |
| `PortsmouthRapids-BoldItalic.ttf` | Portsmouth Rapids Bold Italic | `00ab54b1cc0c46c0706bb51653d8a96c3c5a56e23c2030826b0918ff78a387d1` |
| `Portsmouth.ttf` | Portsmouth Regular | `c3dcc717d26c7f42c83d87550a1b2f4b4c3b51d0d776d780bdd883018ead0f81` |
| `Portsmouth-Light.ttf` | Portsmouth Light | `8e78f093987c736dc32376dd9b112408855b31701ac878bf21650c1d72aa63dc` |
| `Portsmouth-Italic.ttf` | Portsmouth Italic | `0781e11d005c75b6942653b07ad65c2e05d50ab901ee2919b5038cb176a17682` |
| `Portsmouth-Bold.ttf` | Portsmouth Bold | `e709c37667842e2aae50af7e6a000ed4eabee378fc543c35140e49af176d6800` |
| `Portsmouth-BoldItalic.ttf` | Portsmouth Bold Italic | `7cdfaea4929b298ed01e40ac64e34c440436026195939b7897ec3828726c3ea9` |
| `PortsmouthMono.ttf` | Portsmouth Mono Regular (fixed pitch, 575 units) | `6129c819de5046e4ec97c7ee8d915bf195d106b98680d9b17901741a1d7314ad` |

The release's `Portsmouth-Regular-1.0.ttf` (an earlier build under the
separate family name "Portsmouth Regular") and its `.ttc` collections are not
copied; the individual faces above carry the same styles.

These faces are installed with the SDK's font directory. The hosts register
only the two faces above for `FontRole::control`; the others are available to
applications and to font substitution (Portsmouth Mono is the planned
substitute for Consolas, [`planning/stages/F-font-substitution/`](https://github.com/malkuth-software/notes/tree/gui_forms/planning/stages/F-font-substitution)).

The font-pack specimen also contains Carlito and Cousine at upstream commit
and byte identity:

- Carlito commit `3a810cab78ebd6e2e4eed42af9e8453c4f9b850a`; regular
  `f6418f708baede9789daef5d458c0f53d2a888af9820e8062934e504fedc6595`, bold
  `bb5d20f79b82599ec72983597437373a80f2d2085fa91fc144fd74e876a594db`, italic
  `0b019225e58d702bfedcbd35c21696769f8ee115cb6343f84c2f240312450d1c`, and
  bold italic `b32928186c119599e03ca6a1ffc680fdcb7fac95772f4b95d989cf6cd3861517`.
- Cousine commit `c0fbdb438443968c884a5c13f5f9bee916a7f89b`; regular
  `5a57f0184000371cb22fe3fcea4c500354cab1a69efaf7beaf3e6eca6ecfefea`, bold
  `331215ec6445f41e98d8971251bb6237e722907f4101faae990583accfe79545`, italic
  `afb869eee8643d09915c3286edb61dad66278ceb1511c79b474ce21548421bcd`, and
  bold italic `7f804549c941ceaac0a9635389ed09c4bbc86e4bfe37589191dacd1b5b0e26a5`.

Both families are licensed under SIL OFL 1.1; exact license copies are retained
as `OFL-Carlito.txt` and `OFL-Cousine.txt`. Carlito is the declared lead body
**CANDIDATE** from the consumer typography plan, and Cousine is a bounded
monospace specimen. Their use here measures the bundled-font pipeline; it does
not silently elevate either candidate into the final M9 typography decision.

Two bounded, role-independent fallback faces complete the mixed-script
Gallery specimen:

- Noto Sans CJK JP Regular from `notofonts/noto-cjk` commit
  `f8d157532fbfaeda587e826d4cd5b21a49186f7c`, file
  `Sans/OTF/Japanese/NotoSansCJKjp-Regular.otf`, SHA-256
  `68a3fc98800b2a27b371f2fb79991daf3633bd89309d4ffaa6946fd587f375b5`.
- Noto Emoji Regular 1.05 monochrome from `googlefonts/noto-emoji` commit
  `9a5261d871451f9b5183c93483cbd68ed916b1e9`, file
  `fonts/NotoEmoji-Regular.ttf`, SHA-256
  `415dc6290378574135b64c808dc640c1df7531973290c4970c51fdeb849cb0c5`.

Both are licensed under SIL OFL 1.1. Exact upstream license copies are retained
as `OFL-NotoSansCJKjp.txt` and `OFL-NotoEmoji.txt`, each SHA-256
`6a73f9541c2de74158c0e7cf6b0a58ef774f5a780bf191f2d7ec9cc53efe2bf2`.
The files are reproducibly acquired and verified by
`third_party/fetch_showcase_fallback_fonts.sh`. Since 2026-11-16 the CJK file
is the source of the subsets below rather than a bundled face. They are **DECIDED** only for
the Gallery font pack and deterministic cross-script fallback proof;
File Manager's final locale/font-pack policy remains an M9 decision.

**REJECTED for this CPU renderer:** current COLRv1 Noto Emoji shaped correctly
but painted no pixels in live Skia/FreeType dogfood. The older upstream
monochrome face is deliberately bounded to fallback proof until GUI.Drawing
admits and measures a color-font raster policy.

Noto Sans Hebrew is the unmodified Noto Project file under SIL OFL 1.1. See OFL-Noto.txt and language-font-provenance.json for its source URL and byte hash.

## The slim fallback subsets (2026-11-16)

On 2026-11-16 the owner asked for the fallback faces to be stripped down: a
proper CJK face for Japanese and for Korean, and a very minimal fallback for
Chinese, Arabic and the Indic scripts. Hebrew stays as it is. The Noto Sans CJK
JP file above and the upstream Noto Sans Arabic, Devanagari, Bengali and
Gurmukhi files are no longer bundled whole. `tools/fallback_font_subsets.py`
cuts these subsets from them with fontTools 4.66.1. It checks each source
against its SHA-256 first, and its output is byte-for-byte reproducible:

| File | Cut from | Characters |
|---|---|---|
| `NotoSansCJKjp-Subset.otf` | Noto Sans CJK JP | kana, JIS X 0208 (non-kanji, levels 1 and 2), Japanese punctuation and width forms; Japanese forms |
| `NotoSansCJKkr-Subset.otf` | Noto Sans CJK JP | all 11,172 Hangul syllables, the jamo, KS X 1001 with its hanja; Korean forms |
| `NotoSansCJKsc-Minimal.otf` | Noto Sans CJK JP | the GB 2312 level 1 hanzi the two faces above lack; Simplified Chinese forms |
| `NotoSansArabic-Minimal.ttf` | Noto Sans Arabic | basic letters (with the Persian additions), vowel marks, digits and punctuation |
| `NotoSansDevanagari-Minimal.ttf`, `NotoSansBengali-Minimal.ttf`, `NotoSansGurmukhi-Minimal.ttf` | the matching Noto Sans file | the script's main block |

The Noto Sans CJK JP file is Noto's language-specific OTF: it holds every
region's glyphs and reaches the Korean and Chinese forms through its `locl`
feature. The script writes the forms of the face's language into its
character map, so the Korean and Chinese faces draw the regional glyphs with
no shaping. The minimal faces keep no presentation forms and no layout tables:
the text engine draws Arabic and the Indic scripts unshaped (owner, 2026-11-16).
The CJK faces keep no layout tables either.

Each subset is a Modified Version under SIL OFL 1.1. Neither Noto licence text
(`OFL-NotoSansCJKjp.txt`, `OFL-Noto.txt`) declares a Reserved Font Name. The
subsets keep their Noto family names. The Korean and Chinese faces are named
"Noto Sans CJK KR" and "Noto Sans CJK SC" after the forms they draw. Each
keeps the copyright, licence and trademark ("Noto is a trademark of Google
Inc.") name records. Source URLs, digests and each subset's SHA-256 are in
`language-font-provenance.json`.

## Font substitution faces (stage F)

The owner approved these downloads on 2026-10-23 for stage F's substitution
table ([`planning/stages/F-font-substitution/`](https://github.com/malkuth-software/notes/tree/gui_forms/planning/stages/F-font-substitution)). Each file was downloaded into
its own empty directory and checked against the upstream size and git blob
SHA-1 before use. Its SHA-256 is recorded below. The faces are byte-identical
to upstream; only the Selawik files are renamed. The F1 metrics differential
against the Windows reference faces is in
[`results/2026-10-23-f1-metrics-harness`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-23-f1-metrics-harness).

**Arimo 1.33** is metric-compatible with Arial, and is the substitute for
Arial, Helvetica and Microsoft Sans Serif.
- Source: `googlefonts/arimo` commit
  `4a6255f269916ae7ad3fc2706b0935e7621396b8`, files `fonts/ttf/`.
- Licence: SIL OFL 1.1, retained as `OFL-Arimo.txt` (upstream `OFL.txt`,
  SHA-256 `c46fda6859efb80c0a3c1faa868908a3d2fcb26eb7fd9602d73bebeec21bca3a`).

| File | Bytes | Git blob SHA-1 | SHA-256 |
|---|---|---|---|
| `Arimo-Regular.ttf` | 478712 | `d6b78c111a336f8c42e4cf5b6546c7b3386146cd` | `41b22bc8f0b51f932825d37bc55b5eb6ba67dfe599a626e4aff2b43b624f9f8c` |
| `Arimo-Bold.ttf` | 485872 | `6453fed1dd86ed03fd2a5b4060d26e01ad4f57a4` | `d7a8b187cf8444d4cfee102e8eae9e3043682fd5106d5d33ed677fe268a0e2ba` |
| `Arimo-Italic.ttf` | 502332 | `1e7b31c2b59ae634d4e1fd0acf52ff7c2cf7c796` | `ffdf690e4357a392e4edcc3df0b3adb0302920f75e54f2e6eec355afbe7edcba` |
| `Arimo-BoldItalic.ttf` | 508508 | `e7c5924ae3e8fbd2585c4ef73f620caa22b2a3ba` | `77c967d49d4ef4a63c39fbcd1e27892102e503e35a20983cb058430c4f268b34` |

**Tinos 1.340** is metric-compatible with Times New Roman, and is the
substitute for Times New Roman and CSS `serif`.
- Source: `googlefonts/tinos` commit
  `3b4482a99b80ea5fc75f187b1be3120a3f5905b3`, files `fonts/ttf/`.
- Licence: SIL OFL 1.1, retained as `OFL-Tinos.txt` (upstream `OFL.txt`,
  SHA-256 `cb3382d4643e8b02c12e322c220a3c76a5020d667e4fd4e7c75e744cca6caa6b`).

| File | Bytes | Git blob SHA-1 | SHA-256 |
|---|---|---|---|
| `Tinos-Regular.ttf` | 521588 | `893a1b525962984481aee5ce275516d384eb4865` | `60a0e8ef0c04dd5dd69ffe91025fa2ae5836cbd35600a82ba031977557e2cb61` |
| `Tinos-Bold.ttf` | 597880 | `887fc97a95d30a08190217b6075a5e0a0f92bec7` | `393269dbab8899f938db19783eca5eac92eb431f7ae0ab45b8349ca895f1a06b` |
| `Tinos-Italic.ttf` | 565016 | `5aed4f3757537bf334ca89dd6893063a3ef9ecc9` | `5942266ed398b155d7dc23e36833e7ec6be988f2439bdbeb8ef1bede808eaa91` |
| `Tinos-BoldItalic.ttf` | 578976 | `a9abd086816f68d6eb3672f1d0babd1db1807d4a` | `a5de79f0fe863ea0954757acb3d47b3ccd0a930ce3dd5b97230cd3866790a06e` |

**Selawik 1.01** is Microsoft's open fallback for Segoe UI, and the near
substitute for Segoe UI and Tahoma (Tahoma Bold is a named exception in the
gate).
- Source: the `microsoft/Selawik` release 1.01 archive `Selawik_Release.zip`
  (530652 bytes, SHA-256
  `3f62c51e05e3b5a1e6241cf92a371f0be2ea1183aa87b30718bbd40832a8d423`). The
  repository's tag 1.01 is commit
  `b65ff9ce7c4673e75ec5c622924a6b6f61f2f876`. Its tree holds only sources,
  and GitHub publishes no digest for the 2015 archive.
- Bundled: the Regular, Semibold and Bold TTFs. The archive's Light and
  Semilight faces and its web formats are not bundled. Selawik has no italic
  styles.
- Licence: SIL OFL 1.1, retained as `OFL-Selawik.txt` (the tag's
  `LICENSE.txt`, git blob SHA-1 `ac8fdb956049621af2aecd3a8253c38789748c4a`,
  SHA-256 `77b7c2506d4efb22e09c8ccf10159f4956eab3ef7c007fef95de136bcf45300c`).
  "Selawik" is a Reserved Font Name, which an unmodified copy keeps.

| File | Archive name | Bytes | SHA-256 |
|---|---|---|---|
| `Selawik-Regular.ttf` | `selawk.ttf` | 44224 | `e9d98518d8ac2817782a9a382430463a2e0793ea68350b695bb727d9a830ee1c` |
| `Selawik-Semibold.ttf` | `selawksb.ttf` | 44192 | `0a9e9d0549a10f24bef9b3a29e06fe6e0b5c21e7a784c503b048a307841a7783` |
| `Selawik-Bold.ttf` | `selawkb.ttf` | 44068 | `f0db5e174a90e0956ad7d2844bdca1d5e6da92ec65b2c04e57ba9b180668c904` |

Stage F's substitution table, `font-substitution.json` beside these files,
names every face it maps to. GUI.Drawing's raster module ships them in its
`fonts` directory and loads each face the first time a request resolves to
it. The SDK installs the whole font directory.

## Metric snapshots (stage F gate)

`tools/font_metrics_differential/reference_snapshots/` holds one text file
per gated request (owner decision F4-1, stage F decision 18). Each is the
bundled engine's measurement of one Windows reference face: its advances in
font units, its line metrics, and the corpus line widths, line breaks and
heights at each gate size. They were measured on 2026-10-23 by
`gui_forms_font_metrics_differential --snapshot` from the files in
`C:\Windows\Fonts` of a Windows 11 machine. Each snapshot names its reference
file by name, version, byte count and SHA-256; the hashes are also listed in
[`results/2026-10-23-f-font-substitution`](https://github.com/malkuth-software/notes/tree/gui_forms/results/2026-10-23-f-font-substitution). **No font data is included**: no
outlines, glyph programs, tables or other bytes of any reference face are in
the repository. The reference faces were read in place and never copied.
