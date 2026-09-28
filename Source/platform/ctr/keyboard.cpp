#include <cstdlib>
#include <cstring>
#include <charconv>
#include <string>

#include "platform/ctr/keyboard.h"
#include "utils/utf8.hpp"
#include "utils/format.hpp"
#include "utils/language.h"

constexpr size_t MAX_TEXT_LENGTH = 255;

struct vkbdEvent {
	std::string_view hintText;
	std::string_view inText;
	void (*textInputFn)(std::string_view);
};

static vkbdEvent events[16];
static int eventCount = 0;

void ctr_vkbdInput(std::string_view hintText, std::string_view inText, void (*textInputFn)(std::string_view))
{
	if (eventCount >= sizeof(events))
		return;

	vkbdEvent &event = events[eventCount];
	event.hintText = hintText;
	event.inText = inText;
	event.textInputFn = textInputFn;
	eventCount++;
}

void ctr_vkbdFlush()
{
	for (int i = 0; i < eventCount; i++) {
		vkbdEvent &event = events[i];
		SwkbdState swkbd;

		swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, MAX_TEXT_LENGTH);
		swkbdSetValidation(&swkbd, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);

		// swkbdSetInitialText stores the pointer to the c-string, only copying it when swkbdInputText is called. Need to
		//  ensure it has a valid null-terminated string until that point.
		std::string initialText { event.inText };
		swkbdSetInitialText(&swkbd, initialText.c_str());

		// swkbdSetHintText copies from the c-string immediately so we can use the output buffer to save a malloc
		char mybuf[MAX_TEXT_LENGTH + 1];
		devilution::CopyUtf8(mybuf, event.hintText, sizeof(mybuf));
		swkbdSetHintText(&swkbd, mybuf);

		memset(mybuf, 0, sizeof(mybuf));
		SwkbdButton button = swkbdInputText(&swkbd, mybuf, sizeof(mybuf));

		if (button == SWKBD_BUTTON_CONFIRM) {
			event.textInputFn(mybuf);
		}
	}

	eventCount = 0;
}

std::optional<int> ctr_vkbdNumberInput(std::string_view hint, int maximum)
{
	if (maximum <= 0)
		return std::nullopt;
	struct Validation {
		int maximum;
		std::string message;
	} validation { maximum, devilution::FormatRuntime(_("Enter a number from 1 to {:d}."), maximum) };
	SwkbdState keyboard;
	swkbdInit(&keyboard, SWKBD_TYPE_NUMPAD, 2, 10);
	swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
	swkbdSetNumpadKeys(&keyboard, 0, 0);
	const std::string hintText { hint };
	swkbdSetHintText(&keyboard, hintText.c_str());
	swkbdSetFilterCallback(&keyboard, [](void *user, const char **message, const char *text, size_t length) {
		const auto &validation = *static_cast<const Validation *>(user);
		int value = 0;
		const auto result = std::from_chars(text, text + length, value);
		if (result.ec != std::errc {} || result.ptr != text + length || value < 1 || value > validation.maximum) {
			*message = validation.message.c_str();
			return SWKBD_CALLBACK_CONTINUE;
		}
		return SWKBD_CALLBACK_OK;
	}, &validation);
	char buffer[11] {};
	if (swkbdInputText(&keyboard, buffer, sizeof(buffer)) != SWKBD_BUTTON_CONFIRM)
		return std::nullopt;
	int value = 0;
	const auto result = std::from_chars(buffer, buffer + std::strlen(buffer), value);
	if (result.ec != std::errc {} || result.ptr != buffer + std::strlen(buffer) || value < 1 || value > maximum)
		return std::nullopt;
	return value;
}
