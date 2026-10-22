#pragma once

#ifdef USE_SDL3
#include <SDL3/SDL_events.h>
#else
#include <SDL.h>
#endif

#ifdef __3DS__
#include "platform/ctr/system.h"
#endif

#include "controls/controller.h"
#include "controls/controller_motion.h"

namespace devilution {

inline bool PollEvent(SDL_Event *event)
{
#ifdef USE_SDL3
	const bool result = SDL_PollEvent(event);
#else
	const bool result = SDL_PollEvent(event) != 0;
#endif
#ifdef __3DS__
	ctr_process_resume();
#endif
	if (result) {
		UnlockControllerState(*event);
		ProcessControllerMotion(*event);
	}

	return result;
}

} // namespace devilution
