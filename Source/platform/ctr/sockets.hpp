#pragma once

#include <string>

namespace devilution {

bool n3ds_socInit();
void n3ds_socExit();
bool n3ds_initSecureRandom();
std::string n3ds_networkError();

} // namespace devilution
