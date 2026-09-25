#include "panels/quest_log.hpp"

#ifdef __3DS__
#include "platform/ctr/ui.hpp"
#endif

#include <algorithm>
#include <string_view>

#include "DiabloUI/ui_flags.hpp"
#include "control/control.hpp"
#include "effects.h"
#ifdef __3DS__
#include "controls/plrctrls.h"
#include "inv.h"
#include "utils/str_cat.hpp"
#endif
#include "engine/render/clx_render.hpp"
#include "engine/render/text_render.hpp"
#include "minitext.h"
#include "panels/ui_panels.hpp"
#include "quests.h"
#include "utils/language.h"

namespace devilution {

bool QuestLogIsOpen;
OptionalOwnedClxSpriteList pQLogCel;

namespace {

/** Indices of quests to display in quest log window. `FirstFinishedQuest` are active quests the rest are completed */
quest_id EncounteredQuests[MAXQUESTS];
/** Overall number of EncounteredQuests entries */
int EncounteredQuestCount;
/** First (nonselectable) finished quest in list */
int FirstFinishedQuest;
/** Currently selected quest list item */
int SelectedQuest;
#ifdef __3DS__
constexpr int CtrQuestRows = 10;
constexpr int CtrQuestRowHeight = 18;
int QuestScrollOffset;
Point LastQuestMouse { -1, -1 };

int CtrQuestListY()
{
	return 34 + (CtrQuestRows - std::min(EncounteredQuestCount, CtrQuestRows)) * CtrQuestRowHeight / 2;
}

void FocusQuest3DS()
{
	if (SelectedQuest < QuestScrollOffset)
		QuestScrollOffset = SelectedQuest;
	if (SelectedQuest >= QuestScrollOffset + CtrQuestRows)
		QuestScrollOffset = SelectedQuest - CtrQuestRows + 1;
	QuestScrollOffset = std::max(0, QuestScrollOffset);
	if (SelectedQuest >= 0) {
		const Point p { 200, CtrQuestListY() + (SelectedQuest - QuestScrollOffset) * CtrQuestRowHeight + 6 };
		LastQuestMouse = CtrTopToScreen(p);
		SetCursorPos(LastQuestMouse);
	}
}
#endif

constexpr Rectangle InnerPanel { { 32, 26 }, { 280, 300 } };
constexpr int LineHeight = 12;
constexpr int MaxSpacing = LineHeight * 2;
int ListYOffset;
int LineSpacing;
/** The number of pixels to move finished quest, to separate them from the active ones */
int FinishedQuestOffset;

int QuestLogMouseToEntry()
{
#ifdef __3DS__
	const Point p = CtrScreenToTop(MousePosition);
	const int rows = std::min(CtrQuestRows, EncounteredQuestCount - QuestScrollOffset);
	const Rectangle area { { 20, CtrQuestListY() }, { 360, rows * CtrQuestRowHeight } };
	if (!area.contains(p))
		return -1;
	return QuestScrollOffset + (p.y - area.position.y) / CtrQuestRowHeight;
#else
	Rectangle innerArea = InnerPanel;
	innerArea.position += Displacement(GetLeftPanel().position.x, GetLeftPanel().position.y);
	if (!innerArea.contains(MousePosition) || (EncounteredQuestCount == 0))
		return -1;
	const int y = MousePosition.y - innerArea.position.y;
	for (int i = 0; i < FirstFinishedQuest; i++) {
		if ((y >= ListYOffset + i * LineSpacing)
		    && (y < ListYOffset + i * LineSpacing + LineHeight)) {
			return i;
		}
	}
	return -1;
#endif
}

void PrintQLString(const Surface &out, int x, int y, std::string_view str, bool marked, bool disabled = false)
{
	const int width = GetLineWidth(str);
	x += std::max((257 - width) / 2, 0);
	if (marked) {
		ClxDraw(out, GetPanelPosition(UiPanels::Quest, { x - 20, y + 13 }), (*pSPentSpn2Cels)[PentSpn2Spin()]);
	}
	DrawString(out, str, { GetPanelPosition(UiPanels::Quest, { x, y }), { 257, 0 } },
	    { .flags = disabled ? UiFlags::ColorWhitegold : UiFlags::ColorWhite });
	if (marked) {
		ClxDraw(out, GetPanelPosition(UiPanels::Quest, { x + width + 7, y + 13 }), (*pSPentSpn2Cels)[PentSpn2Spin()]);
	}
}

} // namespace

void DrawQuestLog(const Surface &out)
{
#ifdef __3DS__
	if (MousePosition != LastQuestMouse) {
		const int hovered = QuestLogMouseToEntry();
		if (hovered >= 0)
			SelectedQuest = hovered;
		LastQuestMouse = MousePosition;
	}
	DrawCtrPanelBackground(out, CtrPanelBackground::Quest);
	DrawString(out, _("Quest Log"), { { 12, 7 }, { 376, 24 } }, { .flags = UiFlags::AlignCenter | UiFlags::FontSize24 | UiFlags::ColorWhitegold });
	const int count = std::min(CtrQuestRows, EncounteredQuestCount - QuestScrollOffset);
	for (int row = 0; row < count; ++row) {
		const int index = QuestScrollOffset + row;
		const int y = CtrQuestListY() + row * CtrQuestRowHeight;
		const bool selected = index == SelectedQuest;
		if (selected)
			FillRect(out, 18, y - 1, 364, CtrQuestRowHeight, PAL16_GRAY + 13);
		DrawString(out, _(QuestsData[EncounteredQuests[index]]._qlstr), { { 32, y }, { 336, CtrQuestRowHeight } },
		    { .flags = UiFlags::AlignCenter | UiFlags::KerningFitSpacing | (index >= FirstFinishedQuest ? UiFlags::ColorWhitegold : UiFlags::ColorWhite) });
		if (selected && index < FirstFinishedQuest)
			ClxDraw(out, { 18, y + 13 }, (*pSPentSpn2Cels)[PentSpn2Spin()]);
	}
	if (EncounteredQuestCount > CtrQuestRows) {
		DrawString(out, StrCat(QuestScrollOffset + 1, "-", QuestScrollOffset + count, " / ", EncounteredQuestCount), { { 115, 220 }, { 170, 16 } }, { .flags = UiFlags::AlignCenter | UiFlags::ColorWhitegold });
		DrawString(out, "<", { { 30, 220 }, { 30, 16 } }, { .flags = UiFlags::ColorWhite });
		DrawString(out, ">", { { 340, 220 }, { 30, 16 } }, { .flags = UiFlags::ColorWhite });
	}
	return;
#endif
	const int l = QuestLogMouseToEntry();
	if (l >= 0) {
		SelectedQuest = l;
	}
	const auto x = InnerPanel.position.x;
	ClxDraw(out, GetPanelPosition(UiPanels::Quest, { 0, 351 }), (*pQLogCel)[0]);
	int y = InnerPanel.position.y + ListYOffset;
	for (int i = 0; i < EncounteredQuestCount; i++) {
		if (i == FirstFinishedQuest) {
			y += FinishedQuestOffset;
		}
		PrintQLString(out, x, y, _(QuestsData[EncounteredQuests[i]]._qlstr), i == SelectedQuest, i >= FirstFinishedQuest);
		y += LineSpacing;
	}
}

void StartQuestlog()
{
	auto sortQuestIdx = [](int a, int b) {
		return QuestsData[a].questBookOrder < QuestsData[b].questBookOrder;
	};

	EncounteredQuestCount = 0;
	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_ACTIVE && quest._qlog) {
			EncounteredQuests[EncounteredQuestCount] = quest._qidx;
			EncounteredQuestCount++;
		}
	}
	FirstFinishedQuest = EncounteredQuestCount;
	for (auto &quest : Quests) {
		if (quest._qactive == QUEST_DONE || quest._qactive == QUEST_HIVE_DONE) {
			EncounteredQuests[EncounteredQuestCount] = quest._qidx;
			EncounteredQuestCount++;
		}
	}

	std::sort(&EncounteredQuests[0], &EncounteredQuests[FirstFinishedQuest], sortQuestIdx);
	std::sort(&EncounteredQuests[FirstFinishedQuest], &EncounteredQuests[EncounteredQuestCount], sortQuestIdx);

	const bool twoBlocks = FirstFinishedQuest != 0 && FirstFinishedQuest < EncounteredQuestCount;

	ListYOffset = 0;
	FinishedQuestOffset = !twoBlocks ? 0 : LineHeight / 2;

	const int overallMinHeight = (EncounteredQuestCount * LineHeight) + FinishedQuestOffset;
	const int space = InnerPanel.size.height;

	if (EncounteredQuestCount > 0) {
		const int additionalSpace = space - overallMinHeight;
		int addLineSpacing = additionalSpace / EncounteredQuestCount;
		addLineSpacing = std::min(MaxSpacing - LineHeight, addLineSpacing);
		LineSpacing = LineHeight + addLineSpacing;
		if (twoBlocks) {
			int additionalSepSpace = additionalSpace - (addLineSpacing * EncounteredQuestCount);
			additionalSepSpace = std::min(LineHeight, additionalSepSpace);
			FinishedQuestOffset = std::max(4, additionalSepSpace);
		}

		const int overallHeight = (EncounteredQuestCount * LineSpacing) + FinishedQuestOffset;
		ListYOffset += (space - overallHeight) / 2;
	}

	SelectedQuest = FirstFinishedQuest == 0 ? -1 : 0;
	QuestLogIsOpen = true;
#ifdef __3DS__
	invflag = false;
	SpellbookFlag = false;
	CloseGoldDrop();
	QuestScrollOffset = 0;
	SelectedQuest = EncounteredQuestCount == 0 ? -1 : 0;
	FocusQuest3DS();
#endif
}

void QuestlogUp()
{
#ifdef __3DS__
	if (EncounteredQuestCount > 0) {
		SelectedQuest = (SelectedQuest + EncounteredQuestCount + -1) % EncounteredQuestCount;
		FocusQuest3DS();
		PlaySFX(SfxID::MenuMove);
	}
	return;
#endif
	if (FirstFinishedQuest == 0) {
		SelectedQuest = -1;
	} else {
		SelectedQuest--;
		if (SelectedQuest < 0) {
			SelectedQuest = FirstFinishedQuest - 1;
		}
		PlaySFX(SfxID::MenuMove);
	}
}

void QuestlogDown()
{
#ifdef __3DS__
	if (EncounteredQuestCount > 0) {
		SelectedQuest = (SelectedQuest + EncounteredQuestCount + 1) % EncounteredQuestCount;
		FocusQuest3DS();
		PlaySFX(SfxID::MenuMove);
	}
	return;
#endif
	if (FirstFinishedQuest == 0) {
		SelectedQuest = -1;
	} else {
		SelectedQuest++;
		if (SelectedQuest == FirstFinishedQuest) {
			SelectedQuest = 0;
		}
		PlaySFX(SfxID::MenuMove);
	}
}

void QuestlogEnter()
{
#ifdef __3DS__
	if (SelectedQuest < 0 || SelectedQuest >= FirstFinishedQuest)
		return;
#endif
	PlaySFX(SfxID::MenuSelect);
	if (EncounteredQuestCount != 0 && SelectedQuest >= 0 && SelectedQuest < FirstFinishedQuest)
		InitQTextMsg(Quests[EncounteredQuests[SelectedQuest]]._qmsg);
	QuestLogIsOpen = false;
}

void QuestlogESC()
{
#ifdef __3DS__
	const Point p = CtrScreenToTop(MousePosition);
	if (EncounteredQuestCount > CtrQuestRows && p.y >= 218) {
		const int delta = p.x < 100 ? -CtrQuestRows : (p.x >= 300 ? CtrQuestRows : 0);
		SelectedQuest = std::clamp(SelectedQuest + delta, 0, EncounteredQuestCount - 1);
		FocusQuest3DS();
		return;
	}
#endif
	const int l = QuestLogMouseToEntry();
	if (l != -1) {
		SelectedQuest = l;
		QuestlogEnter();
	}
}

} // namespace devilution
