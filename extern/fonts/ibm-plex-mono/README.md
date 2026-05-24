IBM Plex Mono — bundled font for Gen VST v2
============================================

License
-------

SIL Open Font License (OFL) 1.1 — see `OFL.txt`. Copyright © 2017 IBM Corp.
with Reserved Font Name "Plex".

Source
------

Pulled from `@fontsource/ibm-plex-mono` (npm package, MIT) via the
jsDelivr CDN. The upstream files originate from IBM's official Plex
release at https://github.com/IBM/plex.

Files
-----

Six woff2 files. The 09-visual-spec.md typography table needs Regular
(400), Medium (500), and Bold (700); each weight is split into two
unicode-range subsets so the browser only fetches what each page needs:

- `ibm-plex-mono-latin-400.woff2`        — Latin Basic (U+0000–00FF + ext-A overlap)
- `ibm-plex-mono-latin-500.woff2`
- `ibm-plex-mono-latin-700.woff2`
- `ibm-plex-mono-latin-ext-400.woff2`    — Latin Extended-A (U+0100–024F + …)
- `ibm-plex-mono-latin-ext-500.woff2`
- `ibm-plex-mono-latin-ext-700.woff2`

Total on-disk: ~88 KB across all six. The 01-static-ui-mockup task
calls for a "single woff2" — that wording assumed IBM Plex Mono shipped
as a variable font, which it does not. Splitting per-weight per-subset
is the standard convention and matches how every other site serves
this family. The `@font-face` block in `ui/src/styles/design-system.css`
references all six files via `unicode-range` so each weight resolves
the right subset transparently.

Replacing the subset
--------------------

If a future change needs additional unicode ranges (e.g., Cyrillic,
Greek) pull the corresponding `latin-ext`/`cyrillic`/`greek` files from
`@fontsource/ibm-plex-mono@latest` and add a matching `@font-face` block
with the right `unicode-range`. Do not hand-subset with `pyftsubset`
unless there is a measurable size win — the fontsource files are
already efficiently subset.
