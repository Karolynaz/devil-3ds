#include "platform/ctr/ui_background.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>

#include "engine/assets.hpp"
#include "engine/palette.h"
#include "engine/render/primitive_render.hpp"

namespace devilution {
namespace {

constexpr int BackgroundWidth = 400;
constexpr int BackgroundHeight = 240;
constexpr size_t BackgroundRgbBytes = BackgroundWidth * BackgroundHeight * 3;

std::optional<OwnedSurface> Backgrounds[2];
bool BackgroundLoadAttempted[2] = {};

void LoadBackground(size_t index)
{
	BackgroundLoadAttempted[index] = true;
	constexpr std::array<const char *, 2> Paths {
	    "data\\ctr_ui_background.rgb",
	    "data\\ctr_quest_background.rgb",
	};
	auto image = LoadAsset(Paths[index]);
	if (!image || image->size != BackgroundRgbBytes)
		return;

	Backgrounds[index].emplace(BackgroundWidth, BackgroundHeight);
	const auto *src = reinterpret_cast<const uint8_t *>(image->data.get());
	for (int y = 0; y < BackgroundHeight; ++y) {
		uint8_t *dst = Backgrounds[index]->at(0, y);
		for (int x = 0; x < BackgroundWidth; ++x) {
			const int r = *src++;
			const int g = *src++;
			const int b = *src++;
			if ((r | g | b) == 0) {
				dst[x] = 0;
				continue;
			}
			// Indices 128–255 remain the same across dungeon palettes.
			unsigned bestDistance = std::numeric_limits<unsigned>::max();
			uint8_t best = 128;
			for (unsigned color = 128; color < 256; ++color) {
				const SDL_Color &candidate = logical_palette[color];
				const int dr = r - candidate.r;
				const int dg = g - candidate.g;
				const int db = b - candidate.b;
				const unsigned distance = dr * dr + dg * dg + db * db;
				if (distance < bestDistance) {
					bestDistance = distance;
					best = static_cast<uint8_t>(color);
				}
			}
			dst[x] = best;
		}
	}
}

} // namespace

void DrawCtrPanelBackground(const Surface &out, CtrPanelBackground background)
{
	const size_t index = background == CtrPanelBackground::Stone ? 0 : 1;
	if (!BackgroundLoadAttempted[index])
		LoadBackground(index);
	if (!Backgrounds[index]) {
		FillRect(out, 0, 0, out.w(), out.h(), 0);
		UnsafeDrawBorder2px(out, { { 1, 1 }, { out.w() - 2, out.h() - 2 } }, PAL16_BEIGE + 8);
		DrawHorizontalLine(out, { 3, 3 }, out.w() - 6, PAL16_BEIGE + 3);
		DrawVerticalLine(out, { 3, 3 }, out.h() - 6, PAL16_BEIGE + 3);
		return;
	}
	out.BlitFrom(*Backgrounds[index], { 0, 0, BackgroundWidth, BackgroundHeight }, { 0, 0 });
}

} // namespace devilution
