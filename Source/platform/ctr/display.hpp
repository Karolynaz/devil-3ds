#pragma once

#include <cstdint>
#include <SDL.h>

namespace devilution {

uint32_t Get3DSScalingFlag(bool fitToScreen, int width, int height);

void CTR_UpdateBottomPalette(const SDL_Color *palette);
void CTR_BlitBottomScreen(const uint8_t *srcPixels, int srcPitch, int srcX, int srcY, int width, int height);
void CTR_PresentBottomScreen();
void CTR_ClearBottomScreen();

/**
 * @brief The world buffer for one present of the top screen.
 *
 * The top screen shows the world in each pixel where the UI layer (the main
 * surface) has the key index CtrWorldKeyIndex (world_view.hpp).
 */
struct CtrWorldFrame {
	/** @brief First pixel of the 8-bit world buffer. */
	const uint8_t *pixels;
	int pitch;
	/** @brief Zoom level (index in CtrZoomScales). The world size follows from it. */
	int level;
};

/** @brief Sets the world for the next CTR_PresentFrame only (one-shot). A frame without it is a plain UI frame. */
void CTR_SetWorldFrame(const CtrWorldFrame &frame);

void CTR_ConfigureFramePresenter(bool dualScreen);
bool CTR_PresentFrame(const SDL_Surface *surface);

} // namespace devilution
