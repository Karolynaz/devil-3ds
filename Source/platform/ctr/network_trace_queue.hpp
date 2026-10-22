#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace devilution {
struct CtrNetworkTrace {
	uint32_t milliseconds;
	unsigned kind;
	int value;
	std::array<char, 80> stage {};
};
// The caller holds its platform lock. A bounded queue keeps packet workers
// independent of SD latency and limits memory use during a failed connection.
class CtrNetworkTraceQueue {
public:
	bool push(const CtrNetworkTrace &event)
	{
		if (count_ == entries_.size()) {
			++dropped_;
			return false;
		}
		entries_[(first_ + count_) % entries_.size()] = event;
		++count_;
		return true;
	}
	bool pop(CtrNetworkTrace &event)
	{
		if (count_ == 0) return false;
		event = entries_[first_];
		first_ = (first_ + 1) % entries_.size();
		--count_;
		return true;
	}
	uint32_t takeDropped()
	{
		const uint32_t result = dropped_;
		dropped_ = 0;
		return result;
	}
private:
	std::array<CtrNetworkTrace, 128> entries_ {};
	size_t first_ = 0;
	size_t count_ = 0;
	uint32_t dropped_ = 0;
};
}
