#include "platform/ctr/display.hpp"
#include <SDL.h>
#include <3ds.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace devilution {

namespace {
struct CTRPaletteEntry {
	uint8_t r, g, b;
	uint16_t rgb565;
};

CTRPaletteEntry bottomPalette[256];
bool paletteInitialized = false;
} // namespace

#ifndef SDL_FITWIDTH
#define SDL_FITWIDTH 0x00400000
#endif
#ifndef SDL_FITHEIGHT
#define SDL_FITHEIGHT 0x00800000
#endif

uint32_t Get3DSScalingFlag(bool fitToScreen, int width, int height)
{
	return SDL_FITWIDTH | SDL_FITHEIGHT;
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

void CTR_ClearBottomScreen()
{
	u16 fbW = 0, fbH = 0;
	u8 *fb = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &fbW, &fbH);
	if (fb == nullptr)
		return;

	const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(GFX_BOTTOM);
	const size_t bytesPerPixel = (format == GSP_RGB565_OES) ? 2 : ((format == GSP_BGR8_OES) ? 3 : 4);
	const size_t size = 320 * 240 * bytesPerPixel;
	memset(fb, 0, size);
	gfxFlushBuffers();
}

namespace {
bool stereo3dEnabled = false;
} // namespace

void CTR_Set3DMode(bool enable)
{
	stereo3dEnabled = enable;
	gfxSet3D(enable);
}

bool CTR_Is3DModeEnabled()
{
	return stereo3dEnabled;
}

float CTR_Get3DSlider()
{
	return osGet3DSliderState();
}

void CTR_PresentStereoRightEye(const SDL_Surface *surface, bool panelsOpen)
{
	if (surface == nullptr || !paletteInitialized || !stereo3dEnabled)
		return;

	const float slider = osGet3DSliderState();
	if (slider <= 0.001f)
		return;

	u16 fbW = 0, fbH = 0;
	u8 *fbRight = gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, &fbW, &fbH);
	if (fbRight == nullptr)
		return;

	const GSPGPU_FramebufferFormat format = gfxGetScreenFormat(GFX_TOP);
	const int srcWidth = surface->w;
	const int topH = std::min(surface->h, 240);
	const uint8_t *srcPixels = static_cast<const uint8_t *>(surface->pixels);
	const int srcPitch = surface->pitch;

	const float maxSeparation = panelsOpen ? 0.0f : (5.0f * slider);

	if (format == GSP_BGR8_OES) {
		for (int y = 0; y < topH; ++y) {
			const float normalized = (120.0f - static_cast<float>(y)) / 120.0f;
			const int shift = static_cast<int>(std::lround(normalized * maxSeparation));
			const uint8_t *srcRow = srcPixels + y * srcPitch;
			const int hw_x = y;
			for (int x = 0; x < 400; ++x) {
				int effShift = shift;
				if (y < 22 && x >= 80 && x <= 320)
					effShift = 0;

				const int shiftedX = std::clamp(x - effShift, 0, 399);
				const int srcX = (shiftedX * srcWidth) / 400;
				const int hw_y = 399 - x;
				const size_t offset = (static_cast<size_t>(hw_y) * 240 + hw_x) * 3;
				const CTRPaletteEntry &entry = bottomPalette[srcRow[srcX]];
				fbRight[offset + 0] = entry.b;
				fbRight[offset + 1] = entry.g;
				fbRight[offset + 2] = entry.r;
			}
		}
	} else if (format == GSP_RGB565_OES) {
		u16 *fb16 = reinterpret_cast<u16 *>(fbRight);
		for (int y = 0; y < topH; ++y) {
			const float normalized = (120.0f - static_cast<float>(y)) / 120.0f;
			const int shift = static_cast<int>(std::lround(normalized * maxSeparation));
			const uint8_t *srcRow = srcPixels + y * srcPitch;
			const int hw_x = y;
			for (int x = 0; x < 400; ++x) {
				int effShift = shift;
				if (y < 22 && x >= 80 && x <= 320)
					effShift = 0;

				const int shiftedX = std::clamp(x - effShift, 0, 399);
				const int srcX = (shiftedX * srcWidth) / 400;
				const int hw_y = 399 - x;
				fb16[hw_y * 240 + hw_x] = bottomPalette[srcRow[srcX]].rgb565;
			}
		}
	} else {
		u32 *fb32 = reinterpret_cast<u32 *>(fbRight);
		for (int y = 0; y < topH; ++y) {
			const float normalized = (120.0f - static_cast<float>(y)) / 120.0f;
			const int shift = static_cast<int>(std::lround(normalized * maxSeparation));
			const uint8_t *srcRow = srcPixels + y * srcPitch;
			const int hw_x = y;
			for (int x = 0; x < 400; ++x) {
				int effShift = shift;
				if (y < 22 && x >= 80 && x <= 320)
					effShift = 0;

				const int shiftedX = std::clamp(x - effShift, 0, 399);
				const int srcX = (shiftedX * srcWidth) / 400;
				const int hw_y = 399 - x;
				const CTRPaletteEntry &entry = bottomPalette[srcRow[srcX]];
				fb32[hw_y * 240 + hw_x] = (static_cast<uint32_t>(entry.r) << 24)
				                        | (static_cast<uint32_t>(entry.g) << 16)
				                        | (static_cast<uint32_t>(entry.b) << 8)
				                        | 0xFF;
			}
		}
	}
	gfxFlushBuffers();
}

} // namespace devilution
