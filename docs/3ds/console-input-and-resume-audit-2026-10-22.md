# Console input and sleep/wake audit

Source baseline: `a976cd7` (3.27). This is a code audit plus native host regression tests; it does not claim testing on a physical 3DS or emulator.

## Changes agreed with the player

- (X), (Y), (L), (R) each cast once per press, including the primary spell. No button requires a mouse click afterward for ordinary combat spells.
- Offensive spells choose a nearby visible enemy with an unobstructed missile path. Teleport, Golem, walls and runes instead use the hero's facing direction; Teleport uses four tiles, placement spells one tile. Their existing engine placement/collision checks still apply.
- In the visual shop, (Y) requests a sale and displays the item and price. (A) confirms; (B) cancels. Buying/selecting remains on (A).

## Confirmed defects fixed

| Area | Defect | Correction |
| --- | --- | --- |
| Casting | Holding (X) repeated casts; the other three bindings were single use. | Four buttons share the same single-use dispatch; the primary cast no longer leaves a repeating spell action. |
| Placement | Ground spells could inherit a monster/player focus despite needing a location. | Clear actor focus and derive the tile from facing before dispatch. |
| Cooperative spells | Heal Other/Resurrect depended on the primary spell's previous actor selection. | Select an eligible nearby live/dead ally using the actual spell, active level, visibility and missile path. Reject a missing ally before quick casting. |
| Visual shop | The 3DS inventory early return intercepted (A) over the vendor grid/buttons. | Dispatch the vendor interaction before the generic inventory branch. |
| Visual shop | (Y) could run the normal inventory ground-drop action. | Route it to the vendor sale flow. Holding (A) no longer sells or uses an inventory item while browsing the vendor. |
| Visual shop | Additional stock pages existed without any caller for their page functions. | (L)/(R) change vendor pages; show a page indicator. |
| Sales | Gold capacity was unchecked and the cached gold total was not updated. | Check capacity including the sold item's released cells before removing it; add the actual credited amount to the gold total. |
| Sales | Confirmation could refer to a different item after inventory changes. | Revalidate item identity, price, vendor and inventory index. Cancel/second confirm cannot sell again. |
| World item spells | The controller's world (A) path bypassed the special-cursor action used by mouse right click. | (A) completes the selected item/spell action; (B) cancels that mode. This covers the existing Telekinesis selection phase. |
| Help | The 3DS help still instructed players to right click, press keyboard keys and open the speedbook. | Use console controls and the current four-binding flow. |
| Resume input | A pre-sleep hold or drag could remain active across a large jump in elapsed time. | Cancel holds and touch gestures without executing drop/use/sale release callbacks; clear held actions and stick state after the SDK pump returns. Preserve carried items. |
| Resume presentation | Native presenter synchronization and its pending scene reference survived a lifecycle transition. | Invalidate these states and retry GPU synchronization without an indefinite queue wait. Keep an unavailable frame in the native path rather than sending it to SDL's renderer worker. |
| Resume timing | Pointer integration multiplied the entire sleep interval by the last stick value. | Bound each integration interval to 100 ms; reset the single-player simulation clock on resume. Do not reset multiplayer synchronization time. |

## Existing paths checked

- **Movement:** Circle Pad feeds one-tile directional walking, with collision/corner checks. Ground touch does not invoke desktop click-to-walk. Actor/NPC interaction uses the existing pathfinder, so walking to talk is still possible without a mouse. Wall sliding/automatic door opening would be a gameplay choice; neither was introduced here.
- **Doors, chests, pickups:** (A) dispatches the focused nearby interaction. Auto pickup options remain independent. The existing item-first priority in combat needs the player's decision (see below).
- **Inventory, stash and belt:** Circle Pad supplies the panel cursor; tap (A) lifts/places, hold (A) uses/equips, and the existing stash transfer gesture remains separate. Gold splitting and withdrawals already use the native numeric keyboard. Oil/Identify/Repair/Recharge use the panel selection phase and (A); they do not require a physical right button.
- **Vendor text menus:** controller confirmation dispatches the selected line, not a screen coordinate. Dialogue cancellation uses (B).
- **Belt touchscreen:** existing long-hold use and drag/swap regression tests still pass. Canceling a gesture on resume does not remove a belt item.

## Questions / remaining design work

1. **Combat vs loot priority:** a nearby item can currently suppress the highlighted enemy. Suggested behavior: prefer an immediately threatening enemy in front, then collect loot after combat. Awaiting the player's decision.
2. **Telekinesis:** its existing interaction remains two-stage: cast, then (A) on the focused target. Long-range item/object selection needs a dedicated targeting rule or UI; automatically opening a trapped chest is not an obvious safe default.
3. **Party inspection:** the upstream party portrait inspection is reached from right click, but the world cursor is intentionally hidden on 3DS. A controller-friendly party list would be clearer than exposing mouse simulation. No new gesture was invented in this audit.
4. **Ground spell distance:** facing-based placement is implemented as agreed. A later optional precise targeting mode could improve tactical wall/rune placement, but it would need a UI and an explicit confirmation/cancel gesture.

## Sleep/wake investigation and limits

The original local APT hook did no game recovery on sleep and only toggled the backlight on wake/restore. No local free/reallocate of gameplay buffers was found on that path, so an application memory leak has **not** been established as the cause of the reported intermittent hang.

The SDK already owns DSP sleep/wake and GPU service restoration. Do not call `ndspExit`/`ndspInit`, reopen SDL audio, or synchronously join game/network workers from an APT hook. In particular, the APT restore hook can run before the SDK's DSP wake finishes. Recovery is deferred until after `SDL_PollEvent`/`aptMainLoop` returns, including a poll that produces no event. A resume counter is written through the normal application logger.

Primary source review:

- [libctru APT implementation](https://github.com/devkitPro/libctru/blob/master/libctru/source/services/apt.c): `aptHandleSleep`, `aptWaitForWakeUp`, DSP sleep/wake ordering.
- [libctru NDSP implementation](https://github.com/devkitPro/libctru/blob/master/libctru/source/ndsp/ndsp.c): SDK-managed DSP component restore and wait events.
- [Official 3DS SDL patch](https://github.com/devkitPro/pacman-packages/blob/master/3ds/SDL/SDL-1.2.15.patch): event pumping and audio buffer waits.
- [Citro3D render queue](https://github.com/devkitPro/citro3d/blob/master/source/renderqueue.c): nonblocking queue synchronization versus synchronous frame/VBlank waits.

Hardware follow-up is still required to determine whether the reported freeze is in SDK audio restoration, graphics, storage or networking. Useful observations: single/multiplayer, frozen last frame vs black screen, whether audio continues, and whether HOME responds. Repeat close/open while walking, casting, dragging a belt item, browsing shops and playing a movie; also test a longer sleep. Host tests cannot reproduce DSP/GSP firmware behavior or prove that this intermittent hardware hang is fixed.

## Regression checks

- `tools/tests/test_ctr_resume.py`: 1,000 wake cycles through the production hooks/recovery, no release callbacks, clock preservation in multiplayer, bounded pointer integration and tick wrap; ASan/UBSan.
- `tools/tests/test_ctr_store_sales.py`: production request/confirm handlers, cancel/replay, changed item/vendor, capacity, held item and exact gold accounting; ASan/UBSan.
- `tools/tests/test_ctr_spell_targeting.py`: production offensive/ally targeting, ground placement and shared cast dispatch; ASan/UBSan.
- `tools/tests/test_ctr_presenter.py`: production native presenter, unavailable GPU, nonblocking retry, wake invalidation, new scene composition and pixel formats.
- Existing HUD touch, spell bindings, towner targeting and gameplay option tests.
