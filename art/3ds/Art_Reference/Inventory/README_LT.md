# Inventoriaus grafika

- `inv.png` – Warrior / Barbarian ir numatytasis 320 × 352 fonas.
- `inv_rog.png` – Rogue / Bard fonas.
- `inv_sor.png` – Sorcerer / Monk fonas.
- `inventory_3ds_400x240_layout.png` – dabartinio viršutinio 3DS ekrano **be daiktų** išdėstymo peržiūra. Šoninis turinys suspaudžiamas į 218 × 240 vidurį, išorinis apvadas ištempiamas per 400 × 240. Peržiūra naudoja artimiausio pikselio mastelį; konsolėje galimas nežymus filtravimo skirtumas.
- `objcurs_frames_atlas.png` – viename paveiksle visi 179 inventoriuje naudojamo `objcurs.cel` rinkinio kadrai.
- `objcurs_frames/` – tie patys kadrai atskirais skaidriais PNG.

**Svarbu perpiešiant:** įrangos vietų rėmeliai ir 40 nešiojamų daiktų langelių jau nupiešti `inv*.png` fone. Uždėti ginklai, šarvai, žiedai ir daiktai piešiami iš `objcurs` kadrų. Magiškų daiktų langelių spalvinimą ir pažymėjimo kontūrą sukuria kodas, atskiro paveikslo jiems nėra. Inventoriaus atvaizdavimą žr. `Source/inv.cpp` (`InitInv`, `DrawInv`), o 3DS mastelį – `Source/engine/render/scrollrt.cpp`.

Jei kursi naują 400 × 240 foną, palik daiktų vietas suderintas su esamu 320 × 352 inventoriaus tinkleliu arba kartu reikės keisti jų koordinates kode.

Šiame kompiuteryje rasta pagrindinio Diablo `DIABDAT.MPQ` grafika. Papildomi, tik Hellfire naudojami `objcurs2` kadrai neįtraukti, nes Hellfire MPQ nerastas.
