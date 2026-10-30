# Town edge rendering — 30 October 2026

A report described slight wobbling on the right side of the screen in town, absent in dungeons. No video or physical-console reproduction was available during this change.

The tile traversal only visited cells whose tile footprint reached the viewport. Town NPC sprites can extend left of their tile origin, so the first excluded column can still contribute visible pixels. A host regression executes the production DrawTileContent traversal against a deliberately wider reference using synthetic 96-pixel sprites across 129 horizontal camera offsets and alternating isometric rows. The previous column count fails the comparison; one additional town column passes. This guard is applied before lightmap generation and all rendering passes. Dungeon coverage stays unchanged.

The native 400×240 world buffer also persisted between new scene renders, despite transparent tiles not necessarily covering every pixel. New frames now clear its visible pixels to palette index zero, without touching pitch padding. Palette-only/repeated frames retain the completed scene. The regression runs production world_surface.cpp through 200 lifecycle cycles, checking right-edge pixels, keyed UI rows, padding and repeat/invalidation behavior with ASan/UBSan.

These address concrete rendering defects consistent with edge artifacts. They do not establish that the user's exact hardware symptom is eliminated. Physical town movement remains the final visual check.

The README and platform guide now consistently put both the Homebrew Launcher executable and game data in /3ds/devil3ds/. Legacy /3ds/devilutionx/ fallback behavior is retained.
