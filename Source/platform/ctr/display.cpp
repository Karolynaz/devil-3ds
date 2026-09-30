#include "platform/ctr/display.hpp"
#include <SDL.h>
#include <3ds.h>
#include <citro3d.h>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace devilution {

namespace {
struct CTRPaletteEntry {
	uint8_t r, g, b;
	uint16_t rgb565;
	uint32_t rgba8;
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
		bottomPalette[i].rgba8 = (static_cast<uint32_t>(c.r) << 24) | (static_cast<uint32_t>(c.g) << 16)
		    | (static_cast<uint32_t>(c.b) << 8) | 0xFF;
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
bool nativePresenterSynchronized = false;
bool nativeDualScreenMode = false;

template <int Width, GSPGPU_FramebufferFormat Format>
void BlitFramebuffer(uint8_t *framebuffer, const uint8_t *source, int pitch)
{
	// A hardware framebuffer column is contiguous. Choose the format once,
	// then write sequentially without per-pixel division or format branches.
	constexpr int BytesPerPixel = Format == GSP_BGR8_OES ? 3 : Format == GSP_RGB565_OES ? 2 : 4;
	for (int x = 0; x < Width; ++x) {
		const uint8_t *src = source + x * 640 / Width + 239 * pitch;
		uint8_t *dst = framebuffer + x * 240 * BytesPerPixel;
		for (int y = 0; y < 240; ++y, dst += BytesPerPixel) {
			const CTRPaletteEntry &color = bottomPalette[*src];
			if constexpr (Format == GSP_BGR8_OES) {
				dst[0] = color.b;
				dst[1] = color.g;
				dst[2] = color.r;
			} else if constexpr (Format == GSP_RGB565_OES) {
				*reinterpret_cast<uint16_t *>(dst) = color.rgb565;
			} else {
				*reinterpret_cast<uint32_t *>(dst) = color.rgba8;
			}
			if (y != 239)
				src -= pitch;
		}
	}
}

template <int Width>
void BlitFramebuffer(uint8_t *framebuffer, GSPGPU_FramebufferFormat format, const uint8_t *source, int pitch)
{
	switch (format) {
	case GSP_BGR8_OES: BlitFramebuffer<Width, GSP_BGR8_OES>(framebuffer, source, pitch); break;
	case GSP_RGB565_OES: BlitFramebuffer<Width, GSP_RGB565_OES>(framebuffer, source, pitch); break;
	case GSP_RGBA8_OES: BlitFramebuffer<Width, GSP_RGBA8_OES>(framebuffer, source, pitch); break;
	default: break;
	}
}

} // namespace

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

bool CTR_PresentFrame(const SDL_Surface *surface)
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
	gfxSet3D(false);

	u16 fbW = 0, fbH = 0;
	u8 *fbLeft = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &fbW, &fbH);
	u8 *fbBottom = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, nullptr, nullptr);
	if (fbLeft == nullptr || fbBottom == nullptr)
		return false;

	const GSPGPU_FramebufferFormat topFormat = gfxGetScreenFormat(GFX_TOP);
	const GSPGPU_FramebufferFormat bottomFormat = gfxGetScreenFormat(GFX_BOTTOM);
	if ((topFormat != GSP_BGR8_OES && topFormat != GSP_RGB565_OES && topFormat != GSP_RGBA8_OES)
	    || (bottomFormat != GSP_BGR8_OES && bottomFormat != GSP_RGB565_OES && bottomFormat != GSP_RGBA8_OES))
		return false;
	const uint8_t *srcPixels = static_cast<const uint8_t *>(surface->pixels);
	const int srcPitch = surface->pitch;
	BlitFramebuffer<400>(fbLeft, topFormat, srcPixels, srcPitch);
	BlitFramebuffer<320>(fbBottom, bottomFormat, srcPixels + 240 * srcPitch, srcPitch);
	gfxFlushBuffers();
	gspWaitForVBlank();
	gfxScreenSwapBuffers(GFX_TOP, false);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
	return true;
}

} // namespace devilution
