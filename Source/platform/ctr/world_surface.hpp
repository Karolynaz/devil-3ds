#pragma once

#include "engine/surface.hpp"
#include "platform/ctr/world_view.hpp"

namespace devilution {

/**
 * @brief The 8-bit world buffer, (400 / s) x (240 / s) pixels for the current zoom level.
 *
 * It is (re)allocated here when the "World zoom" option changes.
 */
const Surface &CtrWorldSurface();

/**
 * @brief Starts an in-game top-screen frame. Call it once per frame, before the UI is drawn.
 *
 * Clears rows 0..239 of `out` (the 640 wide UI layer) to CtrWorldKeyIndex and
 * gives the world buffer to the presenter for the next present (one-shot).
 */
void CtrWorldBeginFrame(const Surface &out);

/**
 * @brief Gives the last world buffer to the presenter again, for one more present.
 *
 * For a redraw of the cursor only: the UI layer still has the key pixels from
 * the last full frame. Does nothing before the first CtrWorldBeginFrame.
 */
void CtrWorldRepeatFrame();

/**
 * @brief True if `out` is not the world buffer, that is, it is the UI layer
 * that can hold key pixels. Half-transparent draws use it to write the dim key.
 */
bool CtrIsUiLayer(const Surface &out);

} // namespace devilution
