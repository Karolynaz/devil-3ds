#pragma once
#include <cstdint>
#include "engine/point.hpp"

namespace devilution {
// No items are removed while dragging: an outside release is a safe cancel.
struct CtrBeltGesture {
	int slot = -1;
	Point origin {};
	uint32_t started = 0;
	bool dragging = false;
	bool used = false;
	void Begin(int value, Point point, uint32_t now) {
		slot = value; origin = point; started = now; dragging = false; used = false;
	}
	bool Update(Point point, uint32_t now) {
		if (slot < 0 || used) return false;
		const int dx = point.x - origin.x, dy = point.y - origin.y;
		if (dx * dx + dy * dy >= 25) dragging = true;
		if (!dragging && static_cast<uint32_t>(now - started) >= 450) {
			used = true;
			return true;
		}
		return false;
	}
	void Cancel() { slot = -1; }
};
}
