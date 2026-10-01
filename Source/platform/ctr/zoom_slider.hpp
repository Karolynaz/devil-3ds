#pragma once

/**
 * @file zoom_slider.hpp
 *
 * @brief 3DS: the 3D depth slider as a live control for the "World zoom" option.
 */

namespace devilution {

/** @brief True if this 3DS model has a 3D slider (false on a 2DS and a New 2DS XL). Reads the CFG service once. */
bool CtrHasZoomSlider();

/** @brief The 3D slider position, 0.0 (bottom) to 1.0 (top). Always 0 on a model without a slider. */
float CtrReadZoomSlider();

/** @brief True if "Zoom control" is "3D slider" and this model has a 3D slider. */
bool CtrZoomSliderActive();

/**
 * @brief Reads the 3D slider and sets "World zoom" when the level changes (with hysteresis).
 *
 * Call it once per game frame, in game only. It does nothing when the control is "Menu".
 * It does not save the ini; the value is saved with the other options.
 */
void CtrPollZoomSlider();

} // namespace devilution
