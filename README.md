![Devil-3Ds](docs/images/title.png)

# Devil-3Ds

**Diablo, rebuilt around Nintendo 3DS's two screens.**

Nintendo 3DS already had a Diablo port, but its desktop-style interface made it difficult to play comfortably on the handheld. Devil-3Ds makes that port fit the console. **Based on [DevilutionX](https://github.com/diasurgical/devilutionX)**, it brings the controls and interface into a layout designed for 3DS.

- **Redesigned lower-screen UI:** the belt, health, mana, experience and item information share a dedicated 320 × 240 display, with touch support and animated health/mana bars.
- **Panels drawn for the upper screen:** inventory, stash, character, quest and spell screens have redesigned backgrounds and layouts for 400 × 240, with readable text and appropriately sized items.
- **Reviewed and optimized code:** screen copying and scaling do less repeated work; controls, spell selection and interaction targeting have been revised. This version runs in 2D.

P. S. This is not „hey chatgpt. make me diablo 3ds. no mistakes“ release. Spend a lot of hours not only with code, but also doing UI screens in Figma, etc. So if you play Diablo 2 and like this port - you can send me some Jah ( https://forums.d2jsp.org/user.php?i=1483218 );)

[Download the latest build](https://github.com/Karolynaz/devil-3ds/releases/latest) · [Report an issue](https://github.com/Karolynaz/devil-3ds/issues)

## Installation

You need a Nintendo 3DS set up to run homebrew + FBI and your own Diablo game data. The original game data is **not included**.

1. Download `devil-3ds.cia` or `devil-3ds.3dsx` from [Releases](https://github.com/Karolynaz/devil-3ds/releases/latest).
3. Create `/3ds/devilutionx/` on the SD card and copy `DIABDAT.MPQ` from your Diablo installation into it. For the shareware edition, use `spawn.mpq` instead.
2. Put `Devil-3Ds.cia` to the SD card and install it with FBI. When updating, install the new CIA or replace the 3DSX; keep the data folder and saves.
3. For **Hellfire**, also copy `hellfire.mpq`, `hfmonk.mpq`, `hfmusic.mpq` and `hfvoice.mpq` into the same data folder. But I don't own Hellfire, so didn't tested.
4. Launch **Devil-3Ds**. 
5. The `/3ds/devilutionx/` data folder name is retained for compatibility with existing installations and saves. Both download formats include the custom interface assets and Lithuanian translation. Performance varies between original and New 3DS hardware.

## Controls

### Playing

| Action | Button |
| --- | --- |
| Move | Circle Pad |
| Attack / interact / talk / pick up a ground item | A |
| Cast the selected spell or use a skill | X |
| Open spell / skill selection | Y |
| Cancel / go back | B |
| Use a potion from belt slots 1–4 | L |
| Use a potion from belt slots 5–8 | R |
| Open inventory panels | SELECT |
| Pause and open the game menu | START |
| Open character panel | D-pad Up |
| Open quest log | D-pad Left |
| Open spell book | D-pad Right |
| Toggle map | D-pad Down |
| Use an item in the belt | Tap its lower-screen slot |

### Inventory, stash and menus

| Action | Button |
| --- | --- |
| Move the pointer / navigate choices | Circle Pad |
| Confirm / pick up or place an item | A |
| Use an inventory item | Hold A |
| Close a panel / cancel | B |
| Switch panels | L / R |
| Drop an inventory item | Y |
| Drop an item while the stash is open | Tap Y |
| Transfer an item between inventory and stash | Hold Y |
| Change stash page | L / R while the stash is open |
| Open the stash gold withdrawal prompt | SELECT while the stash is open |

Panels cycle in this order: **Quests → Character → Inventory → Spell Book**, wrapping at either end. Use the Circle Pad to navigate spell selection and A to confirm. The lower screen displays information about the highlighted enemy, NPC or item. The title-screen settings also include a **Controls** reference.

## Credits

This project builds on the work of the [DevilutionX contributors](https://github.com/diasurgical/devilutionX/graphs/contributors), [Devilution](https://github.com/diasurgical/devilution#credits) and the original Diablo creators. See [LICENSE.md](LICENSE.md) for the Sustainable Use License; upstream credits and third-party notices remain in the repository.

Diablo and Blizzard Entertainment are trademarks of Blizzard Entertainment. Devil-3Ds is an independent fan project, not affiliated with or endorsed by Blizzard Entertainment, GOG.com or the DevilutionX maintainers.

## Bonus: Lietuvių kalba

Kadangi mano trečiokas dar ne super duper anglų kalbos žinovas, tai žaidimo tekstai išversti į lietuvių kalbą: meniu, sąsaja, daiktų ir burtų pavadinimai, užduotys bei istorijos dialogai. Vertimas įtrauktas į abu diegimo failus – papildomai nieko atsisiųsti nereikia, bet jei žaidžiate Diablo 1 ant mac, PC - galite atsisiųsti tik vertimo failą.
P. S. Balsai ir filmukai lieka angliški.

## Pictures

![Devil-3Ds on Nintendo 3DS — picture 1](docs/images/1.jpg)

![Devil-3Ds on Nintendo 3DS — picture 2](docs/images/2.jpg)

![Devil-3Ds on Nintendo 3DS — picture 3](docs/images/3.jpg)

![Devil-3Ds on Nintendo 3DS — picture 4](docs/images/4.jpg)

![Devil-3Ds on Nintendo 3DS — picture 5](docs/images/5.jpg)

![Devil-3Ds on Nintendo 3DS — picture 6](docs/images/6.jpg)

![Devil-3Ds on Nintendo 3DS — picture 7](docs/images/7.jpg)

![Devil-3Ds on Nintendo 3DS — picture 8](docs/images/8.jpg)

![Devil-3Ds on Nintendo 3DS — picture 9](docs/images/9.jpg)

![Devil-3Ds on Nintendo 3DS — picture 10](docs/images/10.jpg)

![Devil-3Ds on Nintendo 3DS — picture 11](docs/images/11.jpg)

![Devil-3Ds on Nintendo 3DS — picture 12](docs/images/12.jpg)

