#include "platform/ctr/display.hpp"
#include <SDL.h>
#include <3ds.h>
#include <citro3d.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

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
			const int hw_x = 239 - y;
			for (int x = 0; x < blitW; ++x) {
				const int hw_y = x;
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
			const int hw_x = 239 - y;
			for (int x = 0; x < blitW; ++x) {
				const int hw_y = x;
				fb16[hw_y * 240 + hw_x] = bottomPalette[srcRow[x]].rgb565;
			}
		}
	} else {
		u32 *fb32 = reinterpret_cast<u32 *>(fb);
		for (int y = 0; y < blitH; ++y) {
			const uint8_t *srcRow = srcPixels + (srcY + y) * srcPitch + srcX;
			const int hw_x = 239 - y;
			for (int x = 0; x < blitW; ++x) {
				const int hw_y = x;
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
bool nativePresenterSynchronized = false;
bool nativeDualScreenMode = false;

void WriteFramebufferPixel(uint8_t *framebuffer, GSPGPU_FramebufferFormat format, int x, int y, const CTRPaletteEntry &color)
{
	// 3DS framebuffers are stored sideways: logical X selects the 240-pixel
	// memory row and logical Y is reversed within it.
	const size_t pixel = static_cast<size_t>(x) * 240 + (239 - y);
	if (format == GSP_BGR8_OES) {
		const size_t offset = pixel * 3;
		framebuffer[offset + 0] = color.b;
		framebuffer[offset + 1] = color.g;
		framebuffer[offset + 2] = color.r;
	} else if (format == GSP_RGB565_OES) {
		reinterpret_cast<u16 *>(framebuffer)[pixel] = color.rgb565;
	} else {
		reinterpret_cast<u32 *>(framebuffer)[pixel] = (static_cast<uint32_t>(color.r) << 24)
		    | (static_cast<uint32_t>(color.g) << 16) | (static_cast<uint32_t>(color.b) << 8) | 0xFF;
	}
}

} // namespace

void CTR_Set3DMode(bool enable)
{
	stereo3dEnabled = enable;
	gfxSet3D(nativeDualScreenMode && enable);
}

bool CTR_Is3DModeEnabled()
{
	return stereo3dEnabled;
}

float CTR_Get3DSlider()
{
	return osGet3DSliderState();
}

void CTR_ConfigureFramePresenter(bool dualScreen)
{
	// SDL_SetVideoMode has joined the old renderer thread and deleted its
	// targets. The new worker is idle until SDL_Flip signals it.
	nativeDualScreenMode = dualScreen;
	nativePresenterSynchronized = false;
	gfxSet3D(false);
	if (dualScreen && C3D_FrameBegin(0)) {
		C3D_FrameEnd(GX_CMDLIST_FLUSH);
		nativePresenterSynchronized = true;
	}
}

bool CTR_PresentFrame(const SDL_Surface *surface, bool panelsOpen)
{
	if (!gspHasGpuRight())
		return true;
	// Initial palette setup can precede the first UI frame. Keep SDL
	// inactive until it is ready, rather than waking its GPU worker.
	if (nativeDualScreenMode && !paletteInitialized)
		return true;
	if (!nativeDualScreenMode || surface == nullptr || surface->pixels == nullptr || surface->format == nullptr
	    || surface->format->BitsPerPixel != 8 || surface->w != 640 || surface->h != 480
	    || surface->pitch < surface->w || !paletteInitialized) {
		// Citro3D's regular SDL path presents a flat frame in this case.
		gfxSet3D(false);
		nativePresenterSynchronized = false;
		return false;
	}
	if (!nativePresenterSynchronized)
		return false;
	gfxSet3D(stereo3dEnabled);

	u16 fbW = 0, fbH = 0;
	u8 *fbLeft = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbW, &fbH);
	u8 *fbRight = stereo3dEnabled ? gfxGetFramebuffer(GFX_TOP, GFX_RIGHT, nullptr, nullptr) : nullptr;
	u8 *fbBottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr);
	if (fbLeft == nullptr || fbBottom == nullptr || (stereo3dEnabled && fbRight == nullptr))
		return false;

	const GSPGPU_FramebufferFormat topFormat = gfxGetScreenFormat(GFX_TOP);
	const GSPGPU_FramebufferFormat bottomFormat = gfxGetScreenFormat(GFX_BOTTOM);
	if ((topFormat != GSP_BGR8_OES && topFormat != GSP_RGB565_OES && topFormat != GSP_RGBA8_OES)
	    || (bottomFormat != GSP_BGR8_OES && bottomFormat != GSP_RGB565_OES && bottomFormat != GSP_RGBA8_OES))
		return false;
	const uint8_t *srcPixels = static_cast<const uint8_t *>(surface->pixels);
	const int srcPitch = surface->pitch;
	const float slider = stereo3dEnabled ? osGet3DSliderState() : 0.0f;
	const float maxSeparation = panelsOpen ? 0.0f : 5.0f * slider;
	for (int y = 0; y < 240; ++y) {
		const uint8_t *srcRow = srcPixels + y * srcPitch;
		const int shift = static_cast<int>(std::lround((120.0f - y) / 120.0f * maxSeparation));
		for (int x = 0; x < 400; ++x) {
			const int srcX = (x * surface->w) / 400;
			const CTRPaletteEntry &leftColor = bottomPalette[srcRow[srcX]];
			WriteFramebufferPixel(fbLeft, topFormat, x, y, leftColor);
			if (fbRight != nullptr) {
				int rightShift = (y >= 180 || (y < 22 && x >= 80 && x <= 320)) ? 0 : shift;
				if (panelsOpen)
					rightShift = 0;
				const int shiftedX = std::clamp(x - rightShift, 0, 399);
				const CTRPaletteEntry &rightColor = bottomPalette[srcRow[(shiftedX * surface->w) / 400]];
				WriteFramebufferPixel(fbRight, topFormat, x, y, rightColor);
			}
		}
	}
	for (int y = 0; y < 240; ++y) {
		const uint8_t *srcRow = srcPixels + (240 + y) * srcPitch;
		for (int x = 0; x < 320; ++x) {
			const int srcX = (x * surface->w) / 320;
			WriteFramebufferPixel(fbBottom, bottomFormat, x, y, bottomPalette[srcRow[srcX]]);
		}
	}
	gfxFlushBuffers();
	gspWaitForVBlank();
	gfxScreenSwapBuffers(GFX_TOP, stereo3dEnabled);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
	return true;
}

} // namespace devilution
