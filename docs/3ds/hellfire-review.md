# 3DS code and Hellfire review — 2026-10-04–05

This review covers the custom 3DS startup, asset lookup, inventory and spellbook layouts, and update checker. It does not establish that every game path is bug free. The changes are local and have not yet been compiled into a new CIA/3DSX, run in an emulator, tested on a console, or published.

## Backup

The tracked source before these changes is in `Backups/2026-10-04_before_hellfire_review/source.zip`, with base commit `769cbd5` recorded alongside it. ZIP CRC validation passed. Existing release packages and the untracked `Translation_Release` directory were left intact.

## Confirmed issues fixed

| Issue | Change |
| --- | --- |
| Hellfire support files were copied to `build/mods/hf`, outside the `build/romfs` packaged by the 3DS build. | The 3DS mod output directory is now `build/romfs/mods`. The existing copied-assets target is a dependency of `libdevilutionx`, before CIA/3DSX packaging. |
| Packed-MPQ builds did not search bundled loose Hellfire files. The expansion's startup script, tables and replacement UI art could therefore be missing. | Active built-in mods mount their RomFS directories. Original MPQs remain the fallback; SD overrides and external packed mods keep precedence. |
| Bundled Hellfire metadata was not loaded, losing its `hsv` save namespace and `HRTL` program ID. | The built-in mod receives its bundled manifest. User-provided packed mods retain their own identity and manifest. |
| Treating shipped loose files as user overrides would block multiplayer's data integrity checks. | Bundled read-only assets use a separate mount list and retain their built-in identity. User overrides remain detectable. |
| Moving an asset reference discarded its `isOverridden` flag. | Move construction and assignment now preserve provenance as well as the stream handle. |
| A missing data/save folder caused the write probe to report a read-only error. POSIX `mkdir` handling also compared its return value with `EEXIST`. | Create the 3DS preference folder before probing it. Accept an existing directory by checking `errno`; reject a blocking regular file. |
| Missing or unreadable primary Hellfire data could be ignored. Hellfire could also be enabled before discovering that the base archive was shareware. | Check the primary archive's load result; reject Hellfire plus shareware at both expansion loading and base-archive discovery. Existing checks for the three companion archives remain. |
| Error dialogs used a desktop rectangle spanning both screens, clipping headings, paths and file lists. | Draw caption and wrapped native-size body text on the upper screen, with a centered lower-screen OK touch target. A/B still dismiss the dialog. |
| Hellfire has five spellbook pages, but the native background contained four tab frames. Spell lookup also had an inclusive upper-bound assertion. | Replace the tab strip with five native frames in Hellfire mode; return an invalid spell for out-of-range page/entry indexes. Diablo retains its four-tab art. |
| The native inventory background used the local player's class while its items used the inspected player's class. | Use the inspected class for both, including Monk, Bard and Barbarian. |
| The update worker could terminate without publishing a result or cleaning up curl resources after an allocation failure. | Protect the response callback, use automatic HTTP cleanup, and publish a failure result when exceptions are enabled. Exception-disabled builds still compile. |

## Performance improvement

Network initialization now happens when ZeroTier, TCP or the update checker needs it. Until then, single player avoids the 1 MiB SOC buffer and a Wi-Fi wait of up to five seconds. TCP builds without ZeroTier also initialize sockets explicitly before connecting.

The update worker uses background thread priority `0x38`, consistent with the native ZeroTier worker, so it has lower scheduling priority than the main thread. These are startup and resource improvements; no console FPS measurements were performed.

The existing stereo path already skips the right-eye world render when the slider is off. Its buffer is allocated on first use of 3D and retained for reuse; turning 3D off stops the extra render work.

Further frame-rate work should start with console timings for world rendering and framebuffer conversion, comparing the same scene with 3D off and on. Hellfire adds more content, so level-load peak memory and time also deserve measurement. Changing render behavior without those measurements is unnecessary for this patch.

## Hellfire data and UI

Put all five original game files together in `/3ds/devilutionx/`:

```text
DIABDAT.MPQ
hellfire.mpq
hfmonk.mpq
hfmusic.mpq
hfvoice.mpq
```

Hellfire is an expansion of full Diablo. Its four archives alone, or combined with `spawn.mpq`, do not supply the required base data. The original commercial data is not bundled. This matches the [official DevilutionX MPQ instructions](https://devilutionx.com/mpq).

Hellfire includes UI differences as well as new quests, monsters, items, spells and levels:

- A different title/logo and class portraits.
- Monk in class selection; optional Bard and Barbarian choices are controlled by the existing asset/settings checks.
- A fifth spellbook page: Lightning Wall, Immolation, Warp, Reflect, Berserk, Ring of Fire and Search.
- Extra class skills, rune/oil handling and Hellfire-specific settings such as the Cow and Theo quests.
- Separate save/stash names through the Hellfire manifest (`.hsv`), preserving the Diablo `.sv` namespace.

The custom inventory, character, stash and lower-screen HUD layouts remain shared. The six class mappings already select the appropriate inventory family: Warrior/Barbarian, Rogue/Bard and Sorcerer/Monk. The class list fits all six choices on the lower screen.

## Verification completed

All 17 scripts in `tools/tests/test_*.py` passed on the host. They compile production functions or exercise production data/assets; SDK, HTTP and archive-opening services are substituted where console services are unavailable.

| Check | Coverage |
| --- | --- |
| `test_game_data_folders.py` | Missing/existing folders, a regular file blocking a directory, write-probe cleanup. ASan/UBSan. |
| `test_hellfire_assets.py` | Bundled script/tables and manifest, external asset precedence, integrity separation, unloading, failed archive opens, missing companion files, shareware detection before/after mod initialization, complete archive set, declared packaged files. ASan/UBSan. |
| `test_asset_reference_moves.py` | Override flags survive moves; ownership transfer closes each stream once; built-in files do not acquire the override flag. ASan/UBSan. |
| `test_hellfire_spellbook.py` | All six class skills from shipped data, all fifth-page spells, invalid page/entry bounds. ASan/UBSan. |
| `test_ctr_data_error_layout.py` | Eight English/Lithuanian folder/archive messages fit with the shipped font metrics; required file names remain intact. ASan/UBSan. |
| `test_ctr_update_worker.py` | Release/comparison states, Wi-Fi/HTTP/thread/allocation failures, response bounds, cleanup; builds with and without exceptions. ASan/UBSan. |
| `test_ctr_spell_navigation.py`, `test_ctr_ui_geometry.py` | Hellfire spells/runes and all class skills reachable, separate source rows, paging, five non-overlapping spellbook tabs. |
| `test_ctr_update_json.py`, `test_ctr_settings_layout.py` | Nested/malformed/large release JSON; 86 English/Lithuanian names and values fit two lines. |
| Existing pixel/presenter/fade/camera/minimap/multiline checks | Native pixel round trips, original spell frames/borders, scene composition, stereo/movie routing, fades, camera targeting, map bounds and independent text-line centering. |

The archive tests simulate successful/failed opens and asset lookup. They do not certify the contents of a commercial MPQ or exercise every expansion quest. No emulator was launched, as requested.

## Console checks after the next build

1. Launch with all five original archives; verify the Hellfire title, Monk creation, fifth spellbook page and new spell icons.
2. Enter both Nest and Crypt, open the custom panels, use a rune/oil, and save/reload. Check that Hellfire uses `.hsv` saves and stash.
3. Select Diablo with the same complete data folder; confirm its four spellbook tabs and existing `.sv` saves remain available.
4. In a disposable test folder, remove one required archive and confirm the whole error message is readable. Test `spawn.mpq` alone separately.
5. Launch single player with Wi-Fi off. Check Update and multiplayer with Wi-Fi on/off, including returning after a failed check.
6. Compare an identical scene with the 3D slider off/on on the console; record frame times and memory before pursuing further renderer optimizations.

No GitHub Release has been created. GitHub Actions compilation requires an approved push of the reviewed source; this review has not exported it.
