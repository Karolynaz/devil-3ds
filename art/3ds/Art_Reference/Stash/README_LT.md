# „Stash“ ir inventoriaus perpiešimo nuorodos

- `stash_background_320x352.png` – originalus `assets/data/stash.clx` fonas. Jame jau nupiešti rėmai, penkių mygtukų pagrindai ir 10 × 10 daiktų langelių.
- `nav_*.png` – penki atskiri 27 × 16 paspaustų mygtukų kadrai iš `assets/data/stashnavbtns.clx`. `navigation_states_atlas.png` juos parodo kartu.
- `stash_panel_218x240.png` ir `inventory_*_split_218x240.png` – abiejų langų atskiri sumažinti vaizdai. Jie naudingi piešiant smulkmenas.
- `stash_inventory_*_top_400x240.png` – **dabartinis bendras viršutinio 3DS ekrano išdėstymas** Warrior, Rogue ir Sorcerer klasėms. Kairėje „stash“, dešinėje inventorius. Juodas vidurys šiame pavyzdyje žymi žaidimo vaizdo vietą; pačiame žaidime ten matomas žaidimas.
- `stash_inventory_*_side_by_side_436x240.png` – tie patys du 218 × 240 langai suglausti vienas prie kito, kad būtų patogiau juos perpiešti. Tai nėra tikras ekrano išdėstymas.

Abu langai piešiami iš 320 × 352 vaizdų. Kai jie atverti kartu, kiekvienas sumažinamas iki 218 × 240 žaidimo loginiame 640 × 240 viršutiniame plote. 3DS fiziniame 400 × 240 ekrane kairysis langas užima maždaug x=0–136, dešinysis x=264–400. Šie pavyzdžiai sudėti pagal dabartines kodo koordinates, be emuliatoriaus.

Puslapio numerį ir aukso kiekį piešia kodas virš fono; pavyzdžiuose jų nėra. Daiktai yra atskiri `objcurs` kadrai, jau ištraukti į [Inventory](../Inventory/objcurs_frames/) katalogą. `stash` tinklelio žingsnis šaltinio paveiksle yra 29 px (28 px langelis ir 1 px tarpas), pirmas langelis prasideda ties x=17, y=48.

PNG naudoja tą pačią miestelio paletę kaip ankstesnis inventoriaus grafikos rinkinys. Pradiniai CLX failai yra `assets/data/stash.clx` ir `assets/data/stashnavbtns.clx`.
