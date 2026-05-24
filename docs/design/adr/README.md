# Architecture Decision Records

Each file records one architecturally significant decision for Gen VST. ADRs are
immutable once accepted — to change a decision, add a new ADR that supersedes the
old one rather than editing it.

**Status values:** `Accepted` (decided and in force), `Proposed` (recommendation
pending sign-off), `Superseded` (replaced by a later ADR).

| ADR | Title | Status |
|-----|-------|--------|
| [0001](0001-juce8-webview-ui.md) | Use JUCE 8 WebView for the plugin UI | Accepted |
| [0002](0002-ymfm-for-fm-emulation.md) | Use ymfm for YM2612 FM emulation | Accepted |
| [0003](0003-gpl-v3-license.md) | License the project under GPL v3 | Accepted |
| [0004](0004-furnace-only-factory-bank.md) | Ship only the Furnace tfilib factory bank | Accepted |
| [0005](0005-filesystem-patch-delivery.md) | Deliver factory patches via install-time filesystem copy | Accepted |
| [0006](0006-folder-tree-patch-browser.md) | Folder-tree patch browser instead of flat banks | Accepted |
| [0007](0007-fixed-window-size.md) | Fixed 960x640 plugin window for MVP | Superseded by 0023 |
| [0008](0008-clap-post-mvp.md) | Support CLAP as a post-MVP build target | Accepted |
| [0009](0009-sn76489-library.md) | SN76489 PSG emulation library | Accepted |
| [0010](0010-ymfm-instance-model.md) | ymfm voice instance model | Accepted |
| [0011](0011-resampling-strategy.md) | Chip-to-host resampling strategy | Accepted |
| [0012](0012-dmp-version-scope.md) | DMP format version scope | Accepted |
| [0013](0013-multitimbral-voice-model.md) | Six-part multitimbral architecture with a shared 16-voice pool | Superseded by 0021 |
| [0014](0014-special-channel-features.md) | Special-channel features under the one-channel-per-instance model | Superseded for DAC by 0021/0024; Ch3 deferral still in force |
| [0015](0015-webview-backend-support.md) | WebView backend support matrix & minimum versions | Accepted |
| [0016](0016-webview2-runtime-distribution.md) | WebView2 runtime distribution & Windows installer | Accepted |
| [0017](0017-hidpi-display-scaling.md) | HiDPI / display scaling across platforms | Accepted |
| [0018](0018-additional-patch-formats.md) | Additional patch formats (Y12, OPM, VGM extraction) are post-MVP | Superseded by 0019 |
| [0019](0019-additional-patch-formats-in-mvp.md) | Y12, OPM, and VGM bank import are in MVP scope | Accepted |
| [0020](0020-dt-register-encoding-y12-opm.md) | DT register encoding conversions for Y12 and OPM loaders | Accepted |
| [0021](0021-three-mode-single-engine-ui.md) | Three-mode single-engine instrument (FM/SQ/D per instance) | Accepted |
| [0022](0022-modern-vst-aesthetic.md) | Modern hardware-VST aesthetic (replaces pixel-art rules) | Accepted |
| [0023](0023-fixed-window-1200x560.md) | Fixed 1200×560 plugin window | Accepted |
| [0024](0024-hardware-filter-toggles.md) | Output character — independent Filter and Ladder toggles | Accepted |
| [0025](0025-tagged-preset-browser.md) | Tagged unified preset browser with mode auto-switch | Accepted |
