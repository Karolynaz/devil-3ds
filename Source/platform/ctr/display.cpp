#include "platform/ctr/display.hpp"
#include <SDL.h>
#include <3ds.h>
#include <algorithm>
#include <cstdint>

namespace {
uint16_t palette565[256];
bool paletteInitialized = false;
} // namespace

uint32_t Get3DSScalingFlag(bool fitToScreen, int width, int height)
{
	if (fitToScreen)
		return SDL_FULLSCREEN;
	if (width * 3 < height * 5)
		return SDL_FITHEIGHT;
	return SDL_FITWIDTH;
}

void CTR_UpdateBottomPalette(const SDL_Color *palette)
{
	if (palette == nullptr)
		return;

	const GSPGPU_FramebufferFormats format = gfxGetScreenFormat(GFX_BOTTOM);
	const bool isBGR = (format == GSP_BGR565_OES);

	for (int i = 0; i < 256; ++i) {
		const SDL_Color &c = palette[i];
		const uint16_t r = c.r >> 3;
		const uint16_t g = c.g >> 2;
		const uint16_t b = c.b >> 3;

		if (isBGR) {
			palette565[i] = (b << 11) | (g << 5) | r;
		} else {
			palette565[i] = (r << 11) | (g << 5) | b;
		}
	}
	paletteInitialized = true;
}

void CTR_BlitBottomScreen(const uint8_t *srcPixels, int srcPitch, int srcX, int srcY, int width, int height)
{
	if (srcPixels == nullptr || !paletteInitialized)
		return;

	u16 fbW = 0, fbH = 0;
	u16 *fb = reinterpret_cast<u16 *>(gfxGetFramebuffer(GFX_BOTTOM, GFX_BOTTOM, &fbW, &fbH));
	if (fb == nullptr)
		return;

	const int blitW = std::min(width, 320);
	const int blitH = std::min(height, 240);

	for (int y = 0; y < blitH; ++y) {
		const uint8_t *srcRow = srcPixels + (srcY + y) * srcPitch + srcX;
		const int hw_x = y;
		for (int x = 0; x < blitW; ++x) {
			const int hw_y = 319 - x;
			fb[hw_y * 240 + hw_x] = palette565[srcRow[x]];
		}
	}
}

void CTR_PresentBottomScreen()
{
	gfxFlushBuffers();
}
