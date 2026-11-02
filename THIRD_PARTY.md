# Third-party notices

- **FM1 Open V14 SDK** — `m-vave/fm1_open` @ `4c7496daac04a95ff10d1afdcc8f9ead2dbabc41`. Application sources GPL-3.0-only WITH the FM1 SDK linking exception (`LICENSE.md`); the binary engine (`libfm1_engine.a`), boot and update resources keep their separate terms in the SDK's `THIRD_PARTY.md`. RIDDIM ROOTS replaces `app/screen.c` (own UI) and `app/fm_factory.c` (minimal factory voice) and inserts two one-line hooks (`midi_router.c`, `audio_output.c`) at build time; the SDK tree itself is reset to the pinned revision before every build. The 5×7 digit/letter glyphs and the single fallback DX7 voice come from M-VAVE's GPL app sources (`app/screen.c`, `app/fm_factory.inc`). The 808-kit preset layout follows `app/presets.c`.
- **JieLi Windows toolchain 2.5.3** (PI32v2 clang 4.0.1) — downloaded from JieLi, verified against the SDK manifest; not redistributed.
- **fm1-emu** — `joakimeriksson/fm1-emu` @ `eb0d897` (MIT, © 2026 Joakim Eriksson). Used for native verification (Node) and copied unmodified into `demo/emulator/` with its license (`LICENSE-fm1-emu.txt`); `firmwares.json` there is our own one-entry catalog.
- **TinyCC 0.9.27** (LGPL) — host test compiler, downloaded, not redistributed.
- **Lion artwork** — supplied by the user (see `assets/lion/README.md`); the user's rights apply.
- **Samples** — none. All sounds are synthesized (DX7-format FM, Karplus-Strong, 303, 808, PSG engines of the SDK and RIDDIM's own DSP). No recordings or copyrighted songs are included.
- **References consulted only** (no code copied): JIANT (juanjiant-bit), Felucca (hugelton), fm1-dev-wiki (sbegueria, CC BY-SA 4.0), public Alesis Vortex Wireless 2 user/editor guides.
