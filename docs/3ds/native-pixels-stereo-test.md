# Native pixels and stereo test — 2026-10-03

## Backup and base

The original checkout is b53c33f9c982e852db10fcd8c1867ef783b3fa7b.
Local backup: `Backups/2026-10-03_before_native_pixels/source.zip`.
The ZIP was checked for CRC errors and includes all tracked files and the
existing untracked translation release folder. Previous CIA/3DSX packages remain
in `3DS_Release/2026-09-30_b53c33f`.

Reviewed community [PR #2](https://github.com/Karolynaz/devil-3ds/pull/2),
4c62a41f6da76cb1f00c68adf5808533b8c36073. Its useful change is separating the
world from the UI. Its default 400-pixel camera changes the visible area, and
its other zoom levels use bilinear sampling. The hardware slider controls zoom.
Those choices do not match the requested test. This branch retains the separate
scene buffer, removes the added zoom settings, and keeps the original 640x240
world view and original nearest-neighbor sampling.

## Rendering changes

- The supplied 400x240 inventory, stash, character and spell backgrounds stay on
  their native pixel grid. All six inventory backgrounds were verified byte for
  byte through the 400 -> 640 -> 400 presentation path.
- Static menu images, animated logos, selectors and the menu cursor are placed
  on the physical screen grid. Small images are 1:1; oversized artwork is fit
  with one aspect ratio using nearest-neighbor sampling. This cannot make large
  source artwork literally 1:1, but it avoids independent X/Y stretching.
- Hero portraits preserve their aspect inside the existing layout rectangles.
- Movies retain the dual-screen framebuffer and fit within the top screen,
  with a black bottom screen. A 320x200 frame stays 320x200.
- Inventory item artwork preserves aspect, including held items. Held items use
  the same native dimensions as placed items, even though the mouse canvas is
  still 640 pixels wide.
- Health, mana and experience fill reveals the original bar pixels instead of
  squeezing its artwork into the remaining fill. The existing liquid animation
  is retained.

The entire 640-pixel world cannot be shown 1:1 on a 400-pixel screen without
cropping or redesigning its graphics. Its framing remains unchanged as requested.
Legacy shop side panels and some engine UI are still legacy-sized; this test
is not a claim that every game sprite is native-resolution artwork.

## Experimental 3D

The old tile/scanline depth approach gives different sections of a single actor
different depth. This test renders the scene separately for both eyes. Complete
player, monster, NPC, item, object and missile sprites receive one consistent
horizontal shift, including outlines and player effect icons. Ground and walls
remain on the screen plane; this is layered 2.5D, not reconstructed 3D geometry.

The 3D slider changes only stereo, never camera zoom:

- Off: normal mono rendering, no second world render.
- Lower range: 5 physical pixels of disparity.
- Upper range: 10 physical pixels of disparity.

These coarse steps keep both eyes on the same sprite sampling phase (8 logical
columns = 5 physical columns). Stereo is disabled for panels, maps, NPC dialog,
shops, pause and game menus. Labels, HUD, and spell selection UI are flat.
The second render does not enqueue item labels or update dead-player tile flags.

This approach requires another scene render while stereo is enabled. Performance,
comfort, direction of perceived depth, wall occlusion, and correct operation on
physical 3DS hardware must be tested. No claim of hardware verification is made.

## Checks and hardware test

Run `python3 tools/tests/test_ctr_pixels.py` and
`python3 tools/tests/test_ctr_ui_geometry.py`.

On the console, compare inventory/stash borders at slider off/on, check logo
proportions, create a hero, drag a potion and a helmet, play an intro movie, and
then move beside a wall and around enemies with the slider on. Check whole
actors and mana-shield sprites rather than individual floor tiles. Lower the
slider if the second render affects frame rate.

This branch is for testing. Do not publish its installers to Releases yet.

## Screen repair test — 2026-10-03

The old emulator configuration contained Width=800, Height=480. The native
presenter only accepts a 640x480 indexed canvas. An 800-wide canvas bypassed it,
showing the scene key as a solid color and letting SDL filter encoded UI pixels.
Movie routing also bypassed its 640x480 path. Menu elements included an extra
80-pixel logical offset, cutting off right selectors.

The 3DS resolution loader now migrates to 640x480; window creation also fixes
that canvas size, including command-line overrides. No settings deletion is
needed. Fullscreen toggles cannot recreate a 32-bit SDL surface. Initial GPU
synchronization retries without waking SDL's texture renderer.

The new supplied 400x240 loading logo is encoded across the complete top screen.
Loading text remains centered in both axes over two lines on the bottom screen.
New Hero hides its empty summary and enlarges the three-hero picture by an
integer factor of two (360x152), centered on the 400x240 screen. Choosing an
existing hero or a class restores the normal summary. The menu uses Credits.

Run `python3 tools/tests/test_ctr_presenter.py` in addition to the two tests above.
It executes the production presenter with mocked hardware calls, checking world
composition, both eyes, right-edge pixels, movie frame separation and recovery
from temporary GPU access failures. These checks do not establish physical 3DS
operation. Check the repaired build on the console before publishing a release.

## Follow-up: independent line centering and game fade

- Center each explicit line independently. The old renderer counted the newline
  glyph plus spacing as part of the next line's width. This displaced the second
  loading/copyright line. The English loading text has the explicit break
  `Not Even Death` / `Can Save You`; translated text is retained.
- Keep the scene attached during every palette-only game fade frame. Replacing
  the canvas with a menu/loading/movie frame invalidates the scene reference.
  This prevents the orange world-key palette entry appearing during startup.
- Show the original title demon behind the main menu's animated flaming logo.
  The artwork is read from the player's game files and translated once to the
  menu palette; no new original-game artwork is distributed.
- Stash heading moves down 2 native pixels; all three buttons and their pointer
  hit regions move up 5 native pixels.

Verification: `test_ctr_multiline.py` executes the production multiline layout,
including both loading/copyright strings with different spacing and alignment;
it fails on the previous code. `test_ctr_fades.py` executes production palette
fade routines, world-buffer lifetime and the native framebuffer presenter,
checking that all fade frames use the scene and that cleared art does not reuse
it. Existing native pixels, geometry and presenter tests also pass.

This build is for local console testing. No emulator test and no Releases upload.

## Follow-up: native spell icons, category rows and camera

Backup before this change: `Backups/2026-10-03_before_native_spells_camera/source.zip`,
base commit `9c240a9005f2ddf9b055c7a2f53378c2c5167b9d`.

- Original spell artwork is 56x56. The old active HUD icon used 40x40 and picker
  icons used 30x30. Both now use the same original 56x56 pixels, encoded on the
  global native sampling grid. No local resizing is used. Drawing the selection
  border last preserves all four two-pixel edges.
- The picker has separate rows for learned spells, equipped staff charges,
  innate skills, and scrolls, in that order. Empty categories are omitted.
  Left/right moves within a category, up/down changes category. Each row shows
  six icons and scrolls horizontally to reveal every available entry; side arrows
  indicate more entries. Opening the picker focuses the current spell and source.
  The separate active HUD icon is hidden while the picker is open to avoid overlap.
- The player anchor moves down 28 screen pixels so the torso is closer to screen
  center. World pointer conversion and the full automap follow that offset.
  Pointer rows above zero are normalized before tile/diamond conversion, keeping
  top-edge targeting aligned. World size, sampling and stereo slider are retained.

Verification: `test_ctr_spell_pixels.py` uses the production native blitter and
border renderer to compare three original reference frames byte for byte at all
horizontal sampling phases, checks every border edge in all row layouts, and
checks clipping with address/undefined-behavior sanitizers.
`test_ctr_spell_navigation.py` executes production picker layout and navigation
with all three base-class skills, spell/staff/scroll masks, long rows, duplicate
spell IDs from different sources, removed categories and empty lists.
`test_ctr_camera.py` executes production view and inverse pointer geometry and
checks rendered tile centers map back to the same tiles in both zoom modes.
The existing pixel, layout, presenter, multiline and fade checks also pass.

Test on the physical console: inspect both icon sizes and all selection edges,
move through a row with more than six spells, switch between categories, cast
from each source, and check player framing and targeting near the top edge.
No emulator test and no Releases upload.
