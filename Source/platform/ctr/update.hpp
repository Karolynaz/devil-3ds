#pragma once
#include <optional>
#include <string>
#include <string_view>
namespace devilution {
enum class CtrUpdateState { Available, Current, Different, Error };
struct CtrUpdateResult { CtrUpdateState state; std::string version; std::string detail; };
inline constexpr std::string_view CtrProjectUrl = "github.com/Karolynaz/devil-3ds";
void CtrCheckForUpdates();
bool CtrUpdateBusy();
std::optional<CtrUpdateResult> CtrPollUpdate();
std::string_view CtrBuildCommit();
std::string_view CtrPortVersion();
} // namespace devilution
