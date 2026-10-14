#pragma once

namespace devilution {
// Disk I/O runs on a dedicated worker, never in the packet callback.
void CtrZeroTierLogStart();
void CtrZeroTierLogStop();
void CtrZeroTierLogStage(const char *stage, int result = 0);
void CtrZeroTierLogTrace(unsigned kind, int value);
}
