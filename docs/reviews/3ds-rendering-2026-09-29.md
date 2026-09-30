# 3DS rendering review — 2026-09-29 / 2026-09-30

Reviewed project branch: `codex/3ds-fullscreen-panels`, starting at `1f16506`.
The local checkout matched the remote branch before this review.

## Findings

- The SDL 1.2 backend submits only a left-eye Citro3D render target. Citro3D
  marks a frame stereo only when a right-eye target participates. Its mono
  swap therefore presents the left buffer to both eyes, even if application
  code has separately written the right buffer and enabled `gfxSet3D`.
- The previous direct framebuffer writer used the opposite rotation from
  libctru's column-major layout. Screen coordinate `(x, y)` addresses pixel
  `x * 240 + 239 - y`.
- SDL's fit-to-screen path selects linear texture filtering. This mixes
  neighboring text pixels. Native UI also went through 400 -> 640 -> 400
  nearest-neighbor scaling with incompatible rounding. At 640 columns,
  320 of the 400 recovered columns selected the preceding native column.
- Scaled glyphs skipped the outline path. Some in-game overlays also missed
  horizontal compensation for the 640 -> 400 display transform.
- The native keyboard queue compared its item count with the array's byte
  size, allowing writes beyond its 16 entries.
- Inventory/stash tinting treated all items with `_iIdentified == false` as
  unidentified. Normal-quality items do not require identification.

## Implemented corrections

Normal dual-screen frames now use one native presenter for 2D and stereo.
It writes both eye buffers when stereo is enabled, submits their distinct
addresses together, and copies the bottom screen with nearest sampling.
Movie modes retain SDL presentation and restore the native presenter through
video-mode reconfiguration. UI panels use inverse sampling to preserve their
400 columns; scaled glyphs align to the physical sample positions and retain
outlines. Scratch clearing is limited to the current glyph dimensions.

Static checks covered all glyph horizontal phases including cropped origins,
400 -> 640/720/800 -> 400 panel round trips, framebuffer pixel coverage and
bounds for 16/24/32-bit formats, and disparity source bounds. Diff whitespace
checks passed using the repository's CRLF convention.

## Scope and limits

Changes target 3DS presentation, UI sampling, and the concrete defects above.
They do not replace the game world with a 3D renderer. The existing depth
approximation uses screen-row-dependent horizontal disparity; it has no
per-object depth or hidden surfaces. Visual depth, eye comfort, performance,
HOME/sleep recovery, and software keyboard/movie transitions require a later
hardware test.

No application build, emulator, hardware session, commit, push, or GitHub
Actions run was performed during this review. Static review and arithmetic
checks cannot establish hardware performance or certify all gameplay code.

## Upstream evidence

- [devkitPro SDL package recipe](https://github.com/devkitPro/pacman-packages/blob/master/3ds/SDL/PKGBUILD)
- [SDL 1.2 3DS backend patch](https://github.com/devkitPro/pacman-packages/blob/master/3ds/SDL/SDL-1.2.15.patch)
- [Citro3D frame submission](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c)
- [libctru framebuffer presentation](https://github.com/devkitPro/libctru/blob/master/libctru/source/gfx.c)
