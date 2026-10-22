#pragma once
namespace devilution {
inline void (*CtrNetworkProgressCallback)() = nullptr;
inline void CtrPumpNetworkProgress()
{
	if (CtrNetworkProgressCallback != nullptr) CtrNetworkProgressCallback();
}
} // namespace devilution
