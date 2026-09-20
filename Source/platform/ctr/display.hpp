#pragma once

#include <cstdint>
#include <SDL.h>

uint32_t Get3DSScalingFlag(bool fitToScreen, int width, int height);

void CTR_UpdateBottomPalette(const SDL_Color *palette);
void CTR_BlitBottomScreen(const uint8_t *srcPixels, int srcPitch, int srcX, int srcY, int width, int height);
void CTR_PresentBottomScreen();
