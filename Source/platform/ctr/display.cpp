#include "platform/ctr/display.hpp"
#include <SDL.h>
#include <3ds.h>
#include <algorithm>
#include <cstdint>

namespace {
struct CTRPaletteEntry {
	uint8_t r, g, b;
	uint16_t rgb565;
};

CTRPaletteEntry bottomPalette[256];
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

	for (int i = 0; i < 256; ++i) {
		const SDL_Color &c = palette[i];
		bottomPalette[i].r = c.r;
		bottomPalette[i].g = c.g;
		bottomPalette[i].b = c.b;

		const uint16_t r = c.r >> 3;
		const uint16_t g = c.g >> 2;
		const uint16_t b = c.b >> 3;
		bottomPalette[i].rgb565 = (r << 11) | (g << 5) | b;
	}
	paletteInitialized = true;
}

void CTR_BlitBottomScreen(const uint8_t *srcPixels, int srcPitch, int srcX, int srcY, int width, int height)
{
	if (srcPixels == nullptr || !paletteInitialized)
		return;

	u16 fbW = 0, fbH = 0;
	u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fbW, &fbH);
	if (fb == nullptr)
		return;

	const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(GFX_BOTTOM);
	const int blitW = std::min(width, 320);
	const int blitH = std::min(height, 240);

	if (format == GSP_BGR8_OES) {
		for (int y = 0; y < blitH; ++y) {
			const uint8_t *srcRow = srcPixels + (srcY + y) * srcPitch + srcX;
			const int hw_x = y;
			for (int x = 0; x < blitW; ++x) {
				const int hw_y = 319 - x;
				const size_t offset = (static_cast<size_t>(hw_y) * 240 + hw_x) * 3;
				const CTRPaletteEntry &entry = bottomPalette[srcRow[x]];
				fb[offset + 0] = entry.b;
				fb[offset + 1] = entry.g;
				fb[offset + 2] = entry.r;
			}
		}
	} else if (format == GSP_RGB565_OES) {
		u16 *fb16 = reinterpret_cast<u16 *>(fb);
		for (int y = 0; y < blitH; ++y) {
			const uint8_t *srcRow = srcPixels + (srcY + y) * srcPitch + srcX;
			const int hw_x = y;
			for (int x = 0; x < blitW; ++x) {
				const int hw_y = 319 - x;
				fb16[hw_y * 240 + hw_x] = bottomPalette[srcRow[x]].rgb565;
			}
		}
	} else {
		u32 *fb32 = reinterpret_cast<u32 *>(fb);
		for (int y = 0; y < blitH; ++y) {
			const uint8_t *srcRow = srcPixels + (srcY + y) * srcPitch + srcX;
			const int hw_x = y;
			for (int x = 0; x < blitW; ++x) {
				const int hw_y = 319 - x;
				const CTRPaletteEntry &entry = bottomPalette[srcRow[x]];
				fb32[hw_y * 240 + hw_x] = (static_cast<uint32_t>(entry.r) << 24)
				                        | (static_cast<uint32_t>(entry.g) << 16)
				                        | (static_cast<uint32_t>(entry.b) << 8)
				                        | 0xFF;
			}
		}
	}
}

void CTR_PresentBottomScreen()
{
	gfxFlushBuffers();
}
