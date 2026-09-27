# Devil-3Ds

**Diablo on Nintendo 3DS, designed for its two screens.**

Devil-3Ds is a work-in-progress 3DS port with a redesigned game interface, readable panels, touch controls, handheld shortcuts, and Lithuanian text. It gives the 3DS a more comfortable way to play Diablo. This project is **based on [DevilutionX](https://github.com/diasurgical/devilutionX)**; the upstream 3DS interface and controls were difficult to use comfortably on the console, so this fork adapts them for handheld play.

## Install

Get the latest 3DS `.cia` or `.3dsx` from this repository's [Releases](https://github.com/Karolynaz/devil-3ds/releases) or [3DS build artifacts](https://github.com/Karolynaz/devil-3ds/actions/workflows/3ds.yml). The `.cia` installs to the 3DS HOME Menu; the `.3dsx` runs from the Homebrew Launcher. Development artifacts require a GitHub sign-in.

The game data is **not included**. Copy `DIABDAT.MPQ` from your own Diablo installation to `/3ds/devilutionx/` on the SD card. The original data folder name is retained for compatibility with existing installations and saves. For Hellfire, also copy `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq`, and `hfvoice.mpq`. See the [3DS guide](docs/manual/platforms/3ds.md) for more details.

## 3DS interface

The upper screen shows the game or a full-size character, quest, inventory, or spell panel. The lower screen shows the belt, life, mana, experience, and contextual information. Select opens panels; L and R cycle between them while a panel is open. Start opens the game menu. Tapping a belt item uses it.

The 3DS can display a 400 × 240 game image on its upper screen. The interface uses both displays. Availability and performance of features may vary between original and New 3DS models.

## Development

The [3DS workflow](.github/workflows/3ds.yml) builds installable files. The source remains compatible with the upstream asset and data format. The new 3DS emblem and its conversion script are in [`art/3ds`](art/3ds) and [`tools/build_3ds_brand_art.py`](tools/build_3ds_brand_art.py).

Issues and feedback: [github.com/Karolynaz/devil-3ds/issues](https://github.com/Karolynaz/devil-3ds/issues).

## Credits and legal

Devil-3Ds is maintained by **Karolynaz**. It builds on the work of the [DevilutionX contributors](https://github.com/diasurgical/devilutionX/graphs/contributors), the [original Devilution project](https://github.com/diasurgical/devilution#credits), and the original Diablo creators. Upstream credits and third-party notices remain in the repository.

The source is provided under the [Sustainable Use License](LICENSE.md) and is for non-commercial use. Diablo and Blizzard Entertainment are trademarks of Blizzard Entertainment. This fan project is not affiliated with or endorsed by Blizzard Entertainment, GOG.com, or the DevilutionX maintainers. Obtain the original game data legally.
