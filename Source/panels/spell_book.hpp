#pragma once

#include <expected>
#include <string>

#ifdef __3DS__
#include "controls/controller_buttons.h"
#endif
#include "engine/clx_sprite.hpp"
#include "engine/surface.hpp"

namespace devilution {

std::expected<void, std::string> InitSpellBook();
void FreeSpellBook();
void CheckSBook();
void DrawSpellBook(const Surface &out);
#ifdef __3DS__
int CtrSpellbookPageCount();
int CtrSpellbookRowCount();
bool CtrSpellBindingActive();
bool HandleCtrSpellBinding(ControllerButton button, bool modifierHeld);
int CtrSpellBindingSlot(ControllerButton button, bool modifierHeld);
void DrawCtrSpellDescription(const Surface &out);
#endif

} // namespace devilution
