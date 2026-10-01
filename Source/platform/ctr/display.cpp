#include "platform/ctr/display.hpp"
#include "platform/ctr/ui_geometry.hpp"
#include "platform/ctr/world_view.hpp"
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
// Packed channels for the world interpolation: red in bits 0..7 and blue in
// bits 16..23 (so one multiply scales both), green alone.
uint32_t paletteRedBlue[256];
uint32_t paletteGreen[256];
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
		paletteRedBlue[i] = static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.b) << 16);
		paletteGreen[i] = c.g;
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
		const uint8_t *src = source + CtrPresenterSourceColumn(x, Width) + 239 * pitch;
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

// The world for the next present (one-shot), see CTR_SetWorldFrame.
bool worldFramePending = false;
CtrWorldFrame worldFrame {};
// Tables of the zoom level. Built when the level changes.
CtrWorldSampling worldSampling {};
int worldSamplingLevel = -1;

/** @brief The two world rows and the vertical weight of one screen row. Built for each frame. */
struct WorldRow {
	const uint8_t *row0;
	const uint8_t *row1;
	uint32_t weight1;
};
WorldRow worldRows[CtrScreenHeight];

template <GSPGPU_FramebufferFormat Format>
inline void StoreColor(uint8_t *dst, uint32_t r, uint32_t g, uint32_t b)
{
	if constexpr (Format == GSP_BGR8_OES) {
		dst[0] = static_cast<uint8_t>(b);
		dst[1] = static_cast<uint8_t>(g);
		dst[2] = static_cast<uint8_t>(r);
	} else if constexpr (Format == GSP_RGB565_OES) {
		*reinterpret_cast<uint16_t *>(dst) = static_cast<uint16_t>(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
	} else {
		*reinterpret_cast<uint32_t *>(dst) = (r << 24) | (g << 16) | (b << 8) | 0xFF;
	}
}

template <GSPGPU_FramebufferFormat Format>
inline void StorePaletteColor(uint8_t *dst, uint8_t index)
{
	const CTRPaletteEntry &color = bottomPalette[index];
	if constexpr (Format == GSP_BGR8_OES) {
		dst[0] = color.b;
		dst[1] = color.g;
		dst[2] = color.r;
	} else if constexpr (Format == GSP_RGB565_OES) {
		*reinterpret_cast<uint16_t *>(dst) = color.rgb565;
	} else {
		*reinterpret_cast<uint32_t *>(dst) = color.rgba8;
	}
}

/**
 * @brief Top screen with the world (CtrWorldKeyIndex pixels of the UI layer show the world).
 *
 * A UI pixel is written as in BlitFramebuffer. A key pixel is the world at
 * (x / s, y / s): the nearest world pixel when !Bilinear (100%, exact 1:1), else
 * a bilinear blend of 4 world pixels in RGB. The inner loop has no division
 * and no floating point: the column and row indexes and weights come from the
 * tables (8 bit fraction); the red and blue channels share one multiply.
 */
template <GSPGPU_FramebufferFormat Format, bool Bilinear>
void BlitTopWithWorld(uint8_t *framebuffer, const uint8_t *source, int pitch)
{
	constexpr int Width = CtrScreenWidth;
	constexpr int Height = CtrScreenHeight;
	constexpr int BytesPerPixel = Format == GSP_BGR8_OES ? 3 : Format == GSP_RGB565_OES ? 2 : 4;
	for (int x = 0; x < Width; ++x) {
		const uint8_t *src = source + CtrPresenterSourceColumn(x, Width) + (Height - 1) * pitch;
		uint8_t *dst = framebuffer + x * Height * BytesPerPixel;
		const int column0 = worldSampling.columns[x].i0;
		[[maybe_unused]] const int column1 = worldSampling.columns[x].i1;
		[[maybe_unused]] const uint32_t weightX1 = worldSampling.columns[x].w;
		[[maybe_unused]] const uint32_t weightX0 = CtrWeightOne - weightX1;
		// The framebuffer column starts at the bottom screen row.
		for (int y = 0; y < Height; ++y, dst += BytesPerPixel) {
			const uint8_t ui = *src;
			if (ui != CtrWorldKeyIndex && ui != CtrWorldDimKeyIndex) {
				StorePaletteColor<Format>(dst, ui);
			} else {
				const WorldRow &row = worldRows[Height - 1 - y];
				if constexpr (!Bilinear) {
					const uint32_t index = row.row0[column0];
					if (ui == CtrWorldKeyIndex) {
						StorePaletteColor<Format>(dst, index);
					} else {
						const uint32_t redBlue = CtrDimRedBlue(paletteRedBlue[index]);
						StoreColor<Format>(dst, redBlue & 0xFF, CtrDimChannel(paletteGreen[index]), redBlue >> 16);
					}
				} else {
					const uint32_t a = row.row0[column0];
					const uint32_t b = row.row0[column1];
					const uint32_t c = row.row1[column0];
					const uint32_t d = row.row1[column1];
					const uint32_t weightY1 = row.weight1;
					const uint32_t weightY0 = CtrWeightOne - weightY1;
					const uint32_t topRedBlue = ((paletteRedBlue[a] * weightX0 + paletteRedBlue[b] * weightX1) >> 8) & 0x00FF00FFu;
					const uint32_t bottomRedBlue = ((paletteRedBlue[c] * weightX0 + paletteRedBlue[d] * weightX1) >> 8) & 0x00FF00FFu;
					uint32_t redBlue = ((topRedBlue * weightY0 + bottomRedBlue * weightY1) >> 8) & 0x00FF00FFu;
					const uint32_t topGreen = (paletteGreen[a] * weightX0 + paletteGreen[b] * weightX1) >> 8;
					const uint32_t bottomGreen = (paletteGreen[c] * weightX0 + paletteGreen[d] * weightX1) >> 8;
					uint32_t green = (topGreen * weightY0 + bottomGreen * weightY1) >> 8;
					if (ui != CtrWorldKeyIndex) {
						redBlue = CtrDimRedBlue(redBlue);
						green = CtrDimChannel(green);
					}
					StoreColor<Format>(dst, redBlue & 0xFF, green, redBlue >> 16);
				}
			}
			if (y != Height - 1)
				src -= pitch;
		}
	}
}

template <bool Bilinear>
void BlitTopWithWorld(uint8_t *framebuffer, GSPGPU_FramebufferFormat format, const uint8_t *source, int pitch)
{
	switch (format) {
	case GSP_BGR8_OES: BlitTopWithWorld<GSP_BGR8_OES, Bilinear>(framebuffer, source, pitch); break;
	case GSP_RGB565_OES: BlitTopWithWorld<GSP_RGB565_OES, Bilinear>(framebuffer, source, pitch); break;
	case GSP_RGBA8_OES: BlitTopWithWorld<GSP_RGBA8_OES, Bilinear>(framebuffer, source, pitch); break;
	default: break;
	}
}

/** @brief Builds the tables for the zoom level (when it changes) and the world rows of this frame. */
void PrepareWorldFrame(const CtrWorldFrame &frame)
{
	if (worldSamplingLevel != frame.level) {
		CtrBuildWorldSampling(worldSampling, frame.level);
		worldSamplingLevel = frame.level;
	}
	for (int y = 0; y < CtrScreenHeight; ++y) {
		const CtrAxisSample &sample = worldSampling.rows[y];
		worldRows[y].row0 = frame.pixels + sample.i0 * frame.pitch;
		worldRows[y].row1 = frame.pixels + sample.i1 * frame.pitch;
		worldRows[y].weight1 = sample.w;
	}
}

} // namespace

void CTR_SetWorldFrame(const CtrWorldFrame &frame)
{
	if (frame.pixels == nullptr || frame.level < 0 || frame.level >= CtrZoomLevelCount)
		return;
	worldFrame = frame;
	worldFramePending = true;
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

bool CTR_PresentFrame(const SDL_Surface *surface)
{
	// The world is for this present only. A later frame (for example a menu)
	// without a new world is a plain UI frame, as before.
	const bool hasWorld = worldFramePending;
	worldFramePending = false;
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
	if (hasWorld) {
		PrepareWorldFrame(worldFrame);
		if (worldFrame.level == static_cast<int>(CtrWorldZoom::Percent100))
			BlitTopWithWorld<false>(fbLeft, topFormat, srcPixels, srcPitch);
		else
			BlitTopWithWorld<true>(fbLeft, topFormat, srcPixels, srcPitch);
	} else {
		BlitFramebuffer<400>(fbLeft, topFormat, srcPixels, srcPitch);
	}
	BlitFramebuffer<320>(fbBottom, bottomFormat, srcPixels + 240 * srcPitch, srcPitch);
	gfxFlushBuffers();
	gspWaitForVBlank();
	gfxScreenSwapBuffers(GFX_TOP, false);
	gfxScreenSwapBuffers(GFX_BOTTOM, false);
	return true;
}

} // namespace devilution
