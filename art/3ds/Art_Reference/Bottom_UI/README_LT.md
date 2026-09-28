# Apatinio ekrano grafika

- `panel8.png` – originalus 640 × 144 valdymo skydelio fonas. **Raudonas ir mėlynas pripildyti burbulai jau nupiešti šiame fone.**
- `bottom_screen_320x240_base_layout.png` – tik bazinis 320 × 240 ekrano išdėstymas: skydelis piešiamas apačioje, kiti žaidimo sluoksniai ir tekstai neįtraukti.
- `p8bulbs_frames/001.png` ir `002.png` – atitinkamai **tuščias gyvybės** ir **tuščias manos** burbulas (88 × 88).
- `globe_fill_examples.png` – viršutinė eilė: gyvybė; apatinė: mana. Kairėje 0, viduryje maždaug pusė, dešinėje pilnas burbulas.
- `panel8bu_frames/` – paspaustų pagrindinių mygtukų kadrai.
- `panel8buc_frames/`, `panel8bucp_frames/`, `dirtybuc_frames/`, `dirtybucp_frames/` – papildomi mygtukų paviršiai ir tekstų užtamsinimas; tai projekto CLX failai.
- `spelicon_frames/` – burtų pasirinkimo piktogramos (51 kadras); spalvą gali keisti žaidimo paletės transformacija.
- `belt_item_frames/` ir `belt_item_frames_atlas.png` – tie patys `objcurs.cel` kadrai, iš kurių piešiami diržo daiktai.
- `charbut_frames/` – veikėjo taškų paskirstymo mygtuko kadrai.
- `duricons_frames/` – įrangos nusidėvėjimo piktogramos.
- `xpbar_frames/` – patirties juostos rėmelis; **užpildymo ilgį ir spalvą piešia kodas**.
- `p8but2_frames/`, `talkbutt_frames/`, `talkbutton_frames/`, `talkpanl.png` – kelių žaidėjų režimo mygtukai ir skydelis.

## Kaip pilnėja burbulai

Gyvybės ir manos kiekis paverčiamas į užpildymo aukštį. Kodas paima **tuščio** burbulo viršutinę dalį iš `p8bulbs` ir uždeda tiek jos eilučių ant `panel8` fone esančio **pilno** burbulo, kiek trūksta iki dabartinio kiekio. Taip gaunamas slenkantis skysčio paviršius. Apatinė ir virš rėmo išsikišusi burbulo dalys tvarkomos atskirai. Naujo skysčio piešinys kaskart negeneruojamas: kodas keičia, kurios iš jau nupieštų eilučių matomos. Žr. `Source/control/control_flasks.cpp` ir `Source/control/control_panel.cpp`.

Mygtukų užrašus, gyvybės / manos skaičius, diržo daiktų kiekius ir kai kuriuos kontūrus piešia kodas; jiems nėra atskiro vientiso paveikslo.
