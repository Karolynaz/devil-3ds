# 3DS Diablo UI grafikos nuorodos

Šie PNG ištraukti iš šiame kompiuteryje esančio `DIABDAT.MPQ` ir projekte esančių `assets/data/*.clx`. Tai originalių žaidimo sluoksnių peržiūros, skirtos perpiešimui. Žaidimas šių PNG tiesiogiai nenaudoja; jis tebeskaito CEL/CLX iš MPQ ir projekto duomenų.

- [Inventory](Inventory/): trys veikėjų klasių fonai, inventoriaus daiktų kadrai ir dabartinio 400 × 240 išdėstymo pavyzdys.
- [Stash](Stash/): sandėlio fonas, penki navigacijos mygtukų kadrai ir trijų klasių bendri „stash + inventory“ išdėstymo pavyzdžiai.
- [Bottom_UI](Bottom_UI/): apatinio skydelio fonas, mygtukai, burbulai, burtų bei diržo piktogramos ir 320 × 240 pagrindo pavyzdys.

PNG spalvos išgautos naudojant žaidimo miestelio paletę. Požemiuose smulkūs spalvų skirtumai gali keistis kartu su žaidimo palete.

Failų pavadinimuose `001.png`, `002.png` ir t. t. yra **vienetu pradedami** kadrų numeriai. Juos taip pat rodo bendros `*_atlas.png` peržiūros.

Paveikslėlių ištraukimo skriptas: `tools/extract_3ds_ui_reference.py`. Jam reikia vietinio `DIABDAT.MPQ` ir `mpqfs` dinaminės bibliotekos (`--mpqfs-library`).
