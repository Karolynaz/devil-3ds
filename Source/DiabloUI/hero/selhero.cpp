#include "DiabloUI/hero/selhero.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_rect.h>
#else
#include <SDL.h>
#endif

#include "DiabloUI/diabloui.h"
#include "DiabloUI/dialogs.h"
#include "DiabloUI/multi/selgame.h"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/selok.h"
#include "DiabloUI/selyesno.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/control_mode.hpp"
#include "engine/assets.hpp"
#ifdef __3DS__
#include "controls/controller.h"
#include "items.h"
#include "engine/load_file.hpp"
#include "engine/load_pcx.hpp"
#include "engine/load_cl2.hpp"
#include "engine/load_clx.hpp"
#include "engine/render/text_render.hpp"
#include "platform/ctr/pixel_geometry.hpp"
#include "engine/palette.h"
#include "engine/render/clx_render.hpp"
#include "player.h"
#include "utils/surface_to_clx.hpp"
#endif
#include "engine/point.hpp"
#include "game_mode.hpp"
#include "levels/gendung.h"
#include "options.h"
#include "pfile.h"
#include "tables/playerdat.hpp"
#include "utils/enum_traits.h"
#include "utils/format.hpp"
#include "utils/language.h"
#include "utils/sdl_geometry.h"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"

namespace devilution {

bool selhero_endMenu;
bool selhero_isMultiPlayer;

bool (*gfnHeroInfo)(bool (*fninfofunc)(_uiheroinfo *));
bool (*gfnHeroCreate)(_uiheroinfo *);
void (*gfnHeroStats)(HeroClass, _uidefaultstats *);

namespace {

std::size_t selhero_SaveCount = 0;
#ifdef __3DS__
_uiheroinfo selhero_heros[2 * MAX_CHARACTERS];
bool HeroListActive = false;
char HeroListHint[512] {};
char HeroLoadHint[512] {};
OptionalOwnedClxSpriteList DiabloNewHeroArt;
#else
_uiheroinfo selhero_heros[MAX_CHARACTERS];
#endif
_uiheroinfo selhero_heroInfo;
char textStats[6][4];
const char *title = "";
_selhero_selections selhero_result;
bool selhero_navigateYesNo;
bool selhero_isSavegame;

std::vector<std::unique_ptr<UiItemBase>> vecSelHeroDialog;
std::vector<std::unique_ptr<UiListItem>> vecSelHeroDlgItems;
std::vector<std::unique_ptr<UiItemBase>> vecSelDlgItems;

UiImageClx *SELHERO_DIALOG_HERO_IMG;
#ifdef __3DS__
std::vector<UiItemBase *> HeroSummaryElements;
std::optional<OwnedClxSpriteList> HeroPreviewRaw;
std::optional<OwnedClxSpriteList> HeroPreviewMapped;
std::array<SDL_Color, 256> HeroPreviewUiPalette {};
std::array<std::array<uint8_t, 3>, 256> HeroPreviewGamePalette {};
std::optional<std::pair<HeroClass, uint8_t>> HeroPreviewKey;
bool HeroPreviewVisible = false;
uint32_t HeroPreviewStarted;

// Match the statistics block instead of choosing a different integer zoom
// for each class/weapon crop. The same dimensions apply to every idle frame.
Size GetHeroPreviewSize(Size source)
{
	constexpr int Width = 180;
	constexpr int Height = 120;
	if (source.width <= 0 || source.height <= 0) return { 0, 0 };
	if (source.width * Height > source.height * Width)
		return { Width, std::max(1, source.height * Width / source.width) };
	return { std::max(1, source.width * Height / source.height), Height };
}

void UpdateHeroPreview()
{
	const auto key = std::pair { selhero_heroInfo.heroclass, selhero_heroInfo.graphics };
	if (HeroPreviewKey != key) {
		HeroPreviewRaw.reset();
		HeroPreviewMapped.reset();
		HeroPreviewKey = key;
		auto sprites = LoadPlayerPreviewSprites(key.first, key.second);
		if (sprites && sprites->numSprites() <= 32) {
			int width = 0, height = 0;
			for (const ClxSprite sprite : ClxSpriteList { *sprites }) {
				width = std::max(width, int(sprite.width()));
				height = std::max(height, int(sprite.height()));
			}
			if (width > 0 && width <= 256 && height > 0 && height <= 256) {
				const int frames = static_cast<int>(sprites->numSprites());
				OwnedSurface atlas(width, height * frames);
				SDL_FillSurfaceRect(atlas.surface, nullptr, 0);
				int left = width, right = -1, top = height, bottom = -1;
				for (int frame = 0; frame < frames; ++frame) {
					const ClxSprite sprite = (*sprites)[frame];
					RenderClxSprite(atlas, sprite, { (width - sprite.width()) / 2, frame * height + height - sprite.height() });
					for (int y = 0; y < height; ++y) {
						for (int x = 0; x < width; ++x) {
							if (*atlas.at(x, frame * height + y) == 0) continue;
							left = std::min(left, x); right = std::max(right, x);
							top = std::min(top, y); bottom = std::max(bottom, y);
						}
					}
				}
				// One shared crop keeps feet/weapon positions stable across frames.
				if (right >= left && bottom >= top
				    && LoadOptionalFileInMem("levels\\towndata\\town.pal", HeroPreviewGamePalette.data(), HeroPreviewGamePalette.size())) {
					OwnedSurface cropped(right - left + 1, (bottom - top + 1) * frames);
					for (int frame = 0; frame < frames; ++frame)
						cropped.BlitFrom(atlas, MakeSdlRect(left, frame * height + top, cropped.w(), bottom - top + 1), { 0, frame * (bottom - top + 1) });
					const Size previewSize = GetHeroPreviewSize({ cropped.w(), bottom - top + 1 });
					OwnedSurface scaled(previewSize.width, previewSize.height * frames);
					for (int frame = 0; frame < frames; ++frame)
						scaled.ScaleBlitFrom(cropped, MakeSdlRect(0, frame * (bottom - top + 1), cropped.w(), bottom - top + 1),
						    MakeSdlRect(0, frame * previewSize.height, previewSize.width, previewSize.height));
					HeroPreviewRaw.emplace(SurfaceToClx(scaled, frames, 0));
				}
			}
		}
	}
	HeroPreviewVisible = HeroPreviewRaw.has_value();
	HeroPreviewStarted = SDL_GetTicks();
	if (HeroPreviewVisible) SELHERO_DIALOG_HERO_IMG->Hide();
	else SELHERO_DIALOG_HERO_IMG->Show();
}

void RenderHeroPreview()
{
	if (!HeroPreviewVisible || !HeroPreviewRaw) return;
	if (!HeroPreviewMapped || std::memcmp(HeroPreviewUiPalette.data(), logical_palette.data(), sizeof(HeroPreviewUiPalette)) != 0) {
		std::array<uint8_t, 256> translation {};
		for (size_t i = 1; i < translation.size(); ++i) {
			int best = 3 * 256 * 256;
			for (size_t j = 1; j < logical_palette.size(); ++j) {
				const int r = int(HeroPreviewGamePalette[i][0]) - logical_palette[j].r;
				const int g = int(HeroPreviewGamePalette[i][1]) - logical_palette[j].g;
				const int b = int(HeroPreviewGamePalette[i][2]) - logical_palette[j].b;
				const int distance = r*r + g*g + b*b;
				if (distance < best) { best = distance; translation[i] = static_cast<uint8_t>(j); }
			}
		}
		HeroPreviewMapped.emplace(HeroPreviewRaw->clone());
		ClxApplyTrans(*HeroPreviewMapped, translation.data());
		HeroPreviewUiPalette = logical_palette;
	}
	// Standing animation advances independently of menu FPS and game tick rate.
	const uint32_t frame = ((SDL_GetTicks() - HeroPreviewStarted) / 100) % HeroPreviewMapped->numSprites();
	UiImageClx image((*HeroPreviewMapped)[frame], MakeSdlRect(12, 55, 288, 130), UiFlags::AlignCenter);
	UiRenderItem(image);
}
// Menu-only clips; none of these animations instantiate a Player or a missile.
struct ClassPreviewClip {
	std::optional<OwnedClxSpriteList> raw;
	std::optional<OwnedClxSpriteList> mapped;
	uint32_t idleFrames = 0;
	uint32_t actionFrames = 0;
	bool attempted = false;
};
bool ClassPreviewVisible = false;
bool ClassPreviewPaletteLoaded = false;
std::vector<HeroClass> ClassPreviewClasses;
std::array<ClassPreviewClip, static_cast<size_t>(HeroClass::LAST) + 1> ClassPreviews;
std::array<ClassPreviewClip, 2> ClassProjectiles;
std::array<SDL_Color, 256> ClassPreviewUiPalette {};
std::array<uint8_t, 256> ClassPreviewTranslation {};
bool ClassPreviewPaletteValid = false;
size_t ClassPreviewSelection = 0;
uint32_t ClassPreviewStarted = 0;
std::string ClassPreviewText;

size_t FirstClassPreview(size_t selection, size_t count)
{
	return count <= 4 ? 0 : std::min(selection > 0 ? selection - 1 : 0, count - 4);
}

uint32_t ClassPreviewFrame(uint32_t elapsed, uint32_t idleFrames, uint32_t actionFrames, bool selected)
{
	if (!selected || idleFrames == 0) return 0;
	if (elapsed / 80 < actionFrames) return idleFrames + elapsed / 80;
	return ((elapsed - actionFrames * 80) / 100) % idleFrames;
}

// Shared crop and anchor across idle/attack frames prevent the figure jumping
// or changing size when a weapon appears and disappears.
void PrepareClassPreview(ClassPreviewClip &clip, ClxSpriteList idle, std::optional<ClxSpriteList> action, int maxWidth = 122)
{
	const uint32_t idleFrames = idle.numSprites();
	const uint32_t actionFrames = action ? action->numSprites() : 0;
	if (idleFrames == 0 || idleFrames > 32 || actionFrames > 32) return;
	constexpr int Canvas = 256;
	const uint32_t frames = idleFrames + actionFrames;
	OwnedSurface atlas(Canvas, Canvas * frames);
	SDL_FillSurfaceRect(atlas.surface, nullptr, 0);
	int left = Canvas, right = -1, top = Canvas, bottom = -1, idleTop = Canvas, idleBottom = -1;
	for (uint32_t frame = 0; frame < frames; ++frame) {
		const ClxSprite sprite = frame < idleFrames ? idle[frame] : (*action)[frame - idleFrames];
		if (sprite.width() > Canvas || sprite.height() > Canvas) return;
		RenderClxSprite(atlas, sprite, { (Canvas - sprite.width()) / 2, static_cast<int>((frame + 1) * Canvas) - sprite.height() });
		for (int y = 0; y < Canvas; ++y) {
			for (int x = 0; x < Canvas; ++x) {
				if (*atlas.at(x, frame * Canvas + y) == 0) continue;
				left = std::min(left, x); right = std::max(right, x);
				top = std::min(top, y); bottom = std::max(bottom, y);
				if (frame < idleFrames) { idleTop = std::min(idleTop, y); idleBottom = std::max(idleBottom, y); }
			}
		}
	}
	if (right < left || idleBottom < idleTop) return;
	const int width = right - left + 1, height = bottom - top + 1;
	// Aim for a 120px standing figure; constrain a wide sword swing to its slot.
	const int idleHeight = idleBottom - idleTop + 1;
	Size size { std::max(1, width * 120 / idleHeight), std::max(1, height * 120 / idleHeight) };
	size = CtrFitImage(size, { maxWidth, 164 });
	OwnedSurface scaled(size.width, size.height * frames);
	for (uint32_t frame = 0; frame < frames; ++frame)
		scaled.ScaleBlitFrom(atlas, MakeSdlRect(left, frame * Canvas + top, width, height),
		    MakeSdlRect(0, frame * size.height, size.width, size.height));
	clip.raw.emplace(SurfaceToClx(scaled, frames, 0));
	clip.idleFrames = idleFrames;
	clip.actionFrames = actionFrames;
}

void EnsureClassPreview(HeroClass heroClass)
{
	auto &clip = ClassPreviews[static_cast<size_t>(heroClass)];
	if (clip.attempted) return;
	clip.attempted = true;
	auto idle = LoadPlayerPreviewSprites(heroClass, 0);
	if (!idle) return;
	PlayerWeaponGraphic weapon = PlayerWeaponGraphic::Unarmed;
	if (heroClass == HeroClass::Warrior || heroClass == HeroClass::Bard) weapon = PlayerWeaponGraphic::Sword;
	else if (heroClass == HeroClass::Rogue) weapon = PlayerWeaponGraphic::Bow;
	else if (heroClass == HeroClass::Barbarian) weapon = PlayerWeaponGraphic::Axe;
	auto action = LoadPlayerPreviewSprites(heroClass, static_cast<uint8_t>(weapon),
	    heroClass == HeroClass::Sorcerer ? player_graphic::Fire : player_graphic::Attack);
	PrepareClassPreview(clip, ClxSpriteList { *idle }, action ? std::optional<ClxSpriteList> { ClxSpriteList { *action } } : std::nullopt,
	    heroClass == HeroClass::Warrior ? 200 : (ClassPreviewClasses.size() >= 4 ? 90 : 122));
}

void EnsureClassProjectile(size_t index)
{
	auto &clip = ClassProjectiles[index];
	if (clip.attempted) return;
	clip.attempted = true;
#ifdef UNPACKED_MPQS
	auto loaded = LoadClxListOrSheetWithStatus(index == 0 ? "missiles\\arrows.clx" : "missiles\\fireba.clx");
#else
	auto loaded = LoadCl2ListOrSheetWithStatus(index == 0 ? "missiles\\arrows" : "missiles\\fireba1", PointerOrValue<uint16_t> { 96 });
#endif
	if (!loaded) return;
	if (loaded->isSheet() && loaded->numLists() == 0) return;
	const ClxSpriteList sprites = loaded->isSheet() ? loaded->sheet()[0] : loaded->list();
	if (sprites.numSprites() == 0 || sprites.numSprites() > 32) return;
	// Arrows store one frame per direction, rather than an animated sequence.
	const uint32_t frames = index == 0 ? 1 : sprites.numSprites();
	OwnedSurface atlas(96, 96 * frames);
	SDL_FillSurfaceRect(atlas.surface, nullptr, 0);
	int left = 96, right = -1, top = 96, bottom = -1;
	for (uint32_t frame = 0; frame < frames; ++frame) {
		const ClxSprite sprite = sprites[frame];
		if (sprite.width() > 96 || sprite.height() > 96) return;
		RenderClxSprite(atlas, sprite, { (96 - sprite.width()) / 2, static_cast<int>((frame + 1) * 96) - sprite.height() });
		for (int y = 0; y < 96; ++y) for (int x = 0; x < 96; ++x) {
			if (*atlas.at(x, frame * 96 + y) == 0) continue;
			left = std::min(left, x); right = std::max(right, x);
			top = std::min(top, y); bottom = std::max(bottom, y);
		}
	}
	if (right < left) return;
	OwnedSurface cropped(right - left + 1, (bottom - top + 1) * frames);
	for (uint32_t frame = 0; frame < frames; ++frame)
		cropped.BlitFrom(atlas, MakeSdlRect(left, frame * 96 + top, cropped.w(), bottom - top + 1), { 0, static_cast<int>(frame * (bottom - top + 1)) });
	clip.raw.emplace(SurfaceToClx(cropped, frames, 0));
}

const char *ClassPreviewDescription(HeroClass heroClass)
{
	switch (heroClass) {
	case HeroClass::Warrior: return N_("A strong melee fighter, skilled with swords and shields.");
	case HeroClass::Rogue: return N_("A swift archer who strikes from a distance with deadly accuracy.");
	case HeroClass::Sorcerer: return N_("A master of magic who destroys enemies with powerful spells.");
	case HeroClass::Monk: return N_("A disciplined fighter, deadly with staves and bare hands.");
	case HeroClass::Bard: return N_("A versatile adventurer, skilled with blades and magic.");
	case HeroClass::Barbarian: return N_("A mighty warrior who overwhelms enemies with heavy weapons.");
	default: return "";
	}
}

void MapClassPreview(ClassPreviewClip &clip)
{
	if (clip.raw && !clip.mapped) {
		clip.mapped.emplace(clip.raw->clone());
		ClxApplyTrans(*clip.mapped, ClassPreviewTranslation.data());
	}
}

void RenderClassPreviews()
{
	if (ClassPreviewClasses.empty() || !ClassPreviewPaletteLoaded) return;
	if (!ClassPreviewPaletteValid || std::memcmp(ClassPreviewUiPalette.data(), logical_palette.data(), sizeof(ClassPreviewUiPalette)) != 0) {
		ClassPreviewTranslation.fill(0);
		for (size_t i = 1; i < 256; ++i) {
			int best = 3 * 256 * 256;
			for (size_t j = 1; j < 256; ++j) {
				const int r = int(HeroPreviewGamePalette[i][0]) - logical_palette[j].r;
				const int g = int(HeroPreviewGamePalette[i][1]) - logical_palette[j].g;
				const int b = int(HeroPreviewGamePalette[i][2]) - logical_palette[j].b;
				const int distance = r*r + g*g + b*b;
				if (distance < best) { best = distance; ClassPreviewTranslation[i] = static_cast<uint8_t>(j); }
			}
		}
		for (auto &clip : ClassPreviews) clip.mapped.reset();
		for (auto &clip : ClassProjectiles) clip.mapped.reset();
		ClassPreviewUiPalette = logical_palette;
		ClassPreviewPaletteValid = true;
	}
	const size_t first = FirstClassPreview(ClassPreviewSelection, ClassPreviewClasses.size());
	const size_t count = std::min<size_t>(4, ClassPreviewClasses.size());
	const uint32_t elapsed = SDL_GetTicks() - ClassPreviewStarted;
	const int slotWidth = std::min(196, static_cast<int>(640 / count) - 16);
	for (size_t slot = 0; slot < count; ++slot) {
		const size_t index = first + slot;
		const HeroClass heroClass = ClassPreviewClasses[index];
		auto &clip = ClassPreviews[static_cast<size_t>(heroClass)];
		const int center = static_cast<int>((2 * slot + 1) * 640 / (2 * count));
		MapClassPreview(clip);
		if (!clip.mapped) {
			UiImageClx portrait(UiGetHeroDialogSprite(static_cast<size_t>(heroClass)), MakeSdlRect(center - slotWidth / 2, 45, slotWidth, 135), UiFlags::AlignCenter);
			UiRenderItem(portrait);
			continue;
		}
		const ClxSprite sprite = (*clip.mapped)[ClassPreviewFrame(elapsed, clip.idleFrames, clip.actionFrames, index == ClassPreviewSelection)];
		// The warrior's sword swing is wider than his standing body. Give the
		// animation its full width instead of shrinking the body to the slot.
		const int imageWidth = heroClass == HeroClass::Warrior ? std::max(slotWidth, (sprite.width() * 640 + 399) / 400) : slotWidth;
		UiImageClx image(sprite, MakeSdlRect(center - imageWidth / 2, 180 - sprite.height(), imageWidth, sprite.height()), UiFlags::AlignCenter);
		UiRenderItem(image);
		if (index != ClassPreviewSelection || clip.actionFrames == 0) continue;
		if (heroClass != HeroClass::Rogue && heroClass != HeroClass::Sorcerer) continue;
		const uint32_t release = clip.actionFrames * 80 / 2;
		if (elapsed < release || elapsed - release >= 450) continue;
		auto &projectile = ClassProjectiles[heroClass == HeroClass::Rogue ? 0 : 1];
		MapClassPreview(projectile);
		if (!projectile.mapped) continue;
		const uint32_t flight = elapsed - release;
		const ClxSprite bolt = (*projectile.mapped)[(flight / 60) % projectile.mapped->numSprites()];
		UiImageClx effect(bolt, MakeSdlRect(center - 24, 122 + flight * 55 / 450, 48, 24), UiFlags::AlignCenter);
		UiRenderItem(effect);
	}
	DrawString(Surface(DiabloUiSurface()), ClassPreviewText, { { 20, 422 }, { 600, 52 } },
	    { .flags = UiFlags::FontSize12 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, .spacing = 1, .lineHeight = 16 });
}

void FocusClassPreview(size_t selection)
{
	if (ClassPreviewClasses.empty()) return;
	ClassPreviewSelection = std::min(selection, ClassPreviewClasses.size() - 1);
	ClassPreviewText = WordWrapString(_(ClassPreviewDescription(ClassPreviewClasses[ClassPreviewSelection])), 600, GameFont12, 1, CtrTextScale::BottomScreen);
	if (!ClassPreviewPaletteLoaded)
		ClassPreviewPaletteLoaded = LoadOptionalFileInMem("levels\\towndata\\town.pal", HeroPreviewGamePalette.data(), HeroPreviewGamePalette.size());
	const size_t first = FirstClassPreview(ClassPreviewSelection, ClassPreviewClasses.size());
	for (size_t i = first; i < std::min(first + 4, ClassPreviewClasses.size()); ++i)
		EnsureClassPreview(ClassPreviewClasses[i]);
	EnsureClassProjectile(0);
	EnsureClassProjectile(1);
	ClassPreviewStarted = SDL_GetTicks();
}

void SetHeroSummaryVisible(bool visible)
{
	if (!visible) { HeroPreviewVisible = false; SELHERO_DIALOG_HERO_IMG->Show(); }
	for (auto *element : HeroSummaryElements) {
		if (visible) element->Show();
		else element->Hide();
	}
	SELHERO_DIALOG_HERO_IMG->m_rect = visible
	    ? MakeSdlRect(GetUIRectangle().position.x + 12, 82, 288, 76)
	    : MakeSdlRect(0, 0, 640, 240);
	SELHERO_DIALOG_HERO_IMG->setAllowUpscale(!visible);
}
#endif

#ifdef __3DS__
void SetHeroMenuMode(bool hellfire)
{
	const bool changed = gbIsHellfire != hellfire;
	SetCtrHeroGameMode(hellfire);
	if (!changed) return;
	HeroPreviewKey.reset();
	HeroPreviewRaw.reset();
	HeroPreviewMapped.reset();
	HeroPreviewVisible = false;
	for (auto &clip : ClassPreviews) clip = ClassPreviewClip {};
	for (auto &clip : ClassProjectiles) clip = ClassPreviewClip {};
	ClassPreviewPaletteLoaded = false;
	ClassPreviewPaletteValid = false;
	UiReloadHeroPortraits();
	FreeItemGFX();
	InitItemGFX();
}

bool CreateOriginalHeroRequested = false;
void SelheroListSelect(size_t value);

bool ConsumeHeroCreationEvent(SDL_Event &event)
{
	if (!HeroListActive || selhero_isSavegame || !HaveHellfire())
		return false;
	for (const ControllerButtonEvent button : ToControllerButtonEvents(event)) {
		if (button.button != ControllerButton_BUTTON_Y) continue;
		if (!button.up) {
			CreateOriginalHeroRequested = true;
			SelheroListSelect(selhero_SaveCount);
		}
		return true;
	}
	return false;
}

void UpdateHeroListPresentation()
{
	if (!HeroListActive) return;
	std::string_view hint;
	if (selhero_isSavegame) {
		hint = selhero_heroInfo.hellfire ? _("Hellfire · Press (Y) to delete") : _("Press (Y) to delete");
	} else {
		hint = HaveHellfire() ? _("Press (Y) to create non-Hellfire character") : std::string_view {};
		SELHERO_DIALOG_HERO_IMG->m_rect = MakeSdlRect(0, 0, 640, 240);
		SELHERO_DIALOG_HERO_IMG->setAllowUpscale(true);
		if (!HaveHellfire() && DiabloNewHeroArt && DiabloNewHeroArt->numSprites() > 3)
			SELHERO_DIALOG_HERO_IMG->setSprite((*DiabloNewHeroArt)[3]);
		else
			SELHERO_DIALOG_HERO_IMG->setSprite(UiGetHeroDialogSprite(GetNumPlayerClasses()));
	}
	CopyUtf8(HeroListHint, WordWrapString(hint, 600, GameFont12, 1, CtrTextScale::BottomScreen), sizeof(HeroListHint));
}
#endif

void SelheroListFocus(size_t value);
void SelheroListSelect(size_t value);
void SelheroListEsc();
void SelheroLoadFocus(size_t value);
void SelheroLoadSelect(size_t value);
void SelheroNameSelect(size_t value);
void SelheroNameEsc();
void SelheroClassSelectorFocus(size_t value);
void SelheroClassSelectorSelect(size_t value);
void SelheroClassSelectorEsc();
const char *SelheroGenerateName(HeroClass heroClass);

void SelheroUiFocusNavigationYesNo()
{
	if (selhero_isSavegame)
		UiFocusNavigationYesNo();
}

void SelheroFree()
{
	ArtBackground = std::nullopt;

#ifdef __3DS__
	HeroListActive = false;
	HeroSummaryElements.clear();
	HeroPreviewRaw.reset();
	HeroPreviewMapped.reset();
	HeroPreviewKey.reset();
	HeroPreviewVisible = false;
	ClassPreviewVisible = false;
	ClassPreviewClasses.clear();
	ClassPreviewText.clear();
	for (auto &clip : ClassPreviews) clip = ClassPreviewClip {};
	for (auto &clip : ClassProjectiles) clip = ClassPreviewClip {};
	ClassPreviewPaletteLoaded = false;
	ClassPreviewPaletteValid = false;
#endif
	vecSelHeroDialog.clear();

	vecSelDlgItems.clear();
	vecSelHeroDlgItems.clear();
	UnloadScrollBar();
}

void SelheroSetStats()
{
#ifdef __3DS__
	if (ClassPreviewVisible) {
		HeroPreviewVisible = false;
		SELHERO_DIALOG_HERO_IMG->Hide();
		for (auto *element : HeroSummaryElements) element->Hide();
		return;
	}
#endif
#ifdef __3DS__
	SetHeroSummaryVisible(true);
#endif
	SELHERO_DIALOG_HERO_IMG->setSprite(UiGetHeroDialogSprite(static_cast<size_t>(selhero_heroInfo.heroclass)));
#ifdef __3DS__
	UpdateHeroPreview();
#endif
	CopyUtf8(textStats[0], StrCat(selhero_heroInfo.level), sizeof(textStats[0]));
	CopyUtf8(textStats[1], StrCat(selhero_heroInfo.strength), sizeof(textStats[1]));
	CopyUtf8(textStats[2], StrCat(selhero_heroInfo.magic), sizeof(textStats[2]));
	CopyUtf8(textStats[3], StrCat(selhero_heroInfo.dexterity), sizeof(textStats[3]));
	CopyUtf8(textStats[4], StrCat(selhero_heroInfo.vitality), sizeof(textStats[4]));
	CopyUtf8(textStats[5], StrCat(selhero_heroInfo.saveNumber), sizeof(textStats[5]));
}

void RenderDifficultyIndicators()
{
	if (!selhero_isSavegame)
		return;
	const uint16_t width = (*DifficultyIndicator)[0].width();
	const uint16_t height = (*DifficultyIndicator)[0].height();
	SDL_Rect rect = MakeSdlRect(
	    SELHERO_DIALOG_HERO_IMG->m_rect.x + 1,
	    SELHERO_DIALOG_HERO_IMG->m_rect.y + SELHERO_DIALOG_HERO_IMG->m_rect.h - height - 1,
	    width,
	    height);
#ifdef __3DS__
	if (HeroPreviewVisible) { rect.x = 12; rect.y = 222; }
#endif
	for (int i = 0; i <= DIFF_LAST; i++) {
		if (i >= selhero_heroInfo.herorank)
			break;
		UiRenderItem(UiImageClx((*DifficultyIndicator)[0], rect, UiFlags::None));
		rect.x += width;
	}
}

UiArtTextButton *SELLIST_DIALOG_DELETE_BUTTON;

bool SelHeroGetHeroInfo(_uiheroinfo *pInfo)
{
#ifdef __3DS__
	if (selhero_SaveCount >= 2 * MAX_CHARACTERS) return false;
#endif
	selhero_heros[selhero_SaveCount] = *pInfo;

	selhero_SaveCount++;

	return true;
}

void SelheroListFocus(size_t value)
{
	UiFlags baseFlags = UiFlags::AlignCenter | UiFlags::FontSize30;
#ifdef __3DS__
	baseFlags |= UiFlags::ElementHidden;
#endif
	if (selhero_SaveCount != 0 && value < selhero_SaveCount) {
		memcpy(&selhero_heroInfo, &selhero_heros[value], sizeof(selhero_heroInfo));
#ifdef __3DS__
		SELHERO_DIALOG_HERO_IMG->m_rect = MakeSdlRect(GetUIRectangle().position.x + 12, 82, 288, 76);
#endif
		SelheroSetStats();
		SELLIST_DIALOG_DELETE_BUTTON->SetFlags(baseFlags | UiFlags::ColorUiGold);
		selhero_isSavegame = true;
#ifdef __3DS__
		UpdateHeroListPresentation();
#endif
		return;
	}

	SELHERO_DIALOG_HERO_IMG->setSprite(UiGetHeroDialogSprite(GetNumPlayerClasses()));
#ifdef __3DS__
	SetHeroSummaryVisible(false);
#endif
	for (char *textStat : textStats)
		strcpy(textStat, "--");
	SELLIST_DIALOG_DELETE_BUTTON->SetFlags(baseFlags | UiFlags::ColorUiSilver | UiFlags::ElementDisabled);
	selhero_isSavegame = false;
#ifdef __3DS__
	UpdateHeroListPresentation();
#endif
}

bool SelheroListDeleteYesNo()
{
	selhero_navigateYesNo = selhero_isSavegame;

	return selhero_navigateYesNo;
}

void SelheroListSelect(size_t value)
{
#ifdef __3DS__
	const bool fromHeroList = HeroListActive;
	HeroListActive = false;
	if (value == selhero_SaveCount && fromHeroList) {
		SetHeroMenuMode(HaveHellfire() && !CreateOriginalHeroRequested);
		CreateOriginalHeroRequested = false;
		// Enumeration above combined two independent save namespaces. Refresh
		// occupancy for the selected mode before choosing a free slot.
		gfnHeroInfo([](_uiheroinfo *) { return true; });
	} else if (value != selhero_SaveCount) {
		SetHeroMenuMode(selhero_heroInfo.hellfire);
		SelheroSetStats();
	}
#endif
	const Point uiPosition = GetUIRectangle().position;

	if (static_cast<std::size_t>(value) == selhero_SaveCount) {
		vecSelDlgItems.clear();

#ifdef __3DS__
		const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 20), 206, 600, 28 };
		vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Choose Class").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver, 3));

		vecSelHeroDlgItems.clear();
		int itemH = 26;
		for (size_t i = 0; i < GetNumPlayerClasses(); ++i) {
			const auto heroClass = static_cast<HeroClass>(i);

			if (heroClass == HeroClass::Monk && !gbIsHellfire) {
				continue;
			}

			if (heroClass == HeroClass::Bard && !HaveBardAssets() && !(*GetOptions().Gameplay.testBard)) {
				continue;
			}

			if (heroClass == HeroClass::Barbarian && !HaveBarbarianAssets() && !(*GetOptions().Gameplay.testBarbarian)) {
				continue;
			}

			const PlayerData &playerData = GetPlayerDataForClass(heroClass);
			vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_(playerData.className), static_cast<int>(heroClass)));
		}
		// Center the list above the bottom-screen class description.
		const int visibleClasses = static_cast<int>(std::min<size_t>(vecSelHeroDlgItems.size(), 6));
		const int itemY = 240 + (172 - visibleClasses * itemH) / 2;
		vecSelDlgItems.push_back(std::make_unique<UiList>(vecSelHeroDlgItems, std::min<size_t>(vecSelHeroDlgItems.size(), 6), uiPosition.x + 150, itemY, 340, itemH, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

		const SDL_Rect rectScrollBar = { (Sint16)(uiPosition.x + 500), itemY, 25, visibleClasses * itemH };
		vecSelDlgItems.push_back(std::make_unique<UiScrollbar>((*ArtScrollBarBackground)[0], (*ArtScrollBarThumb)[0], *ArtScrollBarArrow, rectScrollBar));
#else
		const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 242), (Sint16)(uiPosition.y + 211), 365, 33 };
		vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Choose Class").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

		vecSelHeroDlgItems.clear();
		int itemH = 33;
		for (size_t i = 0; i < GetNumPlayerClasses(); ++i) {
			const auto heroClass = static_cast<HeroClass>(i);

			if (heroClass == HeroClass::Monk && !gbIsHellfire) {
				continue;
			}

			if (heroClass == HeroClass::Bard && !HaveBardAssets() && !(*GetOptions().Gameplay.testBard)) {
				continue;
			}

			if (heroClass == HeroClass::Barbarian && !HaveBarbarianAssets() && !(*GetOptions().Gameplay.testBarbarian)) {
				continue;
			}

			const PlayerData &playerData = GetPlayerDataForClass(heroClass);
			vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_(playerData.className), static_cast<int>(heroClass)));
		}
		if (vecSelHeroDlgItems.size() > 4)
			itemH = 26;
		const int itemY = static_cast<int>(246 + ((176 - std::min<size_t>(vecSelHeroDlgItems.size(), 6) * itemH) / 2));
		vecSelDlgItems.push_back(std::make_unique<UiList>(vecSelHeroDlgItems, std::min<size_t>(vecSelHeroDlgItems.size(), 6), uiPosition.x + 264, (uiPosition.y + itemY), 320, itemH, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

		const SDL_Rect rectScrollBar = { (Sint16)(uiPosition.x + 585), (Sint16)(uiPosition.y + 244), 25, 178 };
		vecSelDlgItems.push_back(std::make_unique<UiScrollbar>((*ArtScrollBarBackground)[0], (*ArtScrollBarThumb)[0], *ArtScrollBarArrow, rectScrollBar));

		const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 279), (Sint16)(uiPosition.y + 429), 140, 35 };
		vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("OK"), &UiFocusNavigationSelect, rect2, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

		const SDL_Rect rect3 = { (Sint16)(uiPosition.x + 429), (Sint16)(uiPosition.y + 429), 144, 35 };
		vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("Cancel"), &UiFocusNavigationEsc, rect3, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
#endif

#ifdef __3DS__
		ClassPreviewVisible = true;
		ClassPreviewClasses.clear();
		for (const auto &item : vecSelHeroDlgItems)
			ClassPreviewClasses.push_back(static_cast<HeroClass>(item->m_value));
#endif
		UiInitList(SelheroClassSelectorFocus, SelheroClassSelectorSelect, SelheroClassSelectorEsc, vecSelDlgItems, true);
		memset(&selhero_heroInfo.name, 0, sizeof(selhero_heroInfo.name));
		selhero_heroInfo.saveNumber = pfile_ui_get_first_unused_save_num();
		SelheroSetStats();
		title = selhero_isMultiPlayer ? _("New Multi Player Hero").data() : _("New Single Player Hero").data();
		selhero_isSavegame = false;
		return;
	}

	if (selhero_heroInfo.hassaved) {
		vecSelDlgItems.clear();

#ifdef __3DS__
		const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 20), 248, 600, 28 };
		vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Save File Exists").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver, 3));

		vecSelHeroDlgItems.clear();
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("Load Game"), 0));
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("Reset World"), 1));
		vecSelDlgItems.push_back(std::make_unique<UiList>(vecSelHeroDlgItems, vecSelHeroDlgItems.size(), uiPosition.x + 70, 318, 500, 36, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));
		CopyUtf8(HeroLoadHint, WordWrapString(_("Reset World: change map layouts and respawn monsters. Your level, stats, items and spells stay."), 600, GameFont12, 1, CtrTextScale::BottomScreen), sizeof(HeroLoadHint));
		vecSelDlgItems.push_back(std::make_unique<UiArtText>(HeroLoadHint, MakeSdlRect(uiPosition.x + 20, 402, 600, 76), UiFlags::AlignCenter | UiFlags::FontSize12 | UiFlags::ColorUiSilver, 1, 17));
#else
		const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 242), (Sint16)(uiPosition.y + 211), 365, 33 };
		vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Save File Exists").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

		vecSelHeroDlgItems.clear();
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("Load Game"), 0));
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("New Game"), 1));
		vecSelDlgItems.push_back(std::make_unique<UiList>(vecSelHeroDlgItems, vecSelHeroDlgItems.size(), uiPosition.x + 265, (uiPosition.y + 285), 320, 33, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

		const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 279), (Sint16)(uiPosition.y + 427), 140, 35 };
		vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("OK"), &UiFocusNavigationSelect, rect2, UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

		const SDL_Rect rect3 = { (Sint16)(uiPosition.x + 429), (Sint16)(uiPosition.y + 427), 144, 35 };
		vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("Cancel"), &UiFocusNavigationEsc, rect3, UiFlags::AlignCenter | UiFlags::VerticalCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
#endif

		UiInitList(SelheroLoadFocus, SelheroLoadSelect, selhero_List_Init, vecSelDlgItems, true);
		title = _("Single Player Characters").data();
		return;
	}

	SelheroLoadSelect(1);
}

void SelheroListEsc()
{
	UiInitList_clear();

	selhero_endMenu = true;
	selhero_result = SELHERO_PREVIOUS;
}

void SelheroClassSelectorFocus(size_t value)
{
	const auto heroClass = static_cast<HeroClass>(vecSelHeroDlgItems[value]->m_value);

	_uidefaultstats defaults;
	gfnHeroStats(heroClass, &defaults);

	selhero_heroInfo.level = 1;
	selhero_heroInfo.heroclass = heroClass;
#ifdef __3DS__
	selhero_heroInfo.graphics = 0;
#endif
	selhero_heroInfo.strength = defaults.strength;
	selhero_heroInfo.magic = defaults.magic;
	selhero_heroInfo.dexterity = defaults.dexterity;
	selhero_heroInfo.vitality = defaults.vitality;

	SelheroSetStats();
#ifdef __3DS__
	FocusClassPreview(value);
#endif
}

bool ShouldPrefillHeroName()
{
#if defined(PREFILL_PLAYER_NAME)
	return true;
#else
	return ControlMode != ControlTypes::KeyboardAndMouse;
#endif
}

void RemoveSelHeroBackground()
{
	vecSelHeroDialog.erase(vecSelHeroDialog.begin());
	ArtBackground = std::nullopt;
}

void AddSelHeroBackground()
{
	LoadBackgroundArt("ui_art\\selhero");
	vecSelHeroDialog.insert(vecSelHeroDialog.begin(),
	    std::make_unique<UiImageClx>((*ArtBackground)[0], MakeSdlRect(0, GetUIRectangle().position.y, 0, 0), UiFlags::AlignCenter));
}

void SelheroClassSelectorSelect(size_t value)
{
#ifdef __3DS__
	ClassPreviewVisible = false;
	SelheroSetStats();
#endif
	auto hClass = static_cast<HeroClass>(vecSelHeroDlgItems[value]->m_value);
	if (gbIsSpawn && (hClass == HeroClass::Rogue || hClass == HeroClass::Sorcerer || (hClass == HeroClass::Bard && !HaveBardAssets()))) {
		RemoveSelHeroBackground();
		UiSelOkDialog(nullptr, _("The Rogue and Sorcerer are only available in the full retail version of Diablo. Visit https://www.gog.com/game/diablo to purchase.").data(), false);
		AddSelHeroBackground();
		SelheroListSelect(selhero_SaveCount);
		return;
	}

	const Point uiPosition = GetUIRectangle().position;

	title = selhero_isMultiPlayer ? _("New Multi Player Hero").data() : _("New Single Player Hero").data();
	memset(selhero_heroInfo.name, '\0', sizeof(selhero_heroInfo.name));
	if (ShouldPrefillHeroName())
		strcpy(selhero_heroInfo.name, SelheroGenerateName(selhero_heroInfo.heroclass));
	vecSelDlgItems.clear();
#ifdef __3DS__
	const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 20), 250, 600, 30 };
	vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Enter Name").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 48), 330, 544, 33 };
	vecSelDlgItems.push_back(std::make_unique<UiEdit>(_("Enter Name"), selhero_heroInfo.name, 15, false, rect2, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));
#else
	const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 242), (Sint16)(uiPosition.y + 211), 365, 33 };
	vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Enter Name").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 265), (Sint16)(uiPosition.y + 317), 320, 33 };
	vecSelDlgItems.push_back(std::make_unique<UiEdit>(_("Enter Name"), selhero_heroInfo.name, 15, false, rect2, UiFlags::FontSize24 | UiFlags::ColorUiGold));

	const SDL_Rect rect3 = { (Sint16)(uiPosition.x + 279), (Sint16)(uiPosition.y + 429), 140, 35 };
	vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("OK"), &UiFocusNavigationSelect, rect3, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	const SDL_Rect rect4 = { (Sint16)(uiPosition.x + 429), (Sint16)(uiPosition.y + 429), 144, 35 };
	vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("Cancel"), &UiFocusNavigationEsc, rect4, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
#endif

	UiInitList(nullptr, SelheroNameSelect, SelheroNameEsc, vecSelDlgItems);
}

void SelheroClassSelectorEsc()
{
	vecSelDlgItems.clear();
	vecSelHeroDlgItems.clear();

#ifdef __3DS__
	selhero_List_Init();
	return;
#endif
	if (selhero_SaveCount != 0) {
		selhero_List_Init();
		return;
	}

	SelheroListEsc();
}

void SelheroNameSelect(size_t /*value*/)
{
	// only check names in multiplayer, we don't care about them in single
	if (selhero_isMultiPlayer && !UiValidPlayerName(selhero_heroInfo.name)) {
		RemoveSelHeroBackground();
		UiSelOkDialog(title, _("Invalid name. A name cannot contain spaces, reserved characters, or reserved words.\n").data(), false);
		AddSelHeroBackground();
	} else {
		if (gfnHeroCreate(&selhero_heroInfo)) {
			SelheroLoadSelect(1);
			return;
		}
		UiErrorOkDialog(_(/* TRANSLATORS: Error Message */ "Unable to create character."), vecSelHeroDialog);
	}

	memset(selhero_heroInfo.name, '\0', sizeof(selhero_heroInfo.name));
	SelheroClassSelectorSelect(0);
}

void SelheroNameEsc()
{
	SelheroListSelect(selhero_SaveCount);
}

void SelheroLoadFocus(size_t value)
{
}

void SelheroLoadSelect(size_t value)
{
	UiInitList_clear();
	selhero_endMenu = true;
	if (vecSelHeroDlgItems[value]->m_value == 0) {
		selhero_result = SELHERO_CONTINUE;
		return;
	}

	if (!selhero_isMultiPlayer) {
		// This is part of a dangerous hack to enable difficulty selection in single-player.
		// FIXME: Dialogs should not refer to each other's variables.

		// We disable `selhero_endMenu` and replace the background and art
		// and the item list with the difficulty selection ones.
		//
		// This means selhero's render loop will render selgame's items,
		// which happens to work because the render loops are similar.
		selhero_endMenu = false;

		// Set this to false so that we do not attempt to render difficulty indicators.
		selhero_isSavegame = false;

		SelheroFree();
		LoadBackgroundArt("ui_art\\selgame");
		selgame_GameSelection_Select(0);
	}

	selhero_result = SELHERO_NEW_DUNGEON;
}

const char *SelheroGenerateName(HeroClass heroClass)
{
	static const char *const Names[6][10] = {
		{
		    // Warrior
		    "Aidan",
		    "Qarak",
		    "Born",
		    "Cathan",
		    "Halbu",
		    "Lenalas",
		    "Maximus",
		    "Vane",
		    "Myrdgar",
		    "Rothat",
		},
		{
		    // Rogue
		    "Moreina",
		    "Akara",
		    "Kashya",
		    "Flavie",
		    "Divo",
		    "Oriana",
		    "Iantha",
		    "Shikha",
		    "Basanti",
		    "Elexa",
		},
		{
		    // Sorcerer
		    "Jazreth",
		    "Drognan",
		    "Armin",
		    "Fauztin",
		    "Jere",
		    "Kazzulk",
		    "Ranslor",
		    "Sarnakyle",
		    "Valthek",
		    "Horazon",
		},
		{
		    // Monk
		    "Akyev",
		    "Dvorak",
		    "Kekegi",
		    "Kharazim",
		    "Mikulov",
		    "Shenlong",
		    "Vedenin",
		    "Vhalit",
		    "Vylnas",
		    "Zhota",
		},
		{
		    // Bard (uses Rogue names)
		    "Moreina",
		    "Akara",
		    "Kashya",
		    "Flavie",
		    "Divo",
		    "Oriana",
		    "Iantha",
		    "Shikha",
		    "Basanti",
		    "Elexa",
		},
		{
		    // Barbarian
		    "Alaric",
		    "Barloc",
		    "Egtheow",
		    "Guthlaf",
		    "Heorogar",
		    "Hrothgar",
		    "Oslaf",
		    "Qual-Kehk",
		    "Ragnar",
		    "Ulf",
		},
	};

	const int iRand = rand() % 10;

	return Names[static_cast<std::size_t>(heroClass) % 6][iRand];
}

} // namespace

void selhero_Init()
{
#ifdef __3DS__
	SetHeroMenuMode(false);
	if (!DiabloNewHeroArt)
		DiabloNewHeroArt = LoadPcxSpriteList("ui_art\\heros", -76);
#endif
	AddSelHeroBackground();
#ifndef __3DS__
	UiAddLogo(&vecSelHeroDialog);
#endif
	LoadScrollBar();

	selhero_SaveCount = 0;
	gfnHeroInfo(SelHeroGetHeroInfo);
#ifdef __3DS__
	if (HaveHellfire()) {
		SetHeroMenuMode(true);
		gfnHeroInfo(SelHeroGetHeroInfo);
	}
#endif
	std::reverse(selhero_heros, selhero_heros + selhero_SaveCount);

	const Point uiPosition = GetUIRectangle().position;

	vecSelDlgItems.clear();
#ifdef __3DS__
	SDL_Rect rect; // The top screen contains only the hero and statistics.

	// Top screen: 640 logical width -> 400 physical width (factor 0.625).
	// 288 logical width -> 180 physical width, preserving 180:76 (1:1 Diablo pixel aspect ratio).
	// Vertically centered at Y = 82 relative to the stats block (Y = 55..185).
	rect = MakeSdlRect(uiPosition.x + 12, 82, 288, 76);
	auto heroImg = std::make_unique<UiImageClx>(UiGetHeroDialogSprite(0), rect, UiFlags::AlignCenter);
	SELHERO_DIALOG_HERO_IMG = heroImg.get();
	vecSelHeroDialog.push_back(std::move(heroImg));

	const size_t statsStart = vecSelHeroDialog.size();
	const UiFlags labelFlags = UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::AlignRight | UiFlags::VerticalCenter;
	const UiFlags valueFlags = UiFlags::FontSize24 | UiFlags::ColorUiGold | UiFlags::AlignCenter;
	const int labelX = uiPosition.x + 295;
	const int valueX = uiPosition.x + 540;
	const int labelWidth = 240;
	const int valueWidth = 80;
	const int statHeight = 26;

	vecSelHeroDialog.push_back(std::make_unique<UiArtText>(_("Level:").data(), MakeSdlRect(labelX, 55, labelWidth, statHeight), labelFlags));
	vecSelHeroDialog.push_back(std::make_unique<UiArtText>(textStats[0], MakeSdlRect(valueX, 55, valueWidth, statHeight), valueFlags));

	const char *statLabels[] {
		_("Strength:").data(), _("Magic:").data(), _("Dexterity:").data(), _("Vitality:").data(),
#ifdef _DEBUG
		_("Savegame:").data()
#endif
	};
	int statY = 55 + statHeight;
	for (size_t i = 0; i < sizeof(statLabels) / sizeof(statLabels[0]); ++i) {
		vecSelHeroDialog.push_back(std::make_unique<UiArtText>(statLabels[i], MakeSdlRect(labelX, statY, labelWidth, statHeight), labelFlags));
		vecSelHeroDialog.push_back(std::make_unique<UiArtText>(textStats[i + 1], MakeSdlRect(valueX, statY, valueWidth, statHeight), valueFlags));
		statY += statHeight;
	}
	for (size_t i = statsStart; i < vecSelHeroDialog.size(); ++i)
		HeroSummaryElements.push_back(vecSelHeroDialog[i].get());
#else
	SDL_Rect rect = MakeSdlRect(uiPosition.x + 24, uiPosition.y + 161, 590, 35);
	vecSelHeroDialog.push_back(std::make_unique<UiArtText>(&title, rect, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	rect = MakeSdlRect(uiPosition.x + 30, uiPosition.y + 211, 180, 76);
	auto heroImg = std::make_unique<UiImageClx>(UiGetHeroDialogSprite(0), rect, UiFlags::None);
	SELHERO_DIALOG_HERO_IMG = heroImg.get();
	vecSelHeroDialog.push_back(std::move(heroImg));

	const UiFlags labelFlags = UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::AlignRight;
	const UiFlags valueFlags = UiFlags::FontSize12 | UiFlags::ColorUiSilverDark | UiFlags::AlignCenter;
	const int labelX = uiPosition.x + 39;
	const int valueX = uiPosition.x + 159;
	const int labelWidth = 110;
	const int valueWidth = 40;
	const int statHeight = 21;

	vecSelHeroDialog.push_back(std::make_unique<UiArtText>(_("Level:").data(), MakeSdlRect(labelX, uiPosition.y + 323, labelWidth, statHeight), labelFlags));
	vecSelHeroDialog.push_back(std::make_unique<UiArtText>(textStats[0], MakeSdlRect(valueX, uiPosition.y + 323, valueWidth, statHeight), valueFlags));

	const char *statLabels[] {
		_("Strength:").data(), _("Magic:").data(), _("Dexterity:").data(), _("Vitality:").data(),
#ifdef _DEBUG
		_("Savegame:").data()
#endif
	};
	int statY = uiPosition.y + 358;
	for (size_t i = 0; i < sizeof(statLabels) / sizeof(statLabels[0]); ++i) {
		vecSelHeroDialog.push_back(std::make_unique<UiArtText>(statLabels[i], MakeSdlRect(labelX, statY, labelWidth, statHeight), labelFlags));
		vecSelHeroDialog.push_back(std::make_unique<UiArtText>(textStats[i + 1], MakeSdlRect(valueX, statY, valueWidth, statHeight), valueFlags));
		statY += statHeight;
	}
#endif
}

void selhero_List_Init()
{
#ifdef __3DS__
	ClassPreviewVisible = false;
	SetHeroMenuMode(HaveHellfire());
	HeroListActive = true;
#endif
	const Point uiPosition = GetUIRectangle().position;

	size_t selectedItem = 0;
	vecSelDlgItems.clear();

#ifdef __3DS__
	const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 20), 248, 600, 26 };
	vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Select Hero").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiSilver, 3));

	vecSelHeroDlgItems.clear();
	for (std::size_t i = 0; i < selhero_SaveCount; i++) {
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(std::string_view(selhero_heros[i].name), static_cast<int>(i)));
		if (selhero_heros[i].saveNumber == selhero_heroInfo.saveNumber
		    && selhero_heros[i].hellfire == selhero_heroInfo.hellfire)
			selectedItem = i;
	}
	vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("New Hero"), static_cast<int>(selhero_SaveCount)));

	vecSelDlgItems.push_back(std::make_unique<UiList>(vecSelHeroDlgItems, 6, uiPosition.x + 70, 280, 500, 24, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

	const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 580), 280, 25, 144 };
	vecSelDlgItems.push_back(std::make_unique<UiScrollbar>((*ArtScrollBarBackground)[0], (*ArtScrollBarThumb)[0], *ArtScrollBarArrow, rect2));

	auto setlistDialogDeleteButton = std::make_unique<UiArtTextButton>(_("Delete"), &SelheroUiFocusNavigationYesNo, MakeSdlRect(0, 0, 0, 0), UiFlags::ElementHidden);
	SELLIST_DIALOG_DELETE_BUTTON = setlistDialogDeleteButton.get();
	vecSelDlgItems.push_back(std::move(setlistDialogDeleteButton));

	const SDL_Rect rectHint = { (Sint16)(uiPosition.x + 20), 438, 600, 40 };
	vecSelDlgItems.push_back(std::make_unique<UiArtText>(HeroListHint, rectHint, UiFlags::AlignCenter | UiFlags::FontSize12 | UiFlags::ColorUiSilver, 1, 18));
#else
	const SDL_Rect rect1 = { (Sint16)(uiPosition.x + 242), (Sint16)(uiPosition.y + 211), 365, 33 };
	vecSelDlgItems.push_back(std::make_unique<UiArtText>(_("Select Hero").data(), rect1, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver, 3));

	vecSelHeroDlgItems.clear();
	for (std::size_t i = 0; i < selhero_SaveCount; i++) {
		vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(std::string_view(selhero_heros[i].name), static_cast<int>(i)));
		if (selhero_heros[i].saveNumber == selhero_heroInfo.saveNumber)
			selectedItem = i;
	}
	vecSelHeroDlgItems.push_back(std::make_unique<UiListItem>(_("New Hero"), static_cast<int>(selhero_SaveCount)));

	vecSelDlgItems.push_back(std::make_unique<UiList>(vecSelHeroDlgItems, 6, uiPosition.x + 265, (uiPosition.y + 256), 320, 26, UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorUiGold));

	const SDL_Rect rect2 = { (Sint16)(uiPosition.x + 585), (Sint16)(uiPosition.y + 244), 25, 178 };
	vecSelDlgItems.push_back(std::make_unique<UiScrollbar>((*ArtScrollBarBackground)[0], (*ArtScrollBarThumb)[0], *ArtScrollBarArrow, rect2));

	const SDL_Rect rect3 = { (Sint16)(uiPosition.x + 239), (Sint16)(uiPosition.y + 429), 120, 35 };
	vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("OK"), &UiFocusNavigationSelect, rect3, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));

	const SDL_Rect rect4 = { (Sint16)(uiPosition.x + 364), (Sint16)(uiPosition.y + 429), 120, 35 };
	auto setlistDialogDeleteButton = std::make_unique<UiArtTextButton>(_("Delete"), &SelheroUiFocusNavigationYesNo, rect4, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::ElementDisabled);
	SELLIST_DIALOG_DELETE_BUTTON = setlistDialogDeleteButton.get();
	vecSelDlgItems.push_back(std::move(setlistDialogDeleteButton));

	const SDL_Rect rect5 = { (Sint16)(uiPosition.x + 489), (Sint16)(uiPosition.y + 429), 144, 35 };
	vecSelDlgItems.push_back(std::make_unique<UiArtTextButton>(_("Cancel"), &UiFocusNavigationEsc, rect5, UiFlags::AlignCenter | UiFlags::FontSize30 | UiFlags::ColorUiGold));
#endif

	UiInitList(SelheroListFocus, SelheroListSelect, SelheroListEsc, vecSelDlgItems, false, nullptr, SelheroListDeleteYesNo, selectedItem);
	if (selhero_isMultiPlayer) {
		title = _("Multi Player Characters").data();
	} else {
		title = _("Single Player Characters").data();
	}
}

static void UiSelHeroDialog(
    bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)),
    bool (*fncreate)(_uiheroinfo *),
    void (*fnstats)(HeroClass, _uidefaultstats *),
    bool (*fnremove)(_uiheroinfo *),
    _selhero_selections *dlgresult,
    uint32_t *saveNumber)
{
	do {
		gfnHeroInfo = fninfo;
		gfnHeroCreate = fncreate;
		gfnHeroStats = fnstats;
		selhero_result = *dlgresult;

		selhero_navigateYesNo = false;

		selhero_Init();

		if (selhero_SaveCount != 0) {
			selhero_heroInfo = {};
			// Search last used save and remember it as selected item
			for (size_t i = 0; i < selhero_SaveCount; i++) {
				if (selhero_heros[i].saveNumber == *saveNumber) {
					memcpy(&selhero_heroInfo, &selhero_heros[i], sizeof(selhero_heroInfo));
					break;
				}
			}
			selhero_List_Init();
		} else {
#ifdef __3DS__
			selhero_heroInfo = {};
			selhero_List_Init();
#else
			SelheroListSelect(selhero_SaveCount);
#endif
		}

		selhero_endMenu = false;
		while (!selhero_endMenu && !selhero_navigateYesNo) {
#ifdef __3DS__
			UpdateHeroListPresentation();
#endif
			UiClearScreen();
			UiRenderItems(vecSelHeroDialog);
			RenderDifficultyIndicators();
#ifdef __3DS__
			if (ClassPreviewVisible) RenderClassPreviews();
			else RenderHeroPreview();
#endif
#ifdef __3DS__
			UiPollAndRender(ConsumeHeroCreationEvent);
#else
			UiPollAndRender();
#endif
		}
		SelheroFree();

		if (selhero_navigateYesNo) {
			char dialogTitle[128] {};
			char dialogText[256];
#ifndef __3DS__
			if (selhero_isMultiPlayer) {
				CopyUtf8(dialogTitle, _("Delete Multi Player Hero"), sizeof(dialogTitle));
			} else {
				CopyUtf8(dialogTitle, _("Delete Single Player Hero"), sizeof(dialogTitle));
			}
#endif
#ifdef __3DS__
			CopyUtf8(dialogText, FormatRuntime(_("Delete character „{:s}“?"), selhero_heroInfo.name), sizeof(dialogText));
#else
			strcpy(dialogText, FormatRuntime(_("Are you sure you want to delete the character \"{:s}\"?"), selhero_heroInfo.name).c_str());
#endif

			if (UiSelHeroYesNoDialog(dialogTitle, dialogText)) {
#ifdef __3DS__
				SetCtrHeroGameMode(selhero_heroInfo.hellfire);
#endif
				fnremove(&selhero_heroInfo);
			}
		}
	} while (selhero_navigateYesNo);

	*dlgresult = selhero_result;
	*saveNumber = selhero_heroInfo.saveNumber;
}

void UiSelHeroSingDialog(
    bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)),
    bool (*fncreate)(_uiheroinfo *),
    bool (*fnremove)(_uiheroinfo *),
    void (*fnstats)(HeroClass, _uidefaultstats *),
    _selhero_selections *dlgresult,
    uint32_t *saveNumber,
    _difficulty *difficulty)
{
	selhero_isMultiPlayer = false;
	UiSelHeroDialog(fninfo, fncreate, fnstats, fnremove, dlgresult, saveNumber);
	*difficulty = nDifficulty;
}

void UiSelHeroMultDialog(
    bool (*fninfo)(bool (*fninfofunc)(_uiheroinfo *)),
    bool (*fncreate)(_uiheroinfo *),
    bool (*fnremove)(_uiheroinfo *),
    void (*fnstats)(HeroClass, _uidefaultstats *),
    _selhero_selections *dlgresult,
    uint32_t *saveNumber)
{
	selhero_isMultiPlayer = true;
	UiSelHeroDialog(fninfo, fncreate, fnstats, fnremove, dlgresult, saveNumber);
}

} // namespace devilution
