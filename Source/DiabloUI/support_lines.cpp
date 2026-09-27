#include <cstddef>

#include "DiabloUI/support_lines.h"
#include "utils/language.h"

namespace devilution {

const char *const SupportLines[] = {
	"",
	N_("For help, bug reports, and updates, visit the Devil-3Ds project: https://github.com/Karolynaz/devil-3ds"),
	"",
	N_("Devil-3Ds is maintained by Karolynaz. Include your console model, game version, and steps to reproduce when reporting a problem."),
	"",
	"",
	N_("Disclaimer:"),
	N_("	Devil-3Ds is an independent fan project. Blizzard Entertainment and GOG.com do not support or certify it. Contact the project maintainer, not Blizzard or GOG.com, with questions about this port."),
	"",
	"",
	N_("	This port makes use of Charis SIL, New Athena Unicode, Unifont, and Noto which are licensed under the SIL Open Font License, as well as Twitmoji which is licensed under CC-BY 4.0. The port also makes use of SDL which is licensed under the zlib-license. See the ReadMe for further details."),
	"",
	"",
};

const std::size_t SupportLinesSize = sizeof(SupportLines) / sizeof(SupportLines[0]);

} // namespace devilution
