# Archived design docs

The files in this folder describe the **v1** UI/UX of Gen VST and are
**superseded** by the v2 design.

| Archived file | Original path | Superseded by |
|---|---|---|
| `v1-05-ui-ux.md` | `docs/design/05-ui-ux.md` | New `docs/design/05-ui-ux.md` (v2 UI strategy) |
| `v1-08-ui-views.md` | `docs/design/08-ui-views.md` | New `docs/design/08-ui-views.md` (v2 view catalog) |
| `v1-genny-ui.md` | `docs/genny-ui.md` | New `docs/design/09-visual-spec.md` (v2 visual spec) |

The v2 design pivot is documented in ADRs **0021–0025**. The high-level
summary:

- **v1** modelled the plugin as a six-part multitimbral Genesis-in-a-box with
  a pixel-art skeuomorphic chassis, an instrument rack, and a MIDI routing
  matrix (see ADR-0013).
- **v2** treats each plugin instance as a single-engine instrument that runs
  in one of three modes — FM, SQ or D — with a modern hardware-VST aesthetic
  modelled on Inphonik's RYM2612 (ADR-0021).

The v1 task chain has been moved to `docs/tasks/mvp/` (formerly at
`docs/tasks/01-*.md` … `docs/tasks/34-*.md`); it is historical. v2 work
tracks under `docs/tasks/v2/`. The v1 task files remain available as the
implementation history of the engine and infrastructure code that v2
inherits.

Engine code in `src/` is the v2 baseline — only the per-mode UI and the
multitimbral wiring are being replaced. The factory patch bank
(`extern/patches/`), the chip emulators (`third_party/ymfm`, `third_party/libvgm`),
the patch-format loaders, and the build system are unchanged.
