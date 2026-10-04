#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <function_ref.hpp>

#include "DiabloUI/diabloui.h"
#include "DiabloUI/scrollbar.h"
#include "DiabloUI/ui_flags.hpp"
#include "DiabloUI/ui_item.h"
#include "controls/controller.h"
#include "controls/controller_buttons.h"
#include "controls/controller_motion.h"
#include "controls/plrctrls.h"
#include "controls/remap_keyboard.h"
#include "engine/assets.hpp"
#include "engine/rectangle.hpp"
#include "engine/render/text_render.hpp"
#include "game_mode.hpp"
#include "hwcursor.hpp"
#include "items.h"
#include "options.h"
#include "utils/enum_traits.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/sdl_compat.h"
#include "utils/sdl_geometry.h"
#include "utils/static_vector.hpp"
#include "utils/str_cat.hpp"
#include "utils/ui_fwd.h"
#include "utils/utf8.hpp"
#ifdef __3DS__
#include "platform/ctr/update.hpp"
#endif

namespace devilution {
namespace {

constexpr size_t IndexKeyOrPadInput = 1;
constexpr size_t IndexPadTimerText = 2;
#ifdef __3DS__
constexpr int ControlsCategoryIndex = 999;
constexpr int UpdateCategoryIndex = 1000;
std::string updateStatus;
#endif

bool endMenu = false;
bool backToMain = false;

std::vector<std::unique_ptr<UiListItem>> vecDialogItems;
std::vector<std::unique_ptr<UiItemBase>> vecDialog;
std::vector<OptionEntryBase *> vecOptions;
OptionCategoryBase *selectedCategory = nullptr;
OptionEntryBase *selectedOption = nullptr;

enum class ShownMenuType : uint8_t {
	Categories,
	Settings,
	ListOption,
	KeyInput,
	PadInput,
#ifdef __3DS__
	Controls,
	Update,
#endif
};

ShownMenuType shownMenu;

char optionDescription[512];

Rectangle rectList;
Rectangle rectDescription;

enum class SpecialMenuEntry : int8_t {
	None = -1,
	PreviousMenu = -2,
	UnbindKey = -3,
	BindPadButton = -4,
	UnbindPadButton = -5,
};

ControllerButtonCombo padEntryCombo {};
Uint32 padEntryStartTime = 0;
std::string padEntryTimerText;

bool IsValidEntry(OptionEntryBase *pOptionEntry)
{
#ifdef __3DS__
	for (OptionEntryBase *pGraphicsEntry : GetOptions().Graphics.GetEntries()) {
		if (pOptionEntry == pGraphicsEntry)
			return false;
	}
	if (pOptionEntry == &GetOptions().Gameplay.grabInput
	    || pOptionEntry == &GetOptions().Gameplay.quickCast
	    || pOptionEntry == &GetOptions().Gameplay.pauseOnFocusLoss
	    || pOptionEntry == &GetOptions().Gameplay.storeUi) {
		return false;
	}
#endif
	auto flags = pOptionEntry->GetFlags();
	if (HasAnyOf(flags, OptionEntryFlags::NeedDiabloMpq) && !HaveIntro())
		return false;
	return HasNoneOf(flags, OptionEntryFlags::Invisible | (gbIsHellfire ? OptionEntryFlags::OnlyDiablo : OptionEntryFlags::OnlyHellfire));
}

std::vector<OptionEntryBase *> GetCategoryEntriesForMenu(OptionCategoryBase *pCategory)
{
#ifdef __3DS__
	if (pCategory == &GetOptions().GameMode || pCategory == &GetOptions().Gameplay)
		return {};
	if (pCategory == &GetOptions().Mods) {
		std::vector<OptionEntryBase *> entries;
		for (OptionEntryBase *pEntry : GetOptions().GameMode.GetEntries())
			entries.push_back(pEntry);
		for (OptionEntryBase *pEntry : GetOptions().Gameplay.GetEntries())
			entries.push_back(pEntry);
		for (OptionEntryBase *pEntry : GetOptions().Mods.GetEntries())
			entries.push_back(pEntry);
		return entries;
	}
#endif
	return pCategory->GetEntries();
}

constexpr UiFlags SettingsSecondaryTextColor = UiFlags::ColorUiSilver;

std::vector<DrawStringFormatArg> CreateDrawStringFormatArgForEntry(OptionEntryBase *pEntry)
{
	return std::vector<DrawStringFormatArg> {
		{ pEntry->GetName(), UiFlags::ColorUiGold },
		{ pEntry->GetValueDescription(), SettingsSecondaryTextColor }
	};
}

/** @brief Check if the option text can't fit in one list line (list width minus drawn selector) */
bool NeedsTwoLinesToDisplayOption(std::vector<DrawStringFormatArg> &formatArgs)
{
#ifdef __3DS__
	return GetLineWidth("{}: {}", formatArgs.data(), formatArgs.size(), 0, GameFontTables::GameFont24, 1, nullptr, std::nullopt, /*doubleWidth=*/true) >= (rectList.size.width - 90);
#else
	return GetLineWidth("{}: {}", formatArgs.data(), formatArgs.size(), 0, GameFontTables::GameFont24, 1) >= (rectList.size.width - 90);
#endif
}

void CleanUpSettingsUI()
{
	UiInitList_clear();

	vecDialogItems.clear();
	vecDialog.clear();
	vecOptions.clear();

	ArtBackground = std::nullopt;
	ArtBackgroundWidescreen = std::nullopt;
	UnloadScrollBar();
}

void GoBackOneMenuLevel()
{
	endMenu = true;
	switch (shownMenu) {
	case ShownMenuType::Categories:
		backToMain = true;
		break;
	case ShownMenuType::Settings:
#ifdef __3DS__
	case ShownMenuType::Controls:
	case ShownMenuType::Update:
#endif
		shownMenu = ShownMenuType::Categories;
		break;
	default:
		shownMenu = ShownMenuType::Settings;
		break;
	}
}

void StartPadEntryTimer()
{
	padEntryCombo = ControllerButton_NONE;
	padEntryStartTime = SDL_GetTicks();
	if (padEntryStartTime == 0)
		padEntryStartTime++;
	// Removes access to these dialog items while entering bindings
	for (size_t i = IndexPadTimerText + 1; i < vecDialogItems.size(); i++)
		vecDialogItems[i]->uiFlags |= UiFlags::ElementHidden;
}

void StopPadEntryTimer()
{
	padEntryCombo = ControllerButton_NONE;
	padEntryStartTime = 0;
	padEntryTimerText = "";
	vecDialogItems[IndexPadTimerText]->m_text = padEntryTimerText;
	// Restores access to these dialog items after binding is complete
	for (size_t i = IndexPadTimerText + 1; i < vecDialogItems.size(); i++)
		vecDialogItems[i]->uiFlags &= ~UiFlags::ElementHidden;
}

void UpdatePadEntryTimerText()
{
	if (shownMenu != ShownMenuType::PadInput)
		return;
	const Uint32 elapsed = SDL_GetTicks() - padEntryStartTime;
	if (padEntryStartTime == 0 || elapsed > 10000) {
		StopPadEntryTimer();
		return;
	}
	padEntryTimerText = StrCat(_("Press gamepad buttons to change."), " ", 10 - (elapsed / 1000));
	vecDialogItems[IndexPadTimerText]->m_text = padEntryTimerText;
}

void UpdateDescription(const OptionEntryBase &option)
{
#ifdef __3DS__
	auto paragraphs = WordWrapString(option.GetDescription(), rectDescription.size.width, GameFont12, 1, /*doubleWidth=*/true);
#else
	auto paragraphs = WordWrapString(option.GetDescription(), rectDescription.size.width, GameFont12, 1);
#endif
	CopyUtf8(optionDescription, paragraphs, sizeof(optionDescription));
}

void UpdateDescription(const OptionCategoryBase &category)
{
#ifdef __3DS__
	auto paragraphs = WordWrapString(category.GetDescription(), rectDescription.size.width, GameFont12, 1, /*doubleWidth=*/true);
#else
	auto paragraphs = WordWrapString(category.GetDescription(), rectDescription.size.width, GameFont12, 1);
#endif
	CopyUtf8(optionDescription, paragraphs, sizeof(optionDescription));
}

void ItemFocused(size_t value)
{
	switch (shownMenu) {
	case ShownMenuType::Categories: {
		auto &vecItem = vecDialogItems[value];
		optionDescription[0] = '\0';
		if (vecItem->m_value < 0)
			return;
#ifdef __3DS__
		if (vecItem->m_value == UpdateCategoryIndex) {
			CopyUtf8(optionDescription, _("Check GitHub for a newer Devil-3Ds release."), sizeof(optionDescription));
			return;
		}
		if (vecItem->m_value == ControlsCategoryIndex) {
			CopyUtf8(optionDescription, _("3DS button layout and in-game controls."), sizeof(optionDescription));
			return;
		}
#endif
		auto *pCategory = GetOptions().GetCategories()[vecItem->m_value];
#ifdef __3DS__
		if (pCategory == &GetOptions().Mods) {
			auto paragraphs = WordWrapString(_("Game mode, gameplay tweaks and modifications."), rectDescription.size.width, GameFont12, 1, /*doubleWidth=*/true);
			CopyUtf8(optionDescription, paragraphs, sizeof(optionDescription));
			return;
		}

#endif
		UpdateDescription(*pCategory);
	} break;
	case ShownMenuType::Settings: {
		auto &vecItem = vecDialogItems[value];
		optionDescription[0] = '\0';
		if (vecItem->m_value < 0)
			return;
		auto *pOption = vecOptions[vecItem->m_value];
		UpdateDescription(*pOption);
	} break;
	default:
		break;
	}
}

bool ChangeOptionValue(OptionEntryBase *pOption, size_t listIndex)
{
	if (HasAnyOf(pOption->GetFlags(), OptionEntryFlags::RecreateUI)) {
		endMenu = true;
		// Clean up all UI related Data
		CleanUpSettingsUI();
		UnloadUiGFX();
		FreeItemGFX();
		selectedOption = pOption;
	}

	switch (pOption->GetType()) {
	case OptionEntryType::Boolean: {
		auto *pOptionBoolean = static_cast<OptionEntryBoolean *>(pOption);
		pOptionBoolean->SetValue(!**pOptionBoolean);
	} break;
	case OptionEntryType::List: {
		auto *pOptionList = static_cast<OptionEntryListBase *>(pOption);
		pOptionList->SetActiveListIndex(listIndex);
	} break;
	case OptionEntryType::Key:
	case OptionEntryType::PadButton:
		break;
	}

	if (HasAnyOf(pOption->GetFlags(), OptionEntryFlags::RecreateUI)) {
		// Reinitialize UI with changed settings (for example game mode, language or resolution)
		UiInitialize();
		InitItemGFX();
		SetHardwareCursor(CursorInfo::UnknownCursor());
		return false;
	}

	return true;
}

void ItemSelected(size_t value)
{
	auto &vecItem = vecDialogItems[value];
	const int vecItemValue = vecItem->m_value;
	if (vecItemValue < 0) {
		auto specialMenuEntry = static_cast<SpecialMenuEntry>(vecItemValue);
		switch (specialMenuEntry) {
		case SpecialMenuEntry::None:
			break;
		case SpecialMenuEntry::PreviousMenu:
			GoBackOneMenuLevel();
			break;
		case SpecialMenuEntry::UnbindKey: {
			auto *pOptionKey = static_cast<KeymapperOptions::Action *>(selectedOption);
			pOptionKey->SetValue(SDLK_UNKNOWN);
			vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
			break;
		}
		case SpecialMenuEntry::BindPadButton:
			StartPadEntryTimer();
			break;
		case SpecialMenuEntry::UnbindPadButton:
			auto *pOptionPad = static_cast<PadmapperOptions::Action *>(selectedOption);
			pOptionPad->SetValue(ControllerButton_NONE);
			vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
			break;
		}
		return;
	}

	switch (shownMenu) {
	case ShownMenuType::Categories: {
#ifdef __3DS__
		if (vecItemValue == UpdateCategoryIndex) {
			endMenu = true;
			shownMenu = ShownMenuType::Update;
			return;
		}
		if (vecItemValue == ControlsCategoryIndex) {
			endMenu = true;
			shownMenu = ShownMenuType::Controls;
			return;
		}
#endif
		selectedCategory = GetOptions().GetCategories()[vecItemValue];
		endMenu = true;
		shownMenu = ShownMenuType::Settings;
	} break;
	case ShownMenuType::Settings: {
		auto *pOption = vecOptions[vecItemValue];
		bool updateValueDescription = false;
		if (pOption->GetType() == OptionEntryType::List) {
			auto *pOptionList = static_cast<OptionEntryListBase *>(pOption);
			if (pOptionList->GetListSize() > 2) {
				selectedOption = pOption;
				endMenu = true;
				shownMenu = ShownMenuType::ListOption;
			} else {
				// If the list contains only two items, we don't show a submenu and instead change the option value instantly
				size_t nextIndex = pOptionList->GetActiveListIndex() + 1;
				if (nextIndex >= pOptionList->GetListSize())
					nextIndex = 0;
				updateValueDescription = ChangeOptionValue(pOption, nextIndex);
			}
		} else if (pOption->GetType() == OptionEntryType::Key) {
			selectedOption = pOption;
			endMenu = true;
			shownMenu = ShownMenuType::KeyInput;
		} else if (pOption->GetType() == OptionEntryType::PadButton) {
			selectedOption = pOption;
			endMenu = true;
			shownMenu = ShownMenuType::PadInput;
		} else {
			updateValueDescription = ChangeOptionValue(pOption, 0);
		}
		if (updateValueDescription) {
#ifdef __3DS__
			if (vecItem->columns) {
				vecItem->rightText = pOption->GetValueDescription();
				break;
			}
#endif
			auto args = CreateDrawStringFormatArgForEntry(pOption);
			const bool optionUsesTwoLines = ((value + 1) < vecDialogItems.size() && vecDialogItems[value]->m_value == vecDialogItems[value + 1]->m_value);
			if (NeedsTwoLinesToDisplayOption(args) != optionUsesTwoLines) {
				selectedOption = pOption;
				endMenu = true;
			} else {
				vecItem->args.clear();
				for (auto &arg : args)
					vecItem->args.push_back(arg);
				if (optionUsesTwoLines) {
					vecDialogItems[value + 1]->m_text = std::string(pOption->GetValueDescription());
				}
			}
		}
	} break;
#ifdef __3DS__
	case ShownMenuType::Update:
		if (!CtrUpdateBusy()) {
			updateStatus = std::string(_("Checking for updates..."));
			CtrCheckForUpdates();
		}
		break;
#endif
	case ShownMenuType::ListOption: {
		ChangeOptionValue(selectedOption, vecItemValue);
		GoBackOneMenuLevel();
	} break;
	case ShownMenuType::KeyInput:
	case ShownMenuType::PadInput:
		break;
	}
}

void EscPressed()
{
	GoBackOneMenuLevel();
}

void FullscreenChanged()
{
	auto *fullscreenOption = &GetOptions().Graphics.fullscreen;

	for (auto &vecItem : vecDialogItems) {
		const int vecItemValue = vecItem->m_value;
		if (vecItemValue < 0 || static_cast<size_t>(vecItemValue) >= vecOptions.size())
			continue;

		auto *pOption = vecOptions[vecItemValue];
		if (pOption != fullscreenOption)
			continue;

		vecItem->args.clear();
		for (auto &arg : CreateDrawStringFormatArgForEntry(pOption))
			vecItem->args.push_back(arg);
		break;
	}
}

} // namespace

void UiSettingsMenu()
{
	backToMain = false;
	shownMenu = ShownMenuType::Categories;
	selectedCategory = nullptr;
	selectedOption = nullptr;

	do {
		endMenu = false;

		// For the settings menu, we use the full height and allow some more width.
		const int uiWidth = std::clamp<int>(gnScreenWidth, 640, 720);
		const Rectangle uiRectangle = {
			{ (gnScreenWidth - uiWidth) / 2, 0 },
			{ uiWidth, gnScreenHeight }
		};

		UiLoadBlackBackground();
		LoadScrollBar();
		UiAddBackground(&vecDialog);
#ifdef __3DS__
		UiAddLogo(&vecDialog, 10);
#else
		UiAddLogo(&vecDialog, uiRectangle.position.y);
#endif

		#ifdef __3DS__
		const int descriptionLineHeight = shownMenu == ShownMenuType::Update ? 14 : (IsSmallFontTall() ? 20 : 18);
#else
		const int descriptionLineHeight = IsSmallFontTall() ? 20 : 18;
#endif
		const int descriptionMarginTop = IsSmallFontTall() ? 10 : 16;

		optionDescription[0] = '\0';

		std::string_view titleText;
		switch (shownMenu) {
		case ShownMenuType::Categories:
			titleText = _("Settings");
			break;
#ifdef __3DS__
		case ShownMenuType::Update:
			titleText = _("Update");
			break;
		case ShownMenuType::Controls:
			titleText = _("Controls");
			break;
#endif
		case ShownMenuType::Settings:
			titleText = selectedCategory->GetName();
			break;
		default:
			titleText = selectedOption->GetName();
			break;
		}
#ifdef __3DS__
		// Keep every settings view inside the 3DS bottom screen (y = 240..479).
		vecDialog.push_back(std::make_unique<UiArtText>(titleText.data(), MakeSdlRect(uiRectangle.position.x, 246, uiRectangle.size.width, 35), UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 8));
#else
		vecDialog.push_back(std::make_unique<UiArtText>(titleText.data(), MakeSdlRect(uiRectangle.position.x, uiRectangle.position.y + 161, uiRectangle.size.width, 35), UiFlags::FontSize30 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 8));
#endif

		size_t itemToSelect = 0;
		std::optional<tl::function_ref<bool(SDL_Event &)>> eventHandler;

		switch (shownMenu) {
		case ShownMenuType::Categories: {
			size_t catIndex = 0;
			for (OptionCategoryBase *pCategory : GetOptions().GetCategories()) {
#ifdef __3DS__
				if (pCategory == &GetOptions().Keymapper || pCategory == &GetOptions().Padmapper || pCategory == &GetOptions().Graphics) {
					catIndex++;
					continue;
				}
				if (pCategory == &GetOptions().GameMode || pCategory == &GetOptions().Gameplay) {
					catIndex++;
					continue;
				}
#endif
				for (OptionEntryBase *pEntry : GetCategoryEntriesForMenu(pCategory)) {
					if (!IsValidEntry(pEntry))
						continue;
					if (selectedCategory == pCategory)
						itemToSelect = vecDialogItems.size();
					vecDialogItems.push_back(std::make_unique<UiListItem>(pCategory->GetName(), static_cast<int>(catIndex), UiFlags::ColorUiGold));
					break;
				}
				catIndex++;
			}
#ifdef __3DS__
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Controls"), ControlsCategoryIndex, UiFlags::ColorUiGold));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Update"), UpdateCategoryIndex, UiFlags::ColorUiGold));
#endif
		} break;
#ifdef __3DS__
		case ShownMenuType::Update:
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Check for updates"), 0, UiFlags::ColorUiGold));
			vecDialog.push_back(std::make_unique<UiArtText>(CtrProjectUrl.data(), MakeSdlRect(12, 465, 616, 14), UiFlags::FontSize12 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 0));
			break;
		case ShownMenuType::Controls: {
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Player Controls"), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiGold | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("A: Action / Attack / Talk"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("B: Cancel / Back"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("X: Cast Spell / Use Skill"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Y: Spell / Skill Selection"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("L: Drink Potions 1-4"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("R: Drink Potions 5-8"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Circle Pad: Move"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("D-Pad Up: Show Character panel"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("D-Pad Down: Show Map"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("D-Pad Left: Show Quests"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("D-Pad Right: Show Spell Book"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("SELECT: Open Inventory"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("START: In-Game Menu"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view {}, static_cast<int>(SpecialMenuEntry::None), UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Panels & Menus"), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiGold | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("L / R: Switch panels / stack tabs"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("A: Pickup / Place"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("A (Hold): Use Item / Sell Item"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Y: Drop Item from Inventory"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("B: Close Panel"), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor));
			itemToSelect = 1;
		} break;
#endif
		case ShownMenuType::Settings: {
			for (OptionEntryBase *pEntry : GetCategoryEntriesForMenu(selectedCategory)) {
				if (!IsValidEntry(pEntry))
					continue;
				if (selectedOption == pEntry)
					itemToSelect = vecDialogItems.size();
				auto formatArgs = CreateDrawStringFormatArgForEntry(pEntry);
				const int optionId = static_cast<int>(vecOptions.size());
#ifdef __3DS__
				auto row = std::make_unique<UiListItem>(pEntry->GetName(), optionId, UiFlags::ColorUiGold);
				row->columns = true;
				row->rightText = pEntry->GetValueDescription();
				if (GetLineWidth(row->rightText, GameFont12, 0, nullptr, true) > row->columnValueWidth)
					row->columnValueWidth = 192;
				vecDialogItems.push_back(std::move(row));
#else
				if (NeedsTwoLinesToDisplayOption(formatArgs)) {
					vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view("{}:"), formatArgs, optionId, UiFlags::ColorUiGold | UiFlags::NeedsNextElement));
					vecDialogItems.push_back(std::make_unique<UiListItem>(std::string(pEntry->GetValueDescription()), optionId, SettingsSecondaryTextColor | UiFlags::ElementDisabled));
				} else {
					vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view("{}: {}"), formatArgs, optionId, UiFlags::ColorUiGold));
				}
#endif
				vecOptions.push_back(pEntry);
			}
		} break;
		case ShownMenuType::ListOption: {
			auto *pOptionList = static_cast<OptionEntryListBase *>(selectedOption);
			for (size_t i = 0; i < pOptionList->GetListSize(); i++) {
				vecDialogItems.push_back(std::make_unique<UiListItem>(pOptionList->GetListDescription(i), static_cast<int>(i), UiFlags::ColorUiGold));
			}
			itemToSelect = pOptionList->GetActiveListIndex();
			UpdateDescription(*pOptionList);
		} break;
		case ShownMenuType::KeyInput: {
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Bound key:"), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorWhitegold | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string(selectedOption->GetValueDescription()), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorUiGold));
			assert(IndexKeyOrPadInput == vecDialogItems.size() - 1);
			itemToSelect = IndexKeyOrPadInput;
			eventHandler = [](SDL_Event &event) {
				if (SelectedItem != IndexKeyOrPadInput)
					return false;
				uint32_t key = SDLK_UNKNOWN;
				switch (event.type) {
				case SDL_EVENT_KEY_DOWN: {
					SDL_Keycode keycode = SDLC_EventKey(event);
					remap_keyboard_key(&keycode);
					key = static_cast<uint32_t>(keycode);
					if (key >= SDLK_A && key <= SDLK_Z) {
						key -= 'a' - 'A';
					}
				} break;
				case SDL_EVENT_MOUSE_BUTTON_DOWN:
					switch (event.button.button) {
					case SDL_BUTTON_MIDDLE:
					case SDL_BUTTON_X1:
					case SDL_BUTTON_X2:
						key = event.button.button | KeymapperMouseButtonMask;
						break;
					}
					break;
#if SDL_VERSION_ATLEAST(2, 0, 0)
				case SDL_EVENT_MOUSE_WHEEL:
					if (SDLC_EventWheelIntY(event) > 0) {
						key = MouseScrollUpButton;
					} else if (SDLC_EventWheelIntY(event) < 0) {
						key = MouseScrollDownButton;
					} else if (SDLC_EventWheelIntX(event) > 0) {
						key = MouseScrollLeftButton;
					} else if (SDLC_EventWheelIntX(event) < 0) {
						key = MouseScrollRightButton;
					}
					break;
#endif
				}
				// Ignore unknown keys
				if (key == SDLK_UNKNOWN)
					return false;
				auto *pOptionKey = static_cast<KeymapperOptions::Action *>(selectedOption);
				if (!pOptionKey->SetValue(key))
					return false;
				vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
				return true;
			};
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Press any key to change."), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view {}, static_cast<int>(SpecialMenuEntry::None), UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Unbind key"), static_cast<int>(SpecialMenuEntry::UnbindKey), UiFlags::ColorUiGold));
			UpdateDescription(*selectedOption);
		} break;
		case ShownMenuType::PadInput: {
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Bound button combo:"), static_cast<int>(SpecialMenuEntry::None), UiFlags::ColorWhitegold | UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(selectedOption->GetValueDescription(), static_cast<int>(SpecialMenuEntry::BindPadButton), UiFlags::ColorUiGold));
			assert(IndexKeyOrPadInput == vecDialogItems.size() - 1);
			itemToSelect = IndexKeyOrPadInput;

			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view(padEntryTimerText), static_cast<int>(SpecialMenuEntry::None), SettingsSecondaryTextColor | UiFlags::ElementDisabled));
			assert(IndexPadTimerText == vecDialogItems.size() - 1);

			vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view {}, static_cast<int>(SpecialMenuEntry::None), UiFlags::ElementDisabled));
			vecDialogItems.push_back(std::make_unique<UiListItem>(_("Unbind button combo"), static_cast<int>(SpecialMenuEntry::UnbindPadButton), UiFlags::ColorUiGold));

			padEntryStartTime = 0;
			eventHandler = [](SDL_Event &event) {
				if (padEntryStartTime == 0)
					return false;

				const StaticVector<ControllerButtonEvent, 4> ctrlEvents = ToControllerButtonEvents(event);
				for (const ControllerButtonEvent ctrlEvent : ctrlEvents) {
					const bool isGamepadMotion = IsControllerMotion(event);
					DetectInputMethod(event, ctrlEvent);
					if (event.type == SDL_EVENT_KEY_UP && SDLC_EventKey(event) == SDLK_ESCAPE) {
						StopPadEntryTimer();
						return true;
					}
					if (isGamepadMotion || IsAnyOf(ctrlEvent.button, ControllerButton_NONE, ControllerButton_IGNORE)) {
						continue;
					}

					const bool modifierPressed = padEntryCombo.modifier != ControllerButton_NONE && IsControllerButtonPressed(padEntryCombo.modifier);
					const bool buttonPressed = padEntryCombo.button != ControllerButton_NONE && IsControllerButtonPressed(padEntryCombo.button);
					if (ctrlEvent.up) {
						// When the player has released all relevant inputs, assume the binding is finished and stop the timer
						if (padEntryCombo.button != ControllerButton_NONE && !modifierPressed && !buttonPressed) {
							StopPadEntryTimer();
							return true;
						}
						continue;
					}

					auto *pOptionPad = static_cast<PadmapperOptions::Action *>(selectedOption);
					if (!modifierPressed && buttonPressed)
						padEntryCombo.modifier = padEntryCombo.button;
					padEntryCombo.button = ctrlEvent.button;
					if (pOptionPad->SetValue(padEntryCombo))
						vecDialogItems[IndexKeyOrPadInput]->m_text = selectedOption->GetValueDescription();
				}
				return true;
			};
			UpdateDescription(*selectedOption);
		} break;
		}

#ifndef __3DS__
		vecDialogItems.push_back(std::make_unique<UiListItem>(std::string_view {}, static_cast<int>(SpecialMenuEntry::None), UiFlags::ElementDisabled));
		vecDialogItems.push_back(std::make_unique<UiListItem>(_("Previous Menu"), static_cast<int>(SpecialMenuEntry::PreviousMenu), UiFlags::ColorUiGold));
#endif

#ifdef __3DS__
		const bool isControlsMenu = (shownMenu == ShownMenuType::Controls);
		const bool isSettingsMenu = (shownMenu == ShownMenuType::Settings);
		const int ListItemHeight = isControlsMenu ? 19 : (isSettingsMenu ? 32 : 27);
		const int maxListHeight = isControlsMenu ? 171 : 135;
		rectList = { { uiRectangle.position.x + 24, isControlsMenu ? 292 : 283 },
			Size { uiRectangle.size.width - 48, std::min<int>(static_cast<int>(vecDialogItems.size()) * ListItemHeight, maxListHeight) } };
		rectDescription = { { uiRectangle.position.x + 24, 423 },
			Size { uiRectangle.size.width - 48, 53 } };
		if (shownMenu == ShownMenuType::Update)
			rectDescription = { { 24, 332 }, { 592, 125 } };
		const UiFlags listFontFlags = isControlsMenu ? (UiFlags::FontSize12 | UiFlags::AlignCenter) : (UiFlags::FontSize24 | UiFlags::AlignCenter);
#else
		constexpr int ListItemHeight = 26;
		rectList = { uiRectangle.position + Displacement { 50, 204 },
			Size { uiRectangle.size.width - 100, std::min<int>(static_cast<int>(vecDialogItems.size()) * ListItemHeight, uiRectangle.size.height - 272) } };
		rectDescription = { rectList.position + Displacement { -26, rectList.size.height + descriptionMarginTop },
			Size { uiRectangle.size.width - 50, 80 - descriptionMarginTop } };
		const UiFlags listFontFlags = UiFlags::FontSize24 | UiFlags::AlignCenter;
#endif
		vecDialog.push_back(std::make_unique<UiScrollbar>((*ArtScrollBarBackground)[0], (*ArtScrollBarThumb)[0],
		    *ArtScrollBarArrow, MakeSdlRect(rectList.position.x + rectList.size.width + 5, rectList.position.y, 25, rectList.size.height)));
		vecDialog.push_back(std::make_unique<UiArtText>(optionDescription, MakeSdlRect(rectDescription),
		    UiFlags::FontSize12 | UiFlags::ColorUiSilver | UiFlags::AlignCenter, 1, descriptionLineHeight));
		vecDialog.push_back(std::make_unique<UiList>(vecDialogItems, rectList.size.height / ListItemHeight,
		    rectList.position.x, rectList.position.y, rectList.size.width, ListItemHeight, listFontFlags));

		UiInitList(ItemFocused, ItemSelected, EscPressed, vecDialog, true, FullscreenChanged, nullptr, itemToSelect);

		while (!endMenu) {
#ifdef __3DS__
			if (shownMenu == ShownMenuType::Update) {
				if (auto result = CtrPollUpdate()) {
					switch (result->state) {
					case CtrUpdateState::Available: updateStatus = std::string(_("A newer release is available.")); break;
					case CtrUpdateState::Current: updateStatus = std::string(_("No newer release. This build is up to date.")); break;
					case CtrUpdateState::Different: updateStatus = std::string(_("Different release branch. Check GitHub for details.")); break;
					case CtrUpdateState::Error: updateStatus = std::string(_("Update check failed.")) + "\n" + result->detail; break;
					}
					if (!result->version.empty()) updateStatus += "\n" + std::string(_("Latest release: ")) + result->version;
				}
				const std::string status = std::string(_("Build: ")) + std::string(CtrBuildCommit().substr(0, 7)) + "\n\n" + (updateStatus.empty() ? std::string(_("Press A to check for updates.")) : updateStatus);
				CopyUtf8(optionDescription, WordWrapString(status, rectDescription.size.width, GameFont12, 1, true), sizeof(optionDescription));
			}
#endif
			UiClearScreen();
			UpdatePadEntryTimerText();
			UiPollAndRender(eventHandler);
		}

		CleanUpSettingsUI();
	} while (!backToMain);

	SaveOptions();
}

} // namespace devilution
