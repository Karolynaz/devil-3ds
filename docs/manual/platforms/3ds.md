# Devil-3Ds for Nintendo 3DS

Devil-3Ds adapts Diablo for the Nintendo 3DS's upper and lower screens. It is based on [DevilutionX](https://github.com/diasurgical/devilutionX).

## Installation

Download a `.cia` or `.3dsx` from this repository's [releases](https://github.com/Karolynaz/devil-3ds/releases) or [3DS build artifacts](https://github.com/Karolynaz/devil-3ds/actions/workflows/3ds.yml). The separate CIA identity and `/3ds/devil3ds/` default described below apply to **3.27 and later**. Earlier builds use `/3ds/devilutionx/`. The `.cia` appears on the HOME Menu. The `.3dsx` is for the Homebrew Launcher.

Copy `DIABDAT.MPQ` from a legally obtained Diablo installation to `/3ds/devil3ds/` on the SD card. This is the default folder for game data, saves and settings. If it does not exist and `/3ds/devilutionx/` already exists, the game uses that old folder automatically. If both folders exist, `/3ds/devil3ds/` takes priority. The CIA has its own Title ID, so it installs alongside DevilutionX. Hellfire also needs `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, and `hfvoice.mpq`.

## In-game interface

- The upper screen displays the game and full-size character, quest, inventory, and spell panels.
- The lower screen displays the belt, life, mana, experience, and information area.
- Select opens the panels. When a panel is open, L and R switch between quests, character, inventory, and spells.
- Start opens the game menu.
- In inventory, Y drops an item. In the stash, tap Y to drop or hold Y to transfer an item between the two grids; L/R change stash pages.
- (X), (Y), (L) and (R) cast assigned spells and skills. In Spells & Skills, (A) opens shortcut assignment. (B) cancels an open panel or prompt. D-pad Up opens Inventory, Left uses potions from belt slots 1–4, Right uses slots 5–8, and Down toggles the map.
- Tap a lower-screen spell icon to cast it. Hold a belt potion or scroll to use it; drag it onto a belt slot to move or swap items. Releasing outside the belt cancels the move.

For controls and settings that may change during development, follow the prompts shown in the game.

## Language and performance

Lithuanian text can be selected in the game language settings. Voices and cinematics remain in their original language. The game stores `diablo.ini` and saves in the selected data folder, using the same new-folder/legacy-folder rule described above.

Original 3DS and New 3DS hardware have different performance characteristics. The upper screen game view is rendered at 400 × 240, while the lower screen is 320 × 240. The current build runs in 2D; stereoscopic rendering has been removed. Performance improvements have not been benchmarked on physical hardware; report any visual or control issues in [Issues](https://github.com/Karolynaz/devil-3ds/issues).

## Acknowledgment

This fork is based on DevilutionX. See the repository [README](../../../README.md) and [license](../../../LICENSE.md) for credits and legal information.
