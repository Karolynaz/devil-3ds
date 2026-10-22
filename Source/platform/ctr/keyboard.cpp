#include <algorithm>
#include <cstdlib>
#include <iterator>
#include <cstring>
#include <charconv>
#include <string>

#include "platform/ctr/keyboard.h"
#include "utils/utf8.hpp"
#include "utils/format.hpp"
#include "utils/language.h"

constexpr size_t MAX_TEXT_LENGTH = 255;

struct vkbdEvent {
	std::string hintText;
	std::string inText;
	void (*textInputFn)(std::string_view);
	void (*cancelFn)();
	int maxBytes;
	bool allowEmpty;
};

static vkbdEvent events[16];
static int eventCount = 0;

void ctr_vkbdInput(std::string_view hintText, std::string_view inText, void (*textInputFn)(std::string_view),
    void (*cancelFn)(), int maxBytes, bool allowEmpty)
{
	if (eventCount >= static_cast<int>(std::size(events)))
		return;

	vkbdEvent &event = events[eventCount];
	event.hintText = hintText;
	event.inText = inText;
	event.textInputFn = textInputFn;
	event.cancelFn = cancelFn;
	event.maxBytes = std::clamp(maxBytes, 1, static_cast<int>(MAX_TEXT_LENGTH));
	event.allowEmpty = allowEmpty;
	eventCount++;
}

void ctr_vkbdClear() { eventCount = 0; }

void ctr_vkbdFlush()
{
	if (eventCount == 0) return;
	// Remove the request before invoking callbacks. A callback may replace the
	// menu and queue its next keyboard, which must wait for that menu to render.
	vkbdEvent event = std::move(events[0]);
	for (int i = 1; i < eventCount; ++i) events[i - 1] = std::move(events[i]);
	--eventCount;
	{
		SwkbdState swkbd;

		swkbdInit(&swkbd, SWKBD_TYPE_WESTERN, 2, event.maxBytes);
		swkbdSetValidation(&swkbd, event.allowEmpty ? SWKBD_ANYTHING : SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
		const std::string tooLong { _("Text is too long.") };
		struct Limit { int bytes; const char *message; } limit { event.maxBytes, tooLong.c_str() };
		swkbdSetFilterCallback(&swkbd, [](void *user, const char **message, const char *, size_t length) {
			const auto &limit = *static_cast<const Limit *>(user);
			if (length > static_cast<size_t>(limit.bytes)) {
				*message = limit.message;
				return SWKBD_CALLBACK_CONTINUE;
			}
			return SWKBD_CALLBACK_OK;
		}, &limit);

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
		} else if (event.cancelFn) {
			event.cancelFn();
		}
	}

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

void ctr_vkbdChat(int maxBytes, void *context,
    void (*send)(void *, std::string_view), bool (*refresh)(void *))
{
	// The system applet owns GSP while open. Refresh between sends, then reopen
	// with an empty input field; only Cancel ends this conversation session.
	std::string buffer(maxBytes, '\0');
	while (refresh(context)) {
		SwkbdState keyboard;
		swkbdInit(&keyboard, SWKBD_TYPE_WESTERN, 2, maxBytes - 1);
		swkbdSetValidation(&keyboard, SWKBD_NOTEMPTY_NOTBLANK, 0, 0);
		const std::string hint { _("Talk") };
		const std::string cancel { _("Cancel") };
		const std::string submit { _("Send") };
		swkbdSetHintText(&keyboard, hint.c_str());
		swkbdSetButton(&keyboard, SWKBD_BUTTON_LEFT, cancel.c_str(), false);
		swkbdSetButton(&keyboard, SWKBD_BUTTON_RIGHT, submit.c_str(), true);
		std::fill(buffer.begin(), buffer.end(), '\0');
		if (swkbdInputText(&keyboard, buffer.data(), buffer.size()) != SWKBD_BUTTON_CONFIRM)
			break;
		send(context, std::string_view(buffer.c_str()));
	}
}
