#include "platform/ctr/zoom_slider.hpp"

#include <3ds.h>

#include "platform/ctr/cfgu_service.hpp"

namespace devilution {

bool CtrHasZoomSlider()
{
	static const bool hasSlider = [] {
		n3ds::CFGUService cfguService;
		if (!cfguService.IsInitialized())
			return true; // Unknown model: allow it. The slider then reads 0 (100%).
		u8 model;
		if (!R_SUCCEEDED(CFGU_GetSystemModel(&model)))
			return true;
		return model != CFG_MODEL_2DS && model != CFG_MODEL_N2DSXL;
	}();
	return hasSlider;
}

float CtrReadZoomSlider()
{
	return osGet3DSliderState();
}

} // namespace devilution
