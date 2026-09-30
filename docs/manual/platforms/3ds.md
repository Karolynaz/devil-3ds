# Devil-3Ds for Nintendo 3DS

Devil-3Ds adapts Diablo for the Nintendo 3DS's upper and lower screens. It is based on [DevilutionX](https://github.com/diasurgical/devilutionX).

## Installation

Download a `.cia` or `.3dsx` from this repository's [releases](https://github.com/Karolynaz/devil-3ds/releases) or [3DS build artifacts](https://github.com/Karolynaz/devil-3ds/actions/workflows/3ds.yml). The `.cia` appears on the HOME Menu. The `.3dsx` is for the Homebrew Launcher.

Copy `DIABDAT.MPQ` from a legally obtained Diablo installation to `/3ds/devilutionx/` on the SD card. That folder name remains unchanged for compatibility. Hellfire also needs `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, and `hfvoice.mpq`.

## In-game interface

- The upper screen displays the game and full-size character, quest, inventory, and spell panels.
- The lower screen displays the belt, life, mana, experience, and information area.
- Select opens the panels. When a panel is open, L and R switch between quests, character, inventory, and spells.
- Start opens the game menu.
- In inventory, Y drops an item. In the stash, tap Y to drop or hold Y to transfer an item between the two grids; L/R change stash pages.
- X casts the selected spell; Y opens spell selection. B cancels an open panel or prompt. D-pad Up opens Character, Left opens Quests, Right opens Spell Book, and Down toggles the map.
- Tap a belt item on the lower screen to use it. The stylus can also move the pointer and select items.

For controls and settings that may change during development, follow the prompts shown in the game.

## Language and performance

Lithuanian text can be selected in the game language settings. Voices and cinematics remain in their original language. The game uses the original 3DS data path `/3ds/devilutionx/`, including `diablo.ini`.

Original 3DS and New 3DS hardware have different performance characteristics. The upper screen game view is rendered at 400 × 240, while the lower screen is 320 × 240. The current build runs in 2D; stereoscopic rendering has been removed. Performance improvements have not been benchmarked on physical hardware; report any visual or control issues in [Issues](https://github.com/Karolynaz/devil-3ds/issues).

## Acknowledgment

This fork is based on DevilutionX. See the repository [README](../../../README.md) and [license](../../../LICENSE.md) for credits and legal information.
