/**
 * @file scrollrt.cpp
 *
 * Implementation of functionality for rendering the dungeons, monsters and calling other render routines.
 */
#include "engine/render/scrollrt.h"
#ifdef __3DS__
#include "platform/ctr/ui.hpp"
#include "platform/ctr/world_surface.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>

#ifdef USE_SDL3
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_rect.h>
#include <SDL3/SDL_timer.h>
#else
#include <SDL.h>
#endif

#include <ankerl/unordered_dense.h>

#include "DiabloUI/ui_flags.hpp"
#include "automap.h"
#include "controls/control_mode.hpp"
#include "controls/plrctrls.h"
#include "cursor.h"
#include "dead.h"
#include "diablo_msg.hpp"
#include "doom.h"
#include "engine/backbuffer_state.hpp"
#include "engine/displacement.hpp"
#include "engine/dx.h"
#include "engine/point.hpp"
#include "engine/render/clx_render.hpp"
#include "engine/render/dun_render.hpp"
#include "engine/render/light_render.hpp"
#include "engine/render/text_render.hpp"
#include "engine/trn.hpp"
#include "engine/world_tile.hpp"
#include "game_mode.hpp"
#include "gmenu.h"
#include "headless_mode.hpp"
#include "help.h"
#include "hwcursor.hpp"
#include "init.hpp"
#include "inv.h"
#include "levels/dun_tile.hpp"
#include "levels/gendung.h"
#include "levels/tile_properties.hpp"
#include "lighting.h"
#include "lua/lua_event.hpp"
#include "minitext.h"
#include "missiles.h"
#include "nthread.h"
#include "options.h"
#include "panels/charpanel.hpp"
#include "panels/console.hpp"
#include "panels/partypanel.hpp"
#include "panels/quest_log.hpp"
#include "panels/spell_book.hpp"
#include "panels/spell_list.hpp"
#include "plrmsg.h"
#include "qol/chatlog.h"
#include "qol/floatingnumbers.h"
#include "qol/itemlabels.h"
#include "qol/monhealthbar.h"
#include "qol/stash.h"
#include "qol/visual_store.h"
#include "qol/xpbar.h"
#include "stores.h"
#include "tables/playerdat.hpp"
#include "towners.h"
#include "utils/attributes.h"
#include "utils/display.h"
#include "utils/is_of.hpp"
#include "utils/language.h"
#include "utils/log.hpp"
#include "utils/sdl_compat.h"
#include "utils/sdl_thread.h"
#include "utils/str_cat.hpp"

#ifndef USE_SDL1
#include "controls/touch/renderers.h"
#endif

#ifdef _DEBUG
#include "debug.h"
#endif

#ifdef DUN_RENDER_STATS
#include "utils/format_int.hpp"
#endif

namespace devilution {

enum OutlineColors : uint8_t {
	OutlineColorsPlayer1 = (PAL16_ORANGE + 7),
	OutlineColorsPlayer2 = (PAL16_YELLOW + 7),
	OutlineColorsPlayer3 = (PAL16_RED + 7),
	OutlineColorsPlayer4 = (PAL16_BLUE + 7),
	OutlineColorsObject = (PAL16_YELLOW + 2),
	OutlineColorsTowner = (PAL16_BEIGE + 6),
	OutlineColorsMonster = (PAL16_RED + 9),
};

bool AutoMapShowItems;

// DevilutionX extension.
extern void DrawControllerModifierHints(const Surface &out);

bool frameflag;

namespace {

constexpr auto RightFrameDisplacement = Displacement { DunFrameWidth, 0 };

[[nodiscard]] DVL_ALWAYS_INLINE bool IsFloor(Point tilePosition)
{
	return !TileHasAny(tilePosition, TileProperties::Solid | TileProperties::BlockMissile);
}

[[nodiscard]] DVL_ALWAYS_INLINE bool IsWall(Point tilePosition)
{
	return !IsFloor(tilePosition) || dSpecial[tilePosition.x][tilePosition.y] != 0;
}

/**
 * @brief Contains all Missile at rendering position
 */
ankerl::unordered_dense::map<WorldTilePosition, std::vector<Missile *>> MissilesAtRenderingTile;

/**
 * @brief Could the missile (at the next game tick) collide? This method is a simplified version of CheckMissileCol (for example without random).
 */
bool CouldMissileCollide(Point tile, bool checkPlayerAndMonster)
{
	if (!InDungeonBounds(tile))
		return true;
	if (checkPlayerAndMonster) {
		if (dMonster[tile.x][tile.y] > 0)
			return true;
		if (dPlayer[tile.x][tile.y] > 0)
			return true;
	}

	return IsMissileBlockedByTile(tile);
}

void UpdateMissilePositionForRendering(Missile &m, int progress)
{
	DisplacementOf<int64_t> velocity = m.position.velocity;
	velocity *= progress;
	velocity /= AnimationInfo::baseValueFraction;
	const Displacement pixelsTravelled = (m.position.traveled + Displacement { static_cast<int>(velocity.deltaX), static_cast<int>(velocity.deltaY) }) >> 16;
	const Displacement tileOffset = pixelsTravelled.screenToMissile();

	// calculate the future missile position
	m.position.tileForRendering = m.position.start + tileOffset;
	m.position.offsetForRendering = pixelsTravelled + tileOffset.worldToScreen();
}

void UpdateMissileRendererData(Missile &m)
{
	m.position.tileForRendering = m.position.tile;
	m.position.offsetForRendering = m.position.offset;

	const MissileMovementDistribution missileMovement = GetMissileData(m._mitype).movementDistribution;
	// don't calculate missile position if they don't move
	if (missileMovement == MissileMovementDistribution::Disabled || m.position.velocity == Displacement {})
		return;

	int progress = ProgressToNextGameTick;
	UpdateMissilePositionForRendering(m, progress);

	// In some cases this calculated position is invalid.
	// For example a missile shouldn't move inside a wall.
	// In this case the game logic don't advance the missile position and removes the missile or shows an explosion animation at the old position.
	// For the animation distribution logic this means we are not allowed to move to a tile where the missile could collide, because this could be a invalid position.

	// If we are still at the current tile, this tile was already checked and is a valid tile
	if (m.position.tileForRendering == m.position.tile)
		return;

	// If no collision can happen at the new tile we can advance
	if (!CouldMissileCollide(m.position.tileForRendering, missileMovement == MissileMovementDistribution::Blockable))
		return;

	// The new tile could be invalid, so don't advance to it.
	// We search the last offset that is in the old (valid) tile.
	// Implementation note: If someone knows the correct math to calculate this without the loop, I would really appreciate it.
	while (m.position.tile != m.position.tileForRendering) {
		progress -= 1;

		if (progress <= 0) {
			m.position.tileForRendering = m.position.tile;
			m.position.offsetForRendering = m.position.offset;
			return;
		}

		UpdateMissilePositionForRendering(m, progress);
	}
}

void UpdateMissilesRendererData()
{
	MissilesAtRenderingTile.clear();

	for (auto &m : Missiles) {
		UpdateMissileRendererData(m);
		MissilesAtRenderingTile[m.position.tileForRendering].push_back(&m);
	}
}

uint32_t lastFpsUpdateInMs;

Rectangle PrevCursorRect;

void BlitCursor(uint8_t *dst, uint32_t dstPitch, uint8_t *src, uint32_t srcPitch, uint32_t srcWidth, uint32_t srcHeight)
{
	for (std::uint32_t i = 0; i < srcHeight; ++i, src += srcPitch, dst += dstPitch) {
		memcpy(dst, src, srcWidth);
	}
}

/**
 * @brief Remove the cursor from the buffer
 */
void UndrawCursor(const Surface &out)
{
	DrawnCursor &cursor = GetDrawnCursor();
	BlitCursor(&out[cursor.rect.position], out.pitch(), cursor.behindBuffer, cursor.rect.size.width, cursor.rect.size.width, cursor.rect.size.height);
	PrevCursorRect = cursor.rect;
}

bool ShouldShowCursor()
{
#ifdef __3DS__
	if (IsPlayerInStore() || qtextflag)
		return false;
	if (!invflag && !CharFlag && !SpellbookFlag && !QuestLogIsOpen && !IsStashOpen && !IsVisualStoreOpen && !gmenu_is_active())
		return false;
#endif
	if (ControlMode == ControlTypes::KeyboardAndMouse)
		return true;
	if (pcurs == CURSOR_TELEPORT)
		return true;
	if (invflag)
		return true;
	if (CharFlag)
		return true;
	if (SpellbookFlag)
		return true;
	if (QuestLogIsOpen)
		return true;
	if (IsPlayerInStore())
		return true;
	if (qtextflag)
		return true;
	if (IsStashOpen || IsVisualStoreOpen)
		return true;

	return false;
}

/**
 * @brief Blit CL2 sprite, and apply lighting, to the given buffer at the given coordinates
 * @param out Output buffer
 * @param position Target buffer coordinate
 * @param clx CLX frame
 */
inline void ClxDrawLight(const Surface &out, Point position, ClxSprite clx, int lightTableIndex)
{
	if (lightTableIndex != 0) {
		ClxDrawTRN(out, position, clx, LightTables[lightTableIndex].data());
	} else {
		ClxDraw(out, position, clx);
	}
}

/**
 * @brief Blit CL2 sprite, and apply lighting and transparency blending, to the given buffer at the given coordinates
 * @param out Output buffer
 * @param position Target buffer coordinate
 * @param clx CLX frame
 */
inline void ClxDrawLightBlended(const Surface &out, Point position, ClxSprite clx, int lightTableIndex)
{
	if (lightTableIndex != 0) {
		ClxDrawBlendedTRN(out, position, clx, LightTables[lightTableIndex].data());
	} else {
		ClxDrawBlended(out, position, clx);
	}
}

/**
 * @brief Save the content behind the cursor to a temporary buffer, then draw the cursor.
 */
void DrawCursor(const Surface &out)
{
	DrawnCursor &cursor = GetDrawnCursor();
	if (IsHardwareCursor()) {
		SetHardwareCursorVisible(ShouldShowCursor());
		cursor.rect.size = { 0, 0 };
		return;
	}

	if (pcurs <= CURSOR_NONE || !ShouldShowCursor()) {
		cursor.rect.size = { 0, 0 };
		return;
	}

	const Size cursSize = GetInvItemSize(pcurs);
	if (cursSize.width == 0 || cursSize.height == 0) {
		cursor.rect.size = { 0, 0 };
		return;
	}
#ifdef __3DS__
	if (!MyPlayer->HoldItem.isEmpty()) {
		const Size slots = GetInventorySize(MyPlayer->HoldItem);
		const int physicalScreenWidth = MousePosition.y < 240 ? CtrTopSize.width : 320;
		const Size drawnSize {
			((slots.width * CtrItemSlotPitch - 1) * gnScreenWidth + physicalScreenWidth - 1) / physicalScreenWidth,
			slots.height * CtrItemSlotPitch - 1,
		};
		const Point position = MousePosition - Displacement { drawnSize / 2 };
		const Rectangle target { position, drawnSize };
		Rectangle &rect = cursor.rect;
		rect.position = { std::max(0, target.position.x), std::max(0, target.position.y) };
		rect.size = { std::max(0, std::min(out.w(), target.position.x + target.size.width) - rect.position.x),
		    std::max(0, std::min(out.h(), target.position.y + target.size.height) - rect.position.y) };
		if (rect.size.width != 0 && rect.size.height != 0) {
			BlitCursor(cursor.behindBuffer, rect.size.width, &out[rect.position], out.pitch(), rect.size.width, rect.size.height);
			DrawCtrScaledItem(out, MyPlayer->HoldItem, target, true);
		}
		return;
	}
#endif

	constexpr auto Clip = [](int &pos, int &length, int posEnd) {
		if (pos + length <= 0 || pos >= posEnd) {
			pos = 0;
			length = 0;
		} else if (pos < 0) {
			length += pos;
			pos = 0;
		} else if (pos + length > posEnd) {
			length = posEnd - pos;
		}
	};

	// Copy the buffer before the item cursor and its 1px outline are drawn to a temporary buffer.
	const int outlineWidth = !MyPlayer->HoldItem.isEmpty() ? 1 : 0;
	const Displacement offset = !MyPlayer->HoldItem.isEmpty() ? Displacement { cursSize / 2 } : Displacement { 0 };
	const Point cursPosition = MousePosition - offset;

	Rectangle &rect = cursor.rect;
	rect.position.x = cursPosition.x - outlineWidth;
	rect.size.width = cursSize.width + 2 * outlineWidth;
	Clip(rect.position.x, rect.size.width, out.w());

	rect.position.y = cursPosition.y - outlineWidth;
	rect.size.height = cursSize.height + 2 * outlineWidth;
	Clip(rect.position.y, rect.size.height, out.h());

	if (rect.size.width == 0 || rect.size.height == 0)
		return;

	BlitCursor(cursor.behindBuffer, rect.size.width, &out[rect.position], out.pitch(), rect.size.width, rect.size.height);
	DrawSoftwareCursor(out, cursPosition + Displacement { 0, cursSize.height - 1 }, pcurs);
}

/**
 * @brief Render a missile sprite
 * @param out Output buffer
 * @param missile Pointer to Missile struct
 * @param targetBufferPosition Output buffer coordinate
 * @param pre Is the sprite in the background
 */
void DrawMissilePrivate(const Surface &out, const Missile &missile, Point targetBufferPosition, bool pre, int lightTableIndex)
{
#ifdef __3DS__
	targetBufferPosition.x += CtrStereoSpriteOffset();
#endif
	if (missile._miPreFlag != pre || !missile._miDrawFlag)
		return;

	const Point missileRenderPosition { targetBufferPosition + missile.position.offsetForRendering - Displacement { missile._miAnimWidth2, 0 } };
	const ClxSprite sprite = (*missile._miAnimData)[missile._miAnimFrame - 1];
	if (missile._miUniqTrans != 0) {
		ClxDrawTRN(out, missileRenderPosition, sprite, Monsters[missile._misource].uniqueMonsterTRN.get());
	} else if (missile._miLightFlag) {
		ClxDrawLight(out, missileRenderPosition, sprite, lightTableIndex);
	} else {
		ClxDraw(out, missileRenderPosition, sprite);
	}
}

/**
 * @brief Render a missile sprites for a given tile
 * @param out Output buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 * @param pre Is the sprite in the background
 */
void DrawMissile(const Surface &out, WorldTilePosition tilePosition, Point targetBufferPosition, bool pre, int lightTableIndex)
{
	const auto it = MissilesAtRenderingTile.find(tilePosition);
	if (it == MissilesAtRenderingTile.end()) return;
	for (Missile *missile : it->second) {
		DrawMissilePrivate(out, *missile, targetBufferPosition, pre, lightTableIndex);
	}
}

/**
 * @brief Render a monster sprite
 * @param out Output buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 * @param monster Monster reference
 */
void DrawMonster(const Surface &out, Point tilePosition, Point targetBufferPosition, const Monster &monster, int lightTableIndex)
{
	if (!monster.animInfo.sprites) {
		Log("Draw Monster \"{}\": NULL Cel Buffer", monster.name());
		return;
	}

	const ClxSprite sprite = monster.animInfo.currentSprite();

	if (!IsTileLit(tilePosition)) {
		ClxDrawTRN(out, targetBufferPosition, sprite, GetInfravisionTRN());
		return;
	}
	uint8_t *trn = nullptr;
	if (monster.isUnique())
		trn = monster.uniqueMonsterTRN.get();
	if (monster.mode == MonsterMode::Petrified)
		trn = GetStoneTRN();
	if (MyPlayer->_pInfraFlag && lightTableIndex > 8)
		trn = GetInfravisionTRN();
	if (trn != nullptr)
		ClxDrawTRN(out, targetBufferPosition, sprite, trn);
	else
		ClxDrawLight(out, targetBufferPosition, sprite, lightTableIndex);
}

/**
 * @brief Helper for rendering a specific player icon (Mana Shield or Reflect)
 */
void DrawPlayerIconHelper(const Surface &out, MissileGraphicID missileGraphicId, Point position, const Player &player, bool infraVision, int lightTableIndex)
{
	const bool lighting = &player != MyPlayer;

	if (player.isWalking())
		position += GetOffsetForWalking(player.AnimInfo, player._pdir);

	position.x -= GetMissileSpriteData(missileGraphicId).animWidth2;

	const ClxSprite sprite = (*GetMissileSpriteData(missileGraphicId).sprites).list()[0];

	if (!lighting) {
		ClxDraw(out, position, sprite);
		return;
	}

	if (infraVision) {
		ClxDrawTRN(out, position, sprite, GetInfravisionTRN());
		return;
	}

	ClxDrawLight(out, position, sprite, lightTableIndex);
}

/**
 * @brief Helper for rendering player icons (Mana Shield and Reflect)
 * @param out Output buffer
 * @param player Player reference
 * @param position Output buffer coordinates
 * @param infraVision Should infravision be applied
 */
void DrawPlayerIcons(const Surface &out, const Player &player, Point position, bool infraVision, int lightTableIndex)
{
	if (player.pManaShield)
		DrawPlayerIconHelper(out, MissileGraphicID::ManaShield, position, player, infraVision, lightTableIndex);
	if (player.wReflections > 0)
		DrawPlayerIconHelper(out, MissileGraphicID::Reflect, position + Displacement { 0, 16 }, player, infraVision, lightTableIndex);
}

uint8_t GetPlayerOutlineColor(int id)
{
	static constexpr uint8_t PlayerOutlineColors[] = {
		OutlineColorsPlayer1,
		OutlineColorsPlayer2,
		OutlineColorsPlayer3,
		OutlineColorsPlayer4,
	};

	if (id < 0 || id >= static_cast<int>(SDL_arraysize(PlayerOutlineColors)))
		return OutlineColorsPlayer1;

	return PlayerOutlineColors[id];
}

/**
 * @brief Render a player sprite
 * @param out Output buffer
 * @param player Player reference
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 */
void DrawPlayer(const Surface &out, const Player &player, Point tilePosition, Point targetBufferPosition, int lightTableIndex)
{
#ifdef __3DS__
	targetBufferPosition.x += CtrStereoSpriteOffset();
#endif
	if (!IsTileLit(tilePosition) && !MyPlayer->_pInfraFlag && !MyPlayer->isOnArenaLevel() && leveltype != DTYPE_TOWN) {
		return;
	}

	const ClxSprite sprite = player.currentSprite();
	const Point spriteBufferPosition = targetBufferPosition + player.getRenderingOffset(sprite);

	if (&player == PlayerUnderCursor)
		ClxDrawOutlineSkipColorZero(out, GetPlayerOutlineColor(player.getId()), spriteBufferPosition, sprite);

	if (&player == MyPlayer && IsNoneOf(leveltype, DTYPE_NEST, DTYPE_CRYPT)) {
		ClxDraw(out, spriteBufferPosition, sprite);
		DrawPlayerIcons(out, player, targetBufferPosition, /*infraVision=*/false, lightTableIndex);
		return;
	}

	if (!IsTileLit(tilePosition) || ((MyPlayer->_pInfraFlag || MyPlayer->isOnArenaLevel()) && lightTableIndex > 8)) {
		ClxDrawTRN(out, spriteBufferPosition, sprite, GetInfravisionTRN());
		DrawPlayerIcons(out, player, targetBufferPosition, /*infraVision=*/true, lightTableIndex);
		return;
	}

	lightTableIndex = std::max(lightTableIndex - 5, 0);
	ClxDrawLight(out, spriteBufferPosition, sprite, lightTableIndex);
	DrawPlayerIcons(out, player, targetBufferPosition, /*infraVision=*/false, lightTableIndex);
}

/**
 * @brief Render a player sprite
 * @param out Output buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 */
void DrawDeadPlayer(const Surface &out, Point tilePosition, Point targetBufferPosition, int lightTableIndex)
{
	dFlags[tilePosition.x][tilePosition.y] &= ~DungeonFlag::DeadPlayer;

	for (const Player &player : Players) {
		if (player.plractive && player.hasNoLife() && player.isOnActiveLevel() && player.position.tile == tilePosition) {
			dFlags[tilePosition.x][tilePosition.y] |= DungeonFlag::DeadPlayer;
			const Point playerRenderPosition { targetBufferPosition };
			DrawPlayer(out, player, tilePosition, playerRenderPosition, lightTableIndex);
		}
	}
}

/**
 * @brief Render an object sprite
 * @param out Output buffer
 * @param objectToDraw Dungeone object to draw
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 * @param pre Is the sprite in the background
 */
void DrawObject(const Surface &out, const Object &objectToDraw, Point tilePosition, Point targetBufferPosition, int lightTableIndex)
{
#ifdef __3DS__
	targetBufferPosition.x += CtrStereoSpriteOffset();
#endif
	const ClxSprite sprite = objectToDraw.currentSprite();

	const Point screenPosition = targetBufferPosition + objectToDraw.getRenderingOffset(sprite, tilePosition);

	if (&objectToDraw == ObjectUnderCursor) {
		ClxDrawOutlineSkipColorZero(out, OutlineColorsObject, screenPosition, sprite);
	}
	if (objectToDraw.applyLighting) {
		ClxDrawLight(out, screenPosition, sprite, lightTableIndex);
	} else {
		ClxDraw(out, screenPosition, sprite);
	}
}

static void DrawDungeon(const Surface & /*out*/, const Lightmap & /*lightmap*/, Point /*tilePosition*/, Point /*targetBufferPosition*/);

/**
 * @brief Render a cell
 * @param out Target buffer
 * @param lightmap Per-pixel light buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Target buffer coordinates
 */
void DrawCell(const Surface &out, const Lightmap lightmap, Point tilePosition, Point targetBufferPosition, int lightTableIndex)
{
	const uint16_t levelPieceId = dPiece[tilePosition.x][tilePosition.y];
	const MICROS *pMap = &DPieceMicros[levelPieceId];

	const uint8_t *tbl = LightTables[lightTableIndex].data();
	const uint8_t *foliageTbl = tbl;
#ifdef _DEBUG
	int walkpathIdx = -1;
	Point originalTargetBufferPosition;
	if (DebugPath) {
		walkpathIdx = MyPlayer->GetPositionPathIndex(tilePosition);
		if (walkpathIdx != -1) {
			originalTargetBufferPosition = targetBufferPosition;
			tbl = GetPauseTRN();
		}
	}
#endif

	bool transparency = TileHasAny(tilePosition, TileProperties::Transparent) && TransList[dTransVal[tilePosition.x][tilePosition.y]];
#ifdef _DEBUG
	if ((SDL_GetModState() & SDL_KMOD_ALT) != 0) {
		transparency = false;
	}
#endif

	const auto getFirstTileMaskLeft = [=](TileType tile) -> MaskType {
		if (transparency) {
			switch (tile) {
			case TileType::LeftTrapezoid:
			case TileType::TransparentSquare:
				return TileHasAny(tilePosition, TileProperties::TransparentLeft)
				    ? MaskType::Left
				    : MaskType::Solid;
			case TileType::LeftTriangle:
				return MaskType::Solid;
			default:
				return MaskType::Transparent;
			}
		}
		return MaskType::Solid;
	};

	const auto getFirstTileMaskRight = [=](TileType tile) -> MaskType {
		if (transparency) {
			switch (tile) {
			case TileType::RightTrapezoid:
			case TileType::TransparentSquare:
				return TileHasAny(tilePosition, TileProperties::TransparentRight)
				    ? MaskType::Right
				    : MaskType::Solid;
			case TileType::RightTriangle:
				return MaskType::Solid;
			default:
				return MaskType::Transparent;
			}
		}
		return MaskType::Solid;
	};

	// Create a special lightmap buffer to bleed light up walls
	uint8_t lightmapBuffer[TILE_WIDTH * TILE_HEIGHT];
	const Lightmap bleedLightmap = Lightmap::bleedUp(*GetOptions().Graphics.perPixelLighting, lightmap, targetBufferPosition, lightmapBuffer);

	// If the first micro tile is a floor tile, it may be followed
	// by foliage which should be rendered now.
	const bool isFloor = IsFloor(tilePosition);
	if (const LevelCelBlock levelCelBlock { pMap->mt[0] }; levelCelBlock.hasValue()) {
		const TileType tileType = levelCelBlock.type();
		if (!isFloor || tileType == TileType::TransparentSquare) {
			if (isFloor && tileType == TileType::TransparentSquare) {
				RenderTileFoliage(out, bleedLightmap, targetBufferPosition,
				    pDungeonCels.get(), levelCelBlock, foliageTbl);
			} else {
				RenderTile(out, bleedLightmap, targetBufferPosition,
				    pDungeonCels.get(), levelCelBlock, getFirstTileMaskLeft(tileType), tbl);
			}
		}
	}
	if (const LevelCelBlock levelCelBlock { pMap->mt[1] }; levelCelBlock.hasValue()) {
		const TileType tileType = levelCelBlock.type();
		if (!isFloor || tileType == TileType::TransparentSquare) {
			if (isFloor && tileType == TileType::TransparentSquare) {
				RenderTileFoliage(out, bleedLightmap, targetBufferPosition + RightFrameDisplacement,
				    pDungeonCels.get(), levelCelBlock, foliageTbl);
			} else {
				RenderTile(out, bleedLightmap, targetBufferPosition + RightFrameDisplacement,
				    pDungeonCels.get(), levelCelBlock, getFirstTileMaskRight(tileType), tbl);
			}
		}
	}
	targetBufferPosition.y -= TILE_HEIGHT;

	for (uint_fast8_t i = 2, n = MicroTileLen; i < n; i += 2) {
		{
			const LevelCelBlock levelCelBlock { pMap->mt[i] };
			if (levelCelBlock.hasValue()) {
				RenderTile(out, bleedLightmap, targetBufferPosition,
				    pDungeonCels.get(), levelCelBlock,
				    transparency ? MaskType::Transparent : MaskType::Solid, foliageTbl);
			}
		}
		{
			const LevelCelBlock levelCelBlock { pMap->mt[i + 1] };
			if (levelCelBlock.hasValue()) {
				RenderTile(out, bleedLightmap, targetBufferPosition + RightFrameDisplacement,
				    pDungeonCels.get(), levelCelBlock,
				    transparency ? MaskType::Transparent : MaskType::Solid, foliageTbl);
			}
		}
		targetBufferPosition.y -= TILE_HEIGHT;
	}

#ifdef _DEBUG
	if (DebugPath && walkpathIdx != -1) {
		DrawString(out, StrCat(walkpathIdx),
		    Rectangle(originalTargetBufferPosition + Displacement { 0, -TILE_HEIGHT }, Size { TILE_WIDTH, TILE_HEIGHT }),
		    TextRenderOptions {
		        .flags = UiFlags::AlignCenter | UiFlags::VerticalCenter
		            | (IsTileSolid(tilePosition) ? UiFlags::ColorYellow : UiFlags::ColorWhite) });
	}
#endif
}

/**
 * @brief Render a floor tile.
 * @param out Target buffer
 * @param lightmap Per-pixel light buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Target buffer coordinate
 */
void DrawFloorTile(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition)
{
	const int lightTableIndex = dLight[tilePosition.x][tilePosition.y];

	const uint8_t *tbl = LightTables[lightTableIndex].data();
#ifdef _DEBUG
	if (DebugPath && MyPlayer->GetPositionPathIndex(tilePosition) != -1)
		tbl = GetPauseTRN();
#endif

	const uint16_t levelPieceId = dPiece[tilePosition.x][tilePosition.y];
	{
		const LevelCelBlock levelCelBlock { DPieceMicros[levelPieceId].mt[0] };
		if (levelCelBlock.hasValue()) {
			RenderTileFrame(out, lightmap, targetBufferPosition, TileType::LeftTriangle,
			    GetDunFrame(pDungeonCels.get(), levelCelBlock.frame()), DunFrameTriangleHeight, MaskType::Solid, tbl);
		}
	}
	{
		const LevelCelBlock levelCelBlock { DPieceMicros[levelPieceId].mt[1] };
		if (levelCelBlock.hasValue()) {
			RenderTileFrame(out, lightmap, targetBufferPosition + RightFrameDisplacement, TileType::RightTriangle,
			    GetDunFrame(pDungeonCels.get(), levelCelBlock.frame()), DunFrameTriangleHeight, MaskType::Solid, tbl);
		}
	}
}

/**
 * @brief Draw item for a given tile
 * @param out Output buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 * @param pre Is the sprite in the background
 */
void DrawItem(const Surface &out, int8_t itemIndex, Point targetBufferPosition, int lightTableIndex)
{
#ifdef __3DS__
	targetBufferPosition.x += CtrStereoSpriteOffset();
#endif
	const Item &item = Items[itemIndex];
	const ClxSprite sprite = item.AnimInfo.currentSprite();
	const Point position = targetBufferPosition + item.getRenderingOffset(sprite);
	if (!IsPlayerInStore() && (itemIndex == pcursitem || AutoMapShowItems)) {
		ClxDrawOutlineSkipColorZero(out, GetOutlineColor(item, false), position, sprite);
	}
	ClxDrawLight(out, position, sprite, lightTableIndex);
	if (item.AnimInfo.isLastFrame() || item._iCurs == ICURS_MAGIC_ROCK) {
		AddItemToLabelQueue(itemIndex, position);
	}
}

/**
 * @brief Check if and how a monster should be rendered
 * @param out Output buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Output buffer coordinates
 */
void DrawMonsterHelper(const Surface &out, Point tilePosition, Point targetBufferPosition, int lightTableIndex)
{
#ifdef __3DS__
	targetBufferPosition.x += CtrStereoSpriteOffset();
#endif
	int mi = dMonster[tilePosition.x][tilePosition.y];

	mi = std::abs(mi) - 1;

	if (leveltype == DTYPE_TOWN) {
		auto &towner = Towners[mi];
		const Point position = targetBufferPosition + towner.getRenderingOffset();
		const ClxSprite sprite = towner.currentSprite();
		if (mi == pcursmonst) {
			ClxDrawOutlineSkipColorZero(out, OutlineColorsTowner, position, sprite);
		}
		ClxDraw(out, position, sprite);
		return;
	}

	if (!IsTileLit(tilePosition) && !(MyPlayer->_pInfraFlag && IsFloor(tilePosition))) {
		return;
	}

	if (static_cast<size_t>(mi) >= MaxMonsters) {
		Log("Draw Monster: tried to draw illegal monster {}", mi);
		return;
	}

	const auto &monster = Monsters[mi];
	if ((monster.flags & MFLAG_HIDDEN) != 0) {
		return;
	}

	const ClxSprite sprite = monster.animInfo.currentSprite();
	const Displacement offset = monster.getRenderingOffset(sprite);

	const Point monsterRenderPosition = targetBufferPosition + offset;
	if (mi == pcursmonst) {
		ClxDrawOutlineSkipColorZero(out, OutlineColorsMonster, monsterRenderPosition, sprite);
	}
	DrawMonster(out, tilePosition, monsterRenderPosition, monster, lightTableIndex);
}

/**
 * @brief Render object sprites
 * @param out Target buffer
 * @param lightmap Per-pixel light buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Target buffer coordinates
 */
void DrawDungeon(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition)
{
	assert(InDungeonBounds(tilePosition));
	const int lightTableIndex = dLight[tilePosition.x][tilePosition.y];

	DrawCell(out, lightmap, tilePosition, targetBufferPosition, lightTableIndex);

	const int8_t bDead = dCorpse[tilePosition.x][tilePosition.y];
	const int8_t bMap = dTransVal[tilePosition.x][tilePosition.y];

#ifdef _DEBUG
	if (DebugVision && IsTileLit(tilePosition)) {
		ClxDraw(out, targetBufferPosition, (*pSquareCel)[0]);
	}
#endif

	if (MissilePreFlag) {
		DrawMissile(out, tilePosition, targetBufferPosition, true, lightTableIndex);
	}

	if (lightTableIndex < LightsMax && bDead != 0) {
		const Corpse &corpse = Corpses[(bDead & 0x1F) - 1];
		const Point position { targetBufferPosition.x - CalculateSpriteTileCenterX(corpse.width), targetBufferPosition.y };
		const ClxSprite sprite = corpse.spritesForDirection(static_cast<Direction>((bDead >> 5) & 7))[corpse.frame];
		if (corpse.translationPaletteIndex != 0) {
			const uint8_t *trn = Monsters[corpse.translationPaletteIndex - 1].uniqueMonsterTRN.get();
			ClxDrawTRN(out, position, sprite, trn);
		} else {
			ClxDrawLight(out, position, sprite, lightTableIndex);
		}
	}

	const int8_t bItem = dItem[tilePosition.x][tilePosition.y];
	const Object *object = lightTableIndex < LightsMax
	    ? FindObjectAtPosition(tilePosition)
	    : nullptr;
	if (object != nullptr && object->_oPreFlag) {
		DrawObject(out, *object, tilePosition, targetBufferPosition, lightTableIndex);
	}
	if (bItem > 0 && !Items[bItem - 1]._iPostDraw) {
		DrawItem(out, static_cast<int8_t>(bItem - 1), targetBufferPosition, lightTableIndex);
	}

	if (TileContainsDeadPlayer(tilePosition)) {
		DrawDeadPlayer(out, tilePosition, targetBufferPosition, lightTableIndex);
	}
	Player *player = PlayerAtPosition(tilePosition);
	if (player != nullptr) {
		const uint8_t pid = player->getId();
		assert(pid < MAX_PLRS);
		int playerId = static_cast<int>(pid) + 1;
		// If sprite is moving southwards or east, we want to draw it offset from the tile it's moving to, so we need negative ID
		// This respests the order that tiles are drawn. By using the negative id, we ensure that the sprite is drawn with priority
		if (player->_pmode == PM_WALK_SOUTHWARDS || (player->_pmode == PM_WALK_SIDEWAYS && player->_pdir == Direction::East))
			playerId = -playerId;
		if (dPlayer[tilePosition.x][tilePosition.y] == playerId) {
			auto tempTilePosition = tilePosition;
			auto tempTargetBufferPosition = targetBufferPosition;

			// Offset the sprite to the tile it's moving from
			if (player->_pmode == PM_WALK_SOUTHWARDS) {
				switch (player->_pdir) {
				case Direction::SouthWest:
					tempTargetBufferPosition += { TILE_WIDTH / 2, -TILE_HEIGHT / 2 };
					break;
				case Direction::South:
					tempTargetBufferPosition += { 0, -TILE_HEIGHT };
					break;
				case Direction::SouthEast:
					tempTargetBufferPosition += { -TILE_WIDTH / 2, -TILE_HEIGHT / 2 };
					break;
				default:
					DVL_UNREACHABLE();
				}
				tempTilePosition += Opposite(player->_pdir);
			} else if (player->_pmode == PM_WALK_SIDEWAYS && player->_pdir == Direction::East) {
				tempTargetBufferPosition += { -TILE_WIDTH, 0 };
				tempTilePosition += Opposite(player->_pdir);
			}
			DrawPlayer(out, *player, tempTilePosition, tempTargetBufferPosition, lightTableIndex);
		}
	}

	Monster *monster = FindMonsterAtPosition(tilePosition);
	if (monster != nullptr) {
		auto mid = monster->getId();
		assert(mid < MaxMonsters);
		int monsterId = static_cast<int>(mid) + 1;
		// If sprite is moving southwards or east, we want to draw it offset from the tile it's moving to, so we need negative ID
		// This respests the order that tiles are drawn. By using the negative id, we ensure that the sprite is drawn with priority
		if (monster->mode == MonsterMode::MoveSouthwards || (monster->mode == MonsterMode::MoveSideways && monster->direction == Direction::East))
			monsterId = -monsterId;
		if (dMonster[tilePosition.x][tilePosition.y] == monsterId) {
			auto tempTilePosition = tilePosition;
			auto tempTargetBufferPosition = targetBufferPosition;

			// Offset the sprite to the tile it's moving from
			if (monster->mode == MonsterMode::MoveSouthwards) {
				switch (monster->direction) {
				case Direction::SouthWest:
					tempTargetBufferPosition += { TILE_WIDTH / 2, -TILE_HEIGHT / 2 };
					break;
				case Direction::South:
					tempTargetBufferPosition += { 0, -TILE_HEIGHT };
					break;
				case Direction::SouthEast:
					tempTargetBufferPosition += { -TILE_WIDTH / 2, -TILE_HEIGHT / 2 };
					break;
				default:
					DVL_UNREACHABLE();
				}
				tempTilePosition += Opposite(monster->direction);
			} else if (monster->mode == MonsterMode::MoveSideways && monster->direction == Direction::East) {
				tempTargetBufferPosition += { -TILE_WIDTH, 0 };
				tempTilePosition += Opposite(monster->direction);
			}
			DrawMonsterHelper(out, tempTilePosition, tempTargetBufferPosition, lightTableIndex);
		}
	}

	DrawMissile(out, tilePosition, targetBufferPosition, false, lightTableIndex);

	if (object != nullptr && !object->_oPreFlag) {
		DrawObject(out, *object, tilePosition, targetBufferPosition, lightTableIndex);
	}
	if (bItem > 0 && Items[bItem - 1]._iPostDraw) {
		DrawItem(out, static_cast<int8_t>(bItem - 1), targetBufferPosition, lightTableIndex);
	}

	if (leveltype != DTYPE_TOWN) {
		const bool perPixelLighting = *GetOptions().Graphics.perPixelLighting;
		const int8_t bArch = dSpecial[tilePosition.x][tilePosition.y] - 1;
		if (bArch >= 0) {
			bool transparency = TransList[bMap];
#ifdef _DEBUG
			// Turn transparency off here for debugging
			transparency = transparency && (SDL_GetModState() & SDL_KMOD_ALT) == 0;
#endif
			if (perPixelLighting) {
				// Create a special lightmap buffer to bleed light up walls
				uint8_t lightmapBuffer[TILE_WIDTH * TILE_HEIGHT];
				const Lightmap bleedLightmap = Lightmap::bleedUp(*GetOptions().Graphics.perPixelLighting, lightmap, targetBufferPosition, lightmapBuffer);

				if (transparency)
					ClxDrawBlendedWithLightmap(out, targetBufferPosition, (*pSpecialCels)[bArch], bleedLightmap);
				else
					ClxDrawWithLightmap(out, targetBufferPosition, (*pSpecialCels)[bArch], bleedLightmap);
			} else if (transparency) {
				ClxDrawLightBlended(out, targetBufferPosition, (*pSpecialCels)[bArch], lightTableIndex);
			} else {
				ClxDrawLight(out, targetBufferPosition, (*pSpecialCels)[bArch], lightTableIndex);
			}
		}
	} else {
		// Tree leaves should always cover player when entering or leaving the tile,
		// So delay the rendering until after the next row is being drawn.
		// This could probably have been better solved by sprites in screen space.
		if (tilePosition.x > 0 && tilePosition.y > 0 && targetBufferPosition.y > TILE_HEIGHT) {
			const int8_t bArch = dSpecial[tilePosition.x - 1][tilePosition.y - 1] - 1;
			if (bArch >= 0)
				ClxDraw(out, targetBufferPosition + Displacement { 0, -TILE_HEIGHT }, (*pSpecialCels)[bArch]);
		}
	}
}

/**
 * @brief Render a row of tiles
 * @param out Buffer to render to
 * @param lightmap Per-pixel light buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Target buffer coordinates
 * @param rows Number of rows
 * @param columns Tile in a row
 */
void DrawFloor(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition, int rows, int columns)
{
	for (int i = 0; i < rows; i++) {
		for (int j = 0; j < columns; j++, tilePosition += Direction::East, targetBufferPosition.x += TILE_WIDTH) {
			if (!InDungeonBounds(tilePosition))
				continue;
			if (IsFloor(tilePosition)) {
				DrawFloorTile(out, lightmap, tilePosition, targetBufferPosition);
			}
		}
		// Return to start of row
		tilePosition += Displacement(Direction::West) * columns;
		targetBufferPosition.x -= columns * TILE_WIDTH;

		// Jump to next row
		targetBufferPosition.y += TILE_HEIGHT / 2;
		if ((i & 1) != 0) {
			tilePosition.x++;
			columns--;
			targetBufferPosition.x += TILE_WIDTH / 2;
		} else {
			tilePosition.y++;
			columns++;
			targetBufferPosition.x -= TILE_WIDTH / 2;
		}
	}
}

/**
 * @brief Renders the floor tiles
 * @param out Output buffer
 * @param lightmap Per-pixel light buffer
 * @param tilePosition dPiece coordinates
 * @param targetBufferPosition Buffer coordinates
 * @param rows Number of rows
 * @param columns Tile in a row
 */
void DrawTileContent(const Surface &out, const Lightmap &lightmap, Point tilePosition, Point targetBufferPosition, int rows, int columns)
{
	// Keep evaluating until MicroTiles can't affect screen
	rows += MicroTileLen;

#ifdef _DEBUG
	DebugCoordsMap.reserve(rows * columns);
#endif

	for (int i = 0; i < rows; i++) {
		bool skip = false;
		for (int j = 0; j < columns; j++) {
			if (InDungeonBounds(tilePosition)) {
				bool skipNext = false;
#ifdef _DEBUG
				DebugCoordsMap[tilePosition.x + (tilePosition.y * MAXDUNX)] = targetBufferPosition;
#endif
				if (tilePosition.x + 1 < MAXDUNX && tilePosition.y - 1 >= 0 && targetBufferPosition.x + TILE_WIDTH <= GetViewportWidth()) {
					// Render objects behind walls first to prevent sprites, that are moving
					// between tiles, from poking through the walls as they exceed the tile bounds.
					// A proper fix for this would probably be to layout the scene and render by
					// sprite screen position rather than tile position.
					if (IsWall(tilePosition) && (IsWall(tilePosition + Displacement { 1, 0 }) || (tilePosition.x > 0 && IsWall(tilePosition + Displacement { -1, 0 })))) { // Part of a wall aligned on the x-axis
						if (IsTiÛı¶‰ËkºwµçR’°Ğ —7v—F6‚„×•Æ–W"Óå÷F—"’°Ğ –66RF—&V7F–öã£¤æôF—&V7F–öã Ğ –'&V³°Ğ –66RF—&V7F–öã£¤æ÷'Fƒ Ğ –66RF—&V7F–öã£¥6÷WFƒ Ğ —&÷w2³Ò#°Ğ –'&V³°Ğ –66RF—&V7F–öã£¤æ÷'F„V7C Ğ –6öÇVÖç2²³°Ğ —&÷w2³Ò#°Ğ –'&V³°Ğ –66RF—&V7F–öã£¤V7C Ğ –66RF—&V7F–öã£¥vW7C Ğ –6öÇVÖç2²³°Ğ –'&V³°Ğ –66RF—&V7F–öã£¥6÷WF„V7C Ğ –66RF—&V7F–öã£¥6÷WF…vW7C Ğ –66RF—&V7F–öã£¤æ÷'F…vW7C Ğ –6öÇVÖç2²³°Ğ —&÷w2²³°Ğ –'&V³°Ğ —ĞĞ —ĞĞ Ğ¢6–fFVbõó4E5õğĞ –6öÇVÖç2Ò7G%v÷&ÆEF–ÆT6öÇVÖç2†6öÇVÖç2ÂÆWfVÇG—RÓÒEE•UõDõtâ“°Ğ¢6VæF–`Ğ Ğ¢6–fFVbETåõ$TäDU%õ5DE0Ğ ”GVå&VæFW%7FG2æ6ÆV"‚“°Ğ¢6VæF–`Ğ Ğ ”Æ–v‡FÖÆ–v‡FÖÒÆ–v‡FÖ£¦'V–ÆB‚¤vWD÷F–öç2‚’äw&†–72çW%—†VÄÆ–v‡F–ærÂ÷6—F–öâÂö–çB·Ò²öfg6WBÀĞ ’vWEf–Ww÷'Ev–GF‚‚’ÂvWEf–Ww÷'D†V–v‡B‚’Â&÷w2Â6öÇVÖç2ÀĞ ’÷WBæBƒÂ’Â÷WBç—F6‚‚’ÂÆ–v‡EF&ÆW2ÂgVÆÇ”Æ—DÆ–v‡EF&ÆRÂgVÆÇ”F&´Æ–v‡EF&ÆRÀĞ ’DÆ–v‡BÂÖ–7&õF–ÆTÆVâ“°Ğ Ğ ”G&tfÆö÷"†÷WBÂÆ–v‡FÖÂ÷6—F–öâÂö–çB·Ò²öfg6WBÂ&÷w2Â6öÇVÖç2“°Ğ ”G&uF–ÆT6öçFVçB†÷WBÂÆ–v‡FÖÂ÷6—F–öâÂö–çB·Ò²öfg6WBÂ&÷w2Â6öÇVÖç2“°Ğ ”G&tôô"†÷WBÂÆ–v‡FÖÂ÷6—F–öâÂö–çB·Ò²öfg6WBÂ&÷w2Â6öÇVÖç2“°Ğ Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒ’°Ğ •¦ööÒ†gVÆÄ÷WBç7V'&Vv–öâƒÂÂvWEf–Ww÷'Ev–GF‚‚’ÂvWEf–Ww÷'D†V–v‡B‚’’“°Ğ —ĞĞ Ğ¢6–fFVbETåõ$TäDU%õ5DE0Ğ —7FC£§fV7F÷#Ç7FC£§—#ÄGVå&VæFW%G—RÂ6—¦U÷Cãâ6÷'FVE7FG2„GVå&VæFW%7FG2æ&Vv–â‚’ÂGVå&VæFW%7FG2æVæB‚’“°Ğ –5÷6÷'B‡6÷'FVE7FG2ÂµÒ†6öç7B7FC£§—#ÄGVå&VæFW%G—RÂ6—¦U÷CâfÂ6öç7B7FC£§—#ÄGVå&VæFW%G—RÂ6—¦U÷Câf"’°Ğ —&WGW&âæf—'7BæÖ6µG—RÓÒ"æf—'7BæÖ6µG—PĞ ’ò7FF–5ö67CÇV–çC…÷Câ†æf—'7BçF–ÆUG—R’Â7FF–5ö67CÇV–çC…÷Câ†"æf—'7BçF–ÆUG—RĞ ’¢7FF–5ö67CÇV–çC…÷Câ†æf—'7BæÖ6µG—R’Â7FF–5ö67CÇV–çC…÷Câ†"æf—'7BæÖ6µG—R“°Ğ —Ò“°Ğ •ö–çB÷2²Â#Ó°Ğ –f÷"‡6—¦U÷B’Ò²’Â6÷'FVE7FG2ç6—¦R‚“²²¶’’°Ğ –6öç7BWFòg7FBÒ6÷'FVE7FG5¶•Ó°Ğ ”G&u7G&–ær†÷WBÂ7G$6B†’Â"â"’Â&V7FævÆR‡÷2Â6—¦R²#ÂbÒ’Â²æfÆw2ÒV”fÆw3£¤Æ–vå&–v‡BÒ“°Ğ ”G&u7G&–ær†÷WBÂÖ6µG—UFõ7G&–ær‡7FBæf—'7BæÖ6µG—R’Â²÷2ç‚²#BÂ÷2ç’Ò“°Ğ ”G&u7G&–ær†÷WBÂF–ÆUG—UFõ7G&–ær‡7FBæf—'7BçF–ÆUG—R’Â²÷2ç‚²ƒBÂ÷2ç’Ò“°Ğ ”G&u7G&–ær†÷WBÂf÷&ÖD–çFVvW"‡7FBç6V6öæB’Â&V7FævÆR‡²÷2ç‚²3SBÂ÷2ç’ÒÂ6—¦RƒCÂb’’Â²æfÆw2ÒV”fÆw3£¤Æ–vå&–v‡BÒ“°Ğ —÷2ç’³Òc°Ğ —ĞĞ¢6VæF–`Ğ§ĞĞ Ğ¢ò¢ Ğ¢¢'&–Vb7F'B&VæFW&–æröb67&VVâÂF÷vâf&–F–öàĞ¢¢&Ò÷WB'VffW"Fò&VæFW"FğĞ¢¢&Ò7F'E÷6—F–öâ6VçFW"öbf–Wr–âE–V6R6ö÷&F–æFW0Ğ¢¢ğĞ§fö–BG&uf–Wr†6öç7B7W&f6Rf÷WBÂö–çB7F'E÷6—F–öâĞ§°Ğ¢6–fFVbôDT%TpĞ ”FV'Vt6ö÷&G4Öæ6ÆV"‚“°Ğ¢6VæF–`Ğ¢6–fFVbõó4E5õğĞ ’òò&VæFW"F†RæF—fRCƒ#Cv÷&ÆBf–Ws²T’—†VÇ27F’–æFWVæFVçBâF†RF÷ö`Ğ ’òò÷WF—26ÆV&VBFòF†R¶W’–æFW‚Â6òF†R&W6VçFW"6†÷w2F†Rv÷&ÆB–àĞ ’òòWfW'’—†VÂF†BF†RT’&VÆ÷rFöW2æ÷BG&ràĞ ”7G%v÷&ÆD&Vv–äg&ÖR†÷WB“°Ğ –6öç7B7W&f6Rgv÷&ÆD÷WBÒ7G%v÷&ÆE7W&f6R‚“°Ğ¢6VÇ6PĞ –6öç7B7W&f6Rgv÷&ÆD÷WBÒ÷WC°Ğ¢6VæF–`Ğ ”F—7Æ6VÖVçBöfg6WBÒ·Ó°Ğ ”6Æ4f—'7EF–ÆU÷6—F–öâ‡7F'E÷6—F–öâÂöfg6WB“°Ğ ”G&tvÖR‡v÷&ÆD÷WBÂ7F'E÷6—F–öâÂöfg6WB“°Ğ ––b„WFöÖ7F—fPĞ¢6–fFVbõó4E5õğĞ ’bbvÖVçUö—5ö7F—fR‚Ğ¢6VæF–`Ğ ’’°Ğ ”G&tWFöÖ‡v÷&ÆD÷WBç7V'&Vv–öâƒÂÂvWEf–Ww÷'Ev–GF‚‚’ÂvWEf–Ww÷'D†V–v‡B‚’’“°Ğ —ĞĞ¢6–fFVbôDT%TpĞ –&ööÂFV'Vtw&–EFW‡DæVVFVBÒ—4FV'Vtw&–EFW‡DæVVFVB‚“°Ğ ––b†FV'Vtw&–EFW‡DæVVFVBÇÂFV'Vtw&–B’°Ğ ’òòf÷&6R&VG&v–ær÷"FV'Vr7GVfb7F—2öâæVÂöâcCƒCƒ&W6öÇWF–öàĞ •&VG&tWfW'—F†–ær‚“°Ğ —7FC£§7G&–ærFV'Vtw&–EFW‡C°Ğ –&ööÂÖVvF–ÆW2Ò—4FV'Vtw&–D–äÖVvF–ÆW2‚“°Ğ Ğ –f÷"†WFò¶GVä6ö÷&EfÂÂ—†VÄ6ö÷&G5Ò¢FV'Vt6ö÷&G4Ö’°Ğ •ö–çBGVä6ö÷&G2Ò²GVä6ö÷&EfÂRÔ„ETå‚ÂGVä6ö÷&EfÂòÔ„ETå‚Ó°Ğ ––b†ÖVvF–ÆW2bb†GVä6ö÷&G2ç‚R"ÓÒÇÂGVä6ö÷&G2ç’R"ÓÒ’Ğ –6öçF–çVS°Ğ ––b†ÖVvF–ÆW2Ğ ——†VÄ6ö÷&G2³ÒF—7Æ6VÖVçB²ÂD”ÄUô„T”t…Bò"Ó°Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒĞ ——†VÄ6ö÷&G2£Ò#°Ğ ––b†FV'Vtw&–EFW‡DæVVFVBbbvWDFV'Vtw&–EFW‡B†GVä6ö÷&G2ÂFV'Vtw&–EFW‡B’’°Ğ •6—¦RF–ÆU6—¦RÒ²D”ÄUõt”ED‚ÂD”ÄUô„T”t…BÓ°Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒĞ —F–ÆU6—¦R£Ò#°Ğ ”G&u7G&–ær‡v÷&ÆD÷WBÂFV'Vtw&–EFW‡BÂ²—†VÄ6ö÷&G2ÒF—7Æ6VÖVçB²ÂF–ÆU6—¦Ræ†V–v‡BÒÂF–ÆU6—¦RÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷%&VBÂV”fÆw3£¤Æ–vä6VçFW"ÂV”fÆw3£¥fW'F–6Ä6VçFW"Ò“°Ğ —ĞĞ ––b„FV'Vtw&–B’°Ğ ––çB†ÆeF–ÆUv–GF‚ÒD”ÄUõt”ED‚ò#°Ğ ––çB†ÆeF–ÆT†V–v‡BÒD”ÄUô„T”t…Bò#°Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒ’°Ğ –†ÆeF–ÆUv–GF‚£Ò#°Ğ –†ÆeF–ÆT†V–v‡B£Ò#°Ğ —ĞĞ –6öç7Bö–çB6VçFW"²—†VÄ6ö÷&G2ç‚²†ÆeF–ÆUv–GF‚Â—†VÄ6ö÷&G2ç’Ò†ÆeF–ÆT†V–v‡BÓ°Ğ Ğ ––b†ÖVvF–ÆW2’°Ğ –†ÆeF–ÆUv–GF‚£Ò#°Ğ –†ÆeF–ÆT†V–v‡B£Ò#°Ğ —ĞĞ Ğ –6öç7BV–çC…÷B6öÂÒÃeô$T”tS°Ğ –f÷"†6öç7BWFòe¶÷&–v–å‚ÂG…Ò¢²7FC£§—"†6VçFW"ç‚Ò†ÆeF–ÆUv–GF‚Â’Â7FC£§—"†6VçFW"ç‚²†ÆeF–ÆUv–GF‚ÂÓ’Ò’°Ğ ’òòvRöæÇ’æVVBFòG&r†ÆböbF†Rw&–B6VÆÂ&÷VæF&–W2†öæRG&–ævÆR’àĞ ’òòF†R÷F†W"G&–ævÆRv–ÆÂ&RG&vâv†VâG&v–ærF†RF¦6VçBw&–B6VÆÇ2àĞ –6öç7B–çBG’Ò°Ğ •ö–çBg&öÒ²÷&–v–å‚Â6VçFW"ç’Ó°Ğ ––çB†V–v‡BÒ†ÆeF–ÆT†V–v‡C°Ğ ––b‡v÷&ÆD÷WBä–ä&÷VæG2†g&öÒ’bbv÷&ÆD÷WBä–ä&÷VæG2†g&öÒ²F—7Æ6VÖVçB²"¢G‚¢†V–v‡BÂG’¢†V–v‡BÒ’’°Ğ —V–çC…÷B¦G7BÒv÷&ÆD÷WBæB†g&öÒç‚Âg&öÒç’“°Ğ –6öç7B–çB—F6‚Òv÷&ÆD÷WBç—F6‚‚“°Ğ —v†–ÆR††V–v‡BÒÒâ’°Ğ ’¦G7BÒ6öÃ°Ğ –G7B³ÒGƒ°Ğ ’¦G7BÒ6öÃ°Ğ –G7B³ÒGƒ°Ğ –G7B³Ò7FF–5ö67CÇG&F–fe÷Câ†G’¢—F6‚“°Ğ —ĞĞ —ÒVÇ6R°Ğ —v†–ÆR††V–v‡BÒÒâ’°Ğ —v÷&ÆD÷WBå6WE—†VÂ†g&öÒÂ6öÂ“°Ğ –g&öÒç‚³ÒGƒ°Ğ —v÷&ÆD÷WBå6WE—†VÂ†g&öÒÂ6öÂ“°Ğ –g&öÒç‚³ÒGƒ°Ğ –g&öÒç’³ÒG“°Ğ —ĞĞ —ĞĞ —ĞĞ —ĞĞ —ĞĞ —ĞĞ¢6VæF–`Ğ ’òòÆ&VÇ2æB…TB&VÆöærFòF†RfÆBT’Â6†&VB'’&÷F‚W–W2àĞ ”G&t—FVÔæÖTÆ&VÇ2†÷WB“°Ğ ”G&tÖöç7FW$†VÇF„&"†÷WB“°Ğ ”G&tfÆöF–ætçVÖ&W'2†÷WBÂ7F'E÷6—F–öâÂöfg6WB“°Ğ Ğ ––b„—5Æ–W$–å7F÷&R‚’bbFW‡FfÆrĞ ”G&u5FW‡B†÷WB“°Ğ¢6–fFVbõó4E5õğĞ ––b†–çffÆr’°Ğ ––b„—5f—7VÅ7F÷&T÷Vâ’°Ğ ”f–ÆÅ&V7B‚¥6–FUæVÄ'VffW"ÂÂÂ6–FUæVÅ6—¦Rçv–GF‚Â6–FUæVÅ6—¦Ræ†V–v‡BÂ“°Ğ ”G&t–çb‚¥6–FUæVÄ'VffW"“°Ğ ––b„G&÷vöÆDfÆrĞ ”G&tvöÆE7Æ—B‚¥6–FUæVÄ'VffW"“°Ğ –6öç7B&V7FævÆR&V7BÒ7G$–çfVçF÷'•67&VVå&V7B‚“°Ğ –÷WBå66ÆT&Æ—Dg&öÒ‚¥6–FUæVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂ3#Â3S"’ÂÖ¶U6FÅ&V7B‡&V7Bç÷6—F–öâç‚ÂÂ&V7Bç6—¦Rçv–GF‚Â#C’“°Ğ —ÒVÇ6R°Ğ ”7G%æVÄ&6¶w&÷VæB&6¶w&÷VæBÒ—57F6„÷Vâò7G%æVÄ&6¶w&÷VæC£¤–çfVçF÷'•v'&–÷%7F6‚¢7G%æVÄ&6¶w&÷VæC£¤–çfVçF÷'•v'&–÷#°Ğ —7v—F6‚„–ç7V7EÆ–W"Óå÷6Æ72’°Ğ –66R†W&ô6Æ73£¥&öwVS Ğ –66R†W&ô6Æ73£¤&&C Ğ –&6¶w&÷VæBÒ—57F6„÷Vâò7G%æVÄ&6¶w&÷VæC£¤–çfVçF÷'•&öwVU7F6‚¢7G%æVÄ&6¶w&÷VæC£¤–çfVçF÷'•&öwVS°Ğ –'&V³°Ğ –66R†W&ô6Æ73£¥6÷&6W&W# Ğ –66R†W&ô6Æ73£¤Ööæ³ Ğ –&6¶w&÷VæBÒ—57F6„÷Vâò7G%æVÄ&6¶w&÷VæC£¤–çfVçF÷'•6÷&6W&W%7F6‚¢7G%æVÄ&6¶w&÷VæC£¤–çfVçF÷'•6÷&6W&W#°Ğ –'&V³°Ğ –FVfVÇC Ğ –'&V³°Ğ —ĞĞ ”G&t7G%æVÄ&6¶w&÷VæB‚¥F÷æVÄ'VffW"Â&6¶w&÷VæB“°Ğ ”G&t7G$–çfVçF÷'”—FV×2‚¥F÷æVÄ'VffW"Â—57F6„÷Vâ“°Ğ ––b„—57F6„÷VâĞ ”G&t7G%7F6„—FV×2‚¥F÷æVÄ'VffW"“°Ğ ––b‚—57F6„÷Vâ’°Ğ ”G&u7G&–ær‚¥F÷æVÄ'VffW"Âò‚"„Â’6†&7FW""’Â²²bÂ#"ÒÂ²sÂ#BÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷%v†—FVvöÆBÂV”fÆw3£¤¶W&æ–ætf—E76–ærÒ“°Ğ ”G&u7G&–ær‚¥F÷æVÄ'VffW"Âò‚"…"’7VÆÂ&öö²"’Â²²#‚Â#"ÒÂ²sÂ#BÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷%v†—FVvöÆBÂV”fÆw3£¤Æ–vå&–v‡BÂV”fÆw3£¤¶W&æ–ætf—E76–ærÒ“°Ğ ”G&u7G&–ær‚¥F÷æVÄ'VffW"Âò‚"…’’G&÷"’Â²²bÂ#rÒÂ²SÂ‚ÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷%v†—FVvöÆBÂV”fÆw3£¤¶W&æ–ætf—E76–ærÒ“°Ğ —ĞĞ –÷WBå66ÆT&Æ—Dg&öÕ&W6W'f–ætF÷vç66ÆR‚¥F÷æVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂCÂ#C’ÂÖ¶U6FÅ&V7BƒÂÂvå67&VVåv–GF‚Â#C’“°Ğ ’òòF†R÷&–v–æÂvöÆBVçG'’v–FvWG27F–ÆÂW6RF†RÆVv7’æVÂ6çf2àĞ ––b„G&÷vöÆDfÆrÇÂ—5v—F†G&tvöÆD÷Vâ’°Ğ ”f–ÆÅ&V7B‚¥6–FUæVÄ'VffW"ÂÂÂ6–FUæVÅ6—¦Rçv–GF‚Â6–FUæVÅ6—¦Ræ†V–v‡BÂ“°Ğ ––b„—5v—F†G&tvöÆD÷Vâ’°Ğ ”G&u7F6‚‚¥6–FUæVÄ'VffW"“°Ğ ”G&tvöÆEv—F†G&r‚¥6–FUæVÄ'VffW"“°Ğ –÷WBå66ÆT&Æ—Dg&öÒ‚¥6–FUæVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂ3#Â3S"’ÂÖ¶U6FÅ&V7BƒÂÂ#‚Â#C’“°Ğ —ÒVÇ6R°Ğ ”G&t–çb‚¥6–FUæVÄ'VffW"“°Ğ ”G&tvöÆE7Æ—B‚¥6–FUæVÄ'VffW"“°Ğ –6öç7B&V7FævÆR&V7BÒ7G$–çfVçF÷'•67&VVå&V7B‚“°Ğ –÷WBå66ÆT&Æ—Dg&öÒ‚¥6–FUæVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂ3#Â3S"’ÂÖ¶U6FÅ&V7B‡&V7Bç÷6—F–öâç‚ÂÂ&V7Bç6—¦Rçv–GF‚Â#C’“°Ğ —ĞĞ —ĞĞ —ĞĞ —ÒVÇ6R–b…7VÆÆ&öö´fÆr’°Ğ ”G&u7VÆÄ&öö²‚¥F÷æVÄ'VffW"“°Ğ –÷WBå66ÆT&Æ—Dg&öÕ&W6W'f–ætF÷vç66ÆR‚¥F÷æVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂCÂ#C’ÂÖ¶U6FÅ&V7BƒÂÂvå67&VVåv–GF‚Â#C’“°Ğ —ĞĞ¢6VÇ6PĞ ––b†–çffÆr’°Ğ ”G&t–çb†÷WB“°Ğ —ÒVÇ6R–b…7VÆÆ&öö´fÆr’°Ğ ”G&u7VÆÄ&öö²†÷WB“°Ğ —ĞĞ¢6VæF–`Ğ¢6–fFVbõó4E5õğĞ ––b‚–çffÆrbb7VÆÆ&öö´fÆrbb6†$fÆrbbVW7DÆöt—4÷Vâ’°Ğ ”G&tGW$–6öâ†÷WB“°Ğ ”G&tÆWfVÄ'WGFöâ†÷WB“°Ğ —ĞĞ¢6VÇ6PĞ ”G&tGW$–6öâ†÷WB“°Ğ ”G&tÆWfVÄ'WGFöâ†÷WB“°Ğ¢6VæF–`Ğ¢6–fFVbõó4E5õğĞ ––b„6†$fÆrÇÂVW7DÆöt—4÷Vâ’°Ğ ––b„6†$fÆrĞ ”G&t6‡"‚¥F÷æVÄ'VffW"“°Ğ –VÇ6PĞ ”G&uVW7DÆör‚¥F÷æVÄ'VffW"“°Ğ –÷WBå66ÆT&Æ—Dg&öÕ&W6W'f–ætF÷vç66ÆR‚¥F÷æVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂCÂ#C’ÂÖ¶U6FÅ&V7BƒÂÂvå67&VVåv–GF‚Â#C’“°Ğ —ÒVÇ6R–b„—5f—7VÅ7F÷&T÷VâÇÂ„—57F6„÷Vâbb–çffÆr’’°Ğ ”f–ÆÅ&V7B‚¥6–FUæVÄ'VffW"ÂÂÂ6–FUæVÅ6—¦Rçv–GF‚Â6–FUæVÅ6—¦Ræ†V–v‡BÂ“°Ğ ––b„—57F6„÷Vâ’°Ğ ”G&u7F6‚‚¥6–FUæVÄ'VffW"“°Ğ ”G&tvöÆEv—F†G&r‚¥6–FUæVÄ'VffW"“°Ğ —ÒVÇ6R°Ğ ”G&uf—7VÅ7F÷&R‚¥6–FUæVÄ'VffW"“°Ğ —ĞĞ –÷WBå66ÆT&Æ—Dg&öÒ‚¥6–FUæVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂ3#Â3S"’ÂÖ¶U6FÅ&V7BƒÂÂ#‚Â#C’“°Ğ —ĞĞ¢6VÇ6PĞ ––b„6†$fÆr’°Ğ ”G&t6‡"†÷WB“°Ğ —ÒVÇ6R–b…VW7DÆöt—4÷Vâ’°Ğ ”G&uVW7DÆör†÷WB“°Ğ —ÒVÇ6R–b„—57F6„÷Vâ’°Ğ ”G&u7F6‚†÷WB“°Ğ —ÒVÇ6R–b„—5f—7VÅ7F÷&T÷Vâ’°Ğ ”G&uf—7VÅ7F÷&R†÷WB“°Ğ —ĞĞ¢6VæF–`Ğ Ğ¢6–fFVbõó4E5õğĞ ––b„—34E5f—7VÅ7F÷&U6ÆUVæF–ær‚’’°Ğ ”G&s4E5f—7VÅ7F÷&U6ÆR‚¥F÷æVÄ'VffW"“°Ğ –÷WBå66ÆT&Æ—Dg&öÕ&W6W'f–ætF÷vç66ÆR‚¥F÷æVÄ'VffW"ÂÖ¶U6FÅ&V7BƒÂÂCÂ#C’ÂÖ¶U6FÅ&V7BƒÂÂvå67&VVåv–GF‚Â#C’“°Ğ —ĞĞ¢6VæF–`Ğ ––b…6†÷uVæ—VT—FVÔ–æfô&÷‚’°Ğ ”G&uVæ—VT–æfò†÷WB“°Ğ —ĞĞ ––b‡FW‡FfÆr’°Ğ ”G&uFW‡B†÷WB“°Ğ —ĞĞ ––b…7VÆÅ6VÆV7DfÆr’°Ğ ”G&u7VÆÄÆ—7B†÷WB“°Ğ —ĞĞ¢6–fæFVbõó4E5õğĞ ––b„G&÷vöÆDfÆr’°Ğ ”G&tvöÆE7Æ—B†÷WB“°Ğ —ĞĞ ”G&tvöÆEv—F†G&r†÷WB“°Ğ¢6VæF–`Ğ ––b„†VÇfÆr’°Ğ ”G&t†VÇ†÷WB“°Ğ —ĞĞ ––b„6†DÆötfÆr’°Ğ ”G&t6†DÆör†÷WB“°Ğ —ĞĞ ––b„×•Æ–W$—4FVB’°Ğ •&VD&6²†÷WB“°Ğ ”G&tFVF…FW‡B†÷WB“°Ğ —ÒVÇ6R–b…W6TÖöFRÒ’°Ğ –vÖVçUöG&u÷W6R†÷WB“°Ğ —ĞĞ ––b„—4F–&Æô×6tf–Æ&ÆR‚’’°Ğ ”G&tF–&Æô×6r†÷WBç7V'&Vv–öå’ƒÂ÷WBæ‚‚’ÒvWDÖ–åæVÂ‚’ç6—¦Ræ†V–v‡B’“°Ğ —ĞĞ Ğ ”G&t6öçG&öÆÆW$ÖöF–f–W$†–çG2†÷WB“°Ğ ”G&uÇ$×6r†÷WB“°Ğ –vÖVçUöG&r†÷WB“°Ğ –FööÕöG&r†÷WB“°Ğ¢6–fFVbõó4E5õğĞ •WFFTÆ–fTÖæW&6VçB‚“°Ğ¢6VÇ6PĞ ”G&t–æfô&÷‚†÷WB“°Ğ •WFFTÆ–fTÖæW&6VçB‚“²òòWFFRÆ–fRöÖæF÷FÇ2&Vf÷&R&VæFW&–ærç’÷'F–öâöbF†RfÆ6²àĞ ”G&tÆ–fTfÆ6µWW"†÷WB“°Ğ ”G&tÖæfÆ6µWW"†÷WB“°Ğ¢6VæF–`Ğ§ĞĞ Ğ¢ò¢ Ğ¢¢'&–VbF—7Æ’F†R7W'&VçBfW&vRe2÷fW"6V0Ğ¢¢ğĞ§fö–BG&te2†6öç7B7W&f6Rf÷WBĞ§°Ğ —7FF–2–çBg&ÖW56–æ6TÆ7EWFFRÒ°Ğ —7FF–27FC£§7G&–æu÷f–Wrf÷&ÖGFVB·Ó°Ğ Ğ ––b‚g&ÖVfÆrÇÂv$7F—fR’°Ğ —&WGW&ã°Ğ —ĞĞ Ğ –g&ÖW56–æ6TÆ7EWFFR²³°Ğ –6öç7BV–çC3%÷B'VçF–ÖT–ä×2Ò4DÅôvWEF–6·2‚“°Ğ –6öç7BV–çC3%÷B×56–æ6TÆ7EWFFRÒ'VçF–ÖT–ä×2ÒÆ7Dg5WFFT–ä×3°Ğ ––b†×56–æ6TÆ7EWFFRãÒ’°Ğ –Æ7Dg5WFFT–ä×2Ò'VçF–ÖT–ä×3°Ğ –6öç7FW‡"–çBg5÷sÒ°Ğ –6öç7BV–çC3%÷Bg2Ò¢g5÷s¢g&ÖW56–æ6TÆ7EWFFRò×56–æ6TÆ7EWFFS°Ğ –g&ÖW56–æ6TÆ7EWFFRÒ°Ğ Ğ —7FF–26†"'Ve³UÒ·Ó°Ğ –6öç7B6†"¦VæBÒg2ãÒ¢g5÷s Ğ ’ò'Vd6÷’†'VbÂg2òg5÷sÂ"e2"Ğ ’¢'Vd6÷’†'VbÂg2òg5÷sÂ"â"Âg2Rg5÷sÂ"e2"“°Ğ –f÷&ÖGFVBÒ²'VbÂ7FF–5ö67CÇ7FC£§7G&–æu÷f–Ws£§6—¦U÷G—Sâ†VæBÒ'Vb’Ó°Ğ —Ó°Ğ ”G&u7G&–ær†÷WBÂf÷&ÖGFVBÂö–çB²‚Â‚ÒÂ²æfÆw2ÒV”fÆw3£¤6öÆ÷%&VBÒ“°Ğ§ĞĞ Ğ¢ò¢ Ğ¢¢'&–VbWFFR'BöbF†R67&VVâg&öÒF†R&6²'VffW Ğ¢¢ğĞ§fö–BFô&Æ—E67&VVâ…&V7FævÆR&VĞ§°Ğ¢6–fFVbDT%TuôDõô$Ä•Eõ45$TTàĞ –6öç7B7W&f6Rf÷WBÒvÆö&Ä&6´'VffW"‚“°Ğ –6öç7BV–çC…÷BFV'Vt6öÆ÷"ÒÃ…õ$TC°Ğ ”G&t†÷&—¦öçFÄÆ–æR†÷WBÂ&Vç÷6—F–öâÂ&Vç6—¦Rçv–GF‚ÂFV'Vt6öÆ÷"“°Ğ ”G&t†÷&—¦öçFÄÆ–æR†÷WBÂ&Vç÷6—F–öâ²F—7Æ6VÖVçB²Â&Vç6—¦Ræ†V–v‡BÒÒÂ&Vç6—¦Rçv–GF‚ÂFV'Vt6öÆ÷"“°Ğ ”G&ufW'F–6ÄÆ–æR†÷WBÂ&Vç÷6—F–öâÂ&Vç6—¦Ræ†V–v‡BÂFV'Vt6öÆ÷"“°Ğ ”G&ufW'F–6ÄÆ–æR†÷WBÂ&Vç÷6—F–öâ²F—7Æ6VÖVçB²&Vç6—¦Rçv–GF‚ÒÂÒÂ&Vç6—¦Ræ†V–v‡BÂFV'Vt6öÆ÷"“°Ğ¢6VæF–`Ğ •4DÅõ&V7B7&5&V7BÒÖ¶U6FÅ&V7B†&V“°Ğ •4DÅõ&V7BG7E&V7BÒÖ¶U6FÅ&V7B†&V“°Ğ ”&ÇDf7B‚g7&5&V7BÂfG7E&V7B“°Ğ§ĞĞ Ğ¢ò¢ Ğ¢¢'&–Vb6†V6²&VæFW"—VÆ–æRæBWFFR–æF—f–GVÂ67&VVâ'G0Ğ¢¢&Ò÷WB÷WGWB7W&f6RàĞ¢¢&ÒGt†wB6V7F–öâöb67&VVâFòWFFRg&öÒF÷Fò&÷GFöĞĞ¢¢&ÒG&tFW62&VæFW"–æfò&÷€Ğ¢¢&ÒG&t‡&VæFW"†VÇF‚& Ğ¢¢&ÒG&tÖæ&VæFW"Öæ& Ğ¢¢&ÒG&u6&"&VæFW"&VÇ@Ğ¢¢&ÒG&t'Fâ&VæFW"æVÂ'WGFöç0Ğ¢¢ğĞ§fö–BG&tÖ–â†–çBGt†wBÂ&ööÂG&tFW62Â&ööÂG&t‡Â&ööÂG&tÖæÂ&ööÂG&u6&"Â&ööÂG&t'FâĞ§°Ğ ––b‚v$7F—fR’°Ğ —&WGW&ã°Ğ —ĞĞ Ğ –76W'B†Gt†wBãÒbbGt†wBÃÒvå67&VVä†V–v‡B“°Ğ Ğ ––b†Gt†wBâ’°Ğ ”Fô&Æ—E67&VVâ‡²²ÂÒÂ²vå67&VVåv–GF‚ÂGt†wBÒÒ“°Ğ —ĞĞ ––b†Gt†wBÂvå67&VVä†V–v‡B’°Ğ –6öç7Bö–çBÖ–åæVÅ÷6—F–öâÒvWDÖ–åæVÂ‚’ç÷6—F–öã°Ğ ––b†G&u6&"’°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²#BÂRÒÂ²#3"Â#‚ÒÒ“°Ğ —ĞĞ ––b†G&tFW62’°Ğ ––b„6†DfÆr’°Ğ ’òòv†Vâ6†B–çWB—2F—7Æ–VBÂF†R&VÇB—2†–FFVâæBF†R6†BÖ÷fW2WàĞ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²sÂbÒÂ²#“‚ÂbÒÒ“°Ğ —ÒVÇ6R°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²–æfô&÷…&V7Bç÷6—F–öâç‚Â–æfô&÷…&V7Bç÷6—F–öâç’ÒÂ²–æfô&÷…&V7Bç6—¦RÒÒ“°Ğ —ĞĞ —ĞĞ ––b†G&tÖæ’°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²CcÂÒÂ²ƒ‚Âs"ÒÒ“°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²ScBÂcBÒÂ²SbÂSbÒÒ“°Ğ —ĞĞ ––b†G&t‡’°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²“bÂÒÂ²ƒ‚Âs"ÒÒ“°Ğ —ĞĞ ––b†G&t'Fâ’°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²‚ÂrÒÂ²sBÂBÒÒ“°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²SS’ÂrÒÂ²sBÂC‚ÒÒ“°Ğ ––b†v$—4×VÇF—Æ–W"’°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²ƒbÂ“ÒÂ²3BÂ3"ÒÒ“°Ğ ”Fô&Æ—E67&VVâ‡²Ö–åæVÅ÷6—F–öâ²F—7Æ6VÖVçB²S#bÂ“ÒÂ²3BÂ3"ÒÒ“°Ğ —ĞĞ —ĞĞ ––b…&Wd7W'6÷%&V7Bç6—¦Rçv–GF‚Òbb&Wd7W'6÷%&V7Bç6—¦Ræ†V–v‡BÒ’°Ğ ”Fô&Æ—E67&VVâ…&Wd7W'6÷%&V7B“°Ğ —ĞĞ –6öç7B&V7FævÆRf7W'6÷%&V7BÒvWDG&vä7W'6÷"‚’ç&V7C°Ğ ––b†7W'6÷%&V7Bç6—¦Rçv–GF‚Òbb7W'6÷%&V7Bç6—¦Ræ†V–v‡BÒ’°Ğ ”Fô&Æ—E67&VVâ†7W'6÷%&V7B“°Ğ —ĞĞ —ĞĞ§ĞĞ Ğ§fö–B÷F–öå6†÷te46†ævVB‚Ğ§°Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç6†÷te2Ğ ”Væ&ÆTg&ÖT6÷VçB‚“°Ğ –VÇ6PĞ –g&ÖVfÆrÒfÇ6S°Ğ§ĞĞ¦6öç7BWFò÷F–öä6†ævT†æFÆW%6†÷te2Ò„vWD÷F–öç2‚’äw&†–72ç6†÷te2å6WEfÇVT6†ævVD6ÆÆ&6²„÷F–öå6†÷te46†ævVB’ÂG'VR“°Ğ Ğ Ğ Ğ§ÒòòæÖW76PĞ Ğ¤F—7Æ6VÖVçBvWDöfg6WDf÷%vÆ¶–ær†6öç7Bæ–ÖF–öä–æfòfæ–ÖF–öä–æfòÂ6öç7BF—&V7F–öâF—"Â&ööÂ6ÖW&ÖöFRò£ÒfÇ6R¢òĞ§°Ğ ’òò6ÆærÖf÷&ÖBöf`Ğ ’òò6÷WF‚Â6÷WF…vW7BÂvW7BÂæ÷'F…vW7BÂæ÷'F‚Âæ÷'F„V7BÂV7BÂ6÷WF„V7BÀĞ –6öç7FW‡"F—7Æ6VÖVçBÖ÷f–ætöfg6WE³…ÒÒ²²Â3"ÒÂ²Ó3"ÂbÒÂ²ÓcBÂÒÂ²Ó3"ÂÓbÒÂ²ÂÓ3"ÒÂ²3"ÂÓbÒÂ²cBÂÒÂ²3"ÂbÒÓ°Ğ ’òò6ÆærÖf÷&ÖBöàĞ Ğ –6öç7BV–çC…÷Bæ–ÖF–öå&öw&W72Òæ–ÖF–öä–æfòævWDæ–ÖF–öå&öw&W72‚“°Ğ ”F—7Æ6VÖVçBöfg6WBÒÖ÷f–ætöfg6WE·7FF–5ö67CÇ6—¦U÷Câ†F—"•Ó°Ğ –öfg6WB£Òæ–ÖF–öå&öw&W73°Ğ –öfg6WBóÒæ–ÖF–öä–æfó£¦&6UfÇVTg&7F–öã°Ğ Ğ ––b†6ÖW&ÖöFR’°Ğ –öfg6WBÒÖöfg6WC°Ğ —ĞĞ Ğ —&WGW&âöfg6WC°Ğ§ĞĞ Ğ§fö–B6ÆV$7W'6÷"‚’òò4ôDUôd•ƒ¢F†—2v27W÷6VBFò&R–â7W'6÷"æ7 Ğ§°Ğ •&Wd7W'6÷%&V7BÒ·Ó°Ğ§ĞĞ Ğ§fö–B6†–gDw&–B…ö–çB¦öfg6WBÂ–çB†÷&—¦öçFÂÂ–çBfW'F–6ÂĞ§°Ğ –öfg6WBÓç‚³ÒfW'F–6Â²†÷&—¦öçFÃ°Ğ –öfg6WBÓç’³ÒfW'F–6ÂÒ†÷&—¦öçFÃ°Ğ§ĞĞ Ğ¦–çB&÷w46÷fW&VD'•æVÂ‚Ğ§°Ğ –6öç7BWFòfÖ–åæVÅ6—¦RÒvWDÖ–åæVÂ‚’ç6—¦S°Ğ ––b„vWE67&VVåv–GF‚‚’ÃÒÖ–åæVÅ6—¦Rçv–GF‚’°Ğ —&WGW&â°Ğ —ĞĞ Ğ ––çB&÷w2ÒÖ–åæVÅ6—¦Ræ†V–v‡BòD”ÄUô„T”t…C°Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒ’°Ğ —&÷w2óÒ#°Ğ —ĞĞ Ğ —&WGW&â&÷w3°Ğ§ĞĞ Ğ§fö–B6Æ5F–ÆTöfg6WB†–çB¦öfg6WE‚Â–çB¦öfg6WE’Ğ§°Ğ –6öç7BV–çCe÷B67&VVåv–GF‚ÒvWEf–Ww÷'Ev–GF‚‚“°Ğ –6öç7BV–çCe÷Bf–Ww÷'D†V–v‡BÒvWEf–Ww÷'D†V–v‡B‚“°Ğ Ğ ––çBƒ°Ğ ––çB“°Ğ Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒ’°Ğ —‚Ò67&VVåv–GF‚RD”ÄUõt”EDƒ°Ğ —’Òf–Ww÷'D†V–v‡BRD”ÄUô„T”t…C°Ğ —ÒVÇ6R°Ğ —‚Ò‡67&VVåv–GF‚ò"’RD”ÄUõt”EDƒ°Ğ —’Ò‡f–Ww÷'D†V–v‡Bò"’RD”ÄUô„T”t…C°Ğ —ĞĞ Ğ ––b‡‚ÒĞ —‚Ò…D”ÄUõt”ED‚Ò‚’ò#°Ğ ––b‡’ÒĞ —’Ò…D”ÄUô„T”t…BÒ’’ò#°Ğ Ğ ’¦öfg6WE‚Òƒ°Ğ ’¦öfg6WE’Ò“°Ğ§ĞĞ Ğ§fö–BF–ÆW4–åf–Wr†–çB§&6öÇVÖç2Â–çB§'&÷w2Ğ§°Ğ –6öç7BV–çCe÷B67&VVåv–GF‚ÒvWEf–Ww÷'Ev–GF‚‚“°Ğ –6öç7BV–çCe÷Bf–Ww÷'D†V–v‡BÒvWEf–Ww÷'D†V–v‡B‚“°Ğ Ğ ––çB6öÇVÖç2Ò67&VVåv–GF‚òD”ÄUõt”EDƒ°Ğ ––b‚‡67&VVåv–GF‚RD”ÄUõt”ED‚’Ò’°Ğ –6öÇVÖç2²³°Ğ —ĞĞ ––çB&÷w2Òf–Ww÷'D†V–v‡BòD”ÄUô„T”t…C°Ğ ––b‚‡f–Ww÷'D†V–v‡BRD”ÄUô„T”t…B’Ò’°Ğ —&÷w2²³°Ğ —ĞĞ Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒ’°Ğ ’òò†ÆbF†RçVÖ&W"öbF–ÆW2Â&÷VæFVBW Ğ ––b‚†6öÇVÖç2b’Ò’°Ğ –6öÇVÖç2²³°Ğ —ĞĞ –6öÇVÖç2óÒ#°Ğ ––b‚‡&÷w2b’Ò’°Ğ —&÷w2²³°Ğ —ĞĞ —&÷w2óÒ#°Ğ —ĞĞ Ğ ’§&6öÇVÖç2Ò6öÇVÖç3°Ğ ’§'&÷w2Ò&÷w3°Ğ§ĞĞ Ğ§fö–B6Æ5f–Ww÷'DvVöÖWG'’‚Ğ§°Ğ –6öç7B–çB¦ööÔf7F÷"Ò¤vWD÷F–öç2‚’äw&†–72ç¦ööÒò"¢°Ğ –6öç7B–çB67&VVåv–GF‚ÒvWEf–Ww÷'Ev–GF‚‚’ò¦ööÔf7F÷#°Ğ •µ¶Ö–&U÷VçW6VEÕÒ6öç7B–çB67&VVä†V–v‡BÒvWE67&VVä†V–v‡B‚’ò¦ööÔf7F÷#°Ğ •µ¶Ö–&U÷VçW6VEÕÒ6öç7B–çBæVÄ†V–v‡BÒvWDÖ–åæVÂ‚’ç6—¦Ræ†V–v‡Bò¦ööÔf7F÷#°Ğ¢6–fFVbõó4E5õğĞ ’òòF†Rv÷&ÆBf–ÆÇ2F†RF÷67&VVââF†RÖ–âæVÂ—2öâF†R&÷GFöÒ67&VVâàĞ –6öç7B–çB—†VÇ5FõæVÂÒvWEf–Ww÷'D†V–v‡B‚’ò¦ööÔf7F÷#°Ğ¢6VÇ6PĞ –6öç7B–çB—†VÇ5FõæVÂÒ67&VVä†V–v‡BÒæVÄ†V–v‡C°Ğ¢6VæF–`Ğ •ö–çBÆ–W%÷6—F–öâ²67&VVåv–GF‚ò"Â—†VÇ5FõæVÂò"Ó°Ğ¢6–fFVbõó4E5õğĞ —Æ–W%÷6—F–öâç’³Ò7G$6ÖW&fW'F–6Äöfg6WBò¦ööÔf7F÷#°Ğ¢6VæF–`Ğ Ğ ––b‚¤vWD÷F–öç2‚’äw&†–72ç¦ööÒĞ —Æ–W%÷6—F–öâç’³ÒD”ÄUô„T”t…BòC°Ğ Ğ –6öç7B–çBF–ÆW5FõF÷Ò‡Æ–W%÷6—F–öâç’²D”ÄUô„T”t…BÒ’òD”ÄUô„T”t…C°Ğ –6öç7B–çBF–ÆW5FôÆVgBÒ‡Æ–W%÷6—F–öâç‚²D”ÄUõt”ED‚Ò’òD”ÄUõt”EDƒ°Ğ Ğ ’òòÆö6F–öâöbF†R6VçFW"öbF†RF–ÆRg&öÒv†–6‚Fò7F'B&VæFW&–ærÂ&VÆF—fRFòF†Rf–Ww÷'B÷&–v–àĞ •ö–çB7F'E÷6—F–öâÒÆ–W%÷6—F–öâÒF—7Æ6VÖVçB²F–ÆW5FôÆVgB¢D”ÄUõt”ED‚ÂF–ÆW5FõF÷¢D”ÄUô„T”t…BÓ°Ğ Ğ ’òò÷6—F–öâöbF†RF–ÆRg&öÒv†–6‚Fò7F'B&VæFW&–ær–âF–ÆR76RÀĞ ’òò&VÆF—fRFòF†RF–ÆRF†RÆ–W"6†&7FW"ö67W–W0Ğ —F–ÆU6†–gBÒ²ÂÓ°Ğ —F–ÆU6†–gB³ÒF—7Æ6VÖVçB„F—&V7F–öã£¤æ÷'F‚’¢F–ÆW5FõF÷°Ğ —F–ÆU6†–gB³ÒF—7Æ6VÖVçB„F—&V7F–öã£¥vW7B’¢F–ÆW5FôÆVgC°Ğ Ğ ’òòF†R&VæFW&–ærÆö÷W‡V7G2Fò7F'Böâ&÷rv—F‚fWvW"6öÇVÖç0Ğ ––b‡F–ÆW5FôÆVgB¢D”ÄUõt”ED‚ãÒÆ–W%÷6—F–öâç‚’°Ğ —7F'E÷6—F–öâ³ÒF—7Æ6VÖVçB²D”ÄUõt”ED‚ò"ÂÕD”ÄUô„T”t…Bò"Ó°Ğ —F–ÆU6†–gB³ÒF—7Æ6VÖVçB„F—&V7F–öã£¤æ÷'F„V7B“°Ğ —ÒVÇ6R–b‡F–ÆW5FõF÷¢D”ÄUô„T”t…BÂÆ–W%÷6—F–öâç’’°Ğ ’òòF†W&R—2öæR&÷r&÷fRF†R7W'&VçB&÷rF†BæVVG2Fò&R&VæFW&VBÀĞ ’òò'WBvR6¶—FòF†R&÷r&÷fR—B&V6W6R—B†2FöòÖç’6öÇVÖç0Ğ —7F'E÷6—F–öâ³ÒF—7Æ6VÖVçB²ÂÕD”ÄUô„T”t…BÓ°Ğ —F–ÆU6†–gB³ÒF—7Æ6VÖVçB„F—&V7F–öã£¤æ÷'F‚“°Ğ —ĞĞ Ğ ’òòÆö6F–öâöbF†R&÷GFöÒÖÆVgB6÷&æW"öbF†R&÷VæF–ær&÷‚&÷VæBF†PĞ ’òòF–ÆRg&öÒv†–6‚Fò7F'B&VæFW&–ærÂ&VÆF—fRFòF†Rf–Ww÷'B÷&–v–àĞ —F–ÆTöfg6WBÒ²7F'E÷6—F–öâç‚Ò…D”ÄUõt”ED‚ò"’Â7F'E÷6—F–öâç’²…D”ÄUô„T”t…Bò"’ÒÓ°Ğ Ğ ’òò6ö×WFRF†RçVÖ&W"öb&÷w2Fò&R&VæFW&VB2vVÆÂ0Ğ ’òòF†RçVÖ&W"öb6öÇVÖç2Fò&R&VæFW&VB–âF†Rf—'7B&÷pĞ –6öç7B–çBf–Ww÷'D†V–v‡BÒvWEf–Ww÷'D†V–v‡B‚’ò¦ööÔf7F÷#°Ğ –6öç7Bö–çB&VæFW%7F'BÒ7F'E÷6—F–öâÒF—7Æ6VÖVçB²D”ÄUõt”ED‚ò"ÂD”ÄUô„T”t…Bò"Ó°Ğ —F–ÆU&÷w2Ò‡f–Ww÷'D†V–v‡BÒ&VæFW%7F'Bç’²D”ÄUô„T”t…Bò"Ò’ò…D”ÄUô„T”t…Bò"“°Ğ —F–ÆT6öÇVÖç2Ò‡67&VVåv–GF‚Ò&VæFW%7F'Bç‚²D”ÄUõt”ED‚Ò’òD”ÄUõt”EDƒ°Ğ§ĞĞ Ğ¥ö–çBvWE67&VVå÷6—F–öâ…ö–çBF–ÆRĞ§°Ğ •ö–çBf—'7EF–ÆRÒf–Wu÷6—F–öã°Ğ ”F—7Æ6VÖVçBöfg6WBÒ·Ó°Ğ ”6Æ4f—'7EF–ÆU÷6—F–öâ†f—'7EF–ÆRÂöfg6WB“°Ğ Ğ –6öç7BF—7Æ6VÖVçBFVÇFÒf—'7EF–ÆRÒF–ÆS°Ğ Ğ •ö–çB÷6—F–öâ·Ó°Ğ —÷6—F–öâ³ÒFVÇFçv÷&ÆEFõ67&VVâ‚“°Ğ —÷6—F–öâ³Òöfg6WC°Ğ —&WGW&â÷6—F–öã°Ğ§ĞĞ Ğ¦W‡FW&â4DÅõ7W&f6R¥Å7W&f6S°Ğ Ğ§fö–B6ÆV%67&VVä'VffW"‚Ğ§°Ğ ––b„†VFÆW74ÖöFRĞ —&WGW&ã°Ğ Ğ –76W'B…Å7W&f6RÒçVÆÇG"“°Ğ¢6–fFVbõó4E5õğĞ ”7G%v÷&ÆD6ÆV$g&ÖR‚“°Ğ¢6VæF–`Ğ •4DÅôf–ÆÅ7W&f6U&V7B…Å7W&f6RÂçVÆÇG"Â“°Ğ§ĞĞ Ğ¢6–fFVbôDT%TpĞ§fö–B67&öÆÅf–Wr‚Ğ§°Ğ ––b‚×•Æ–W"Óä†öÆD—FVÒæ—4V×G’‚’Ğ —&WGW&ã°Ğ Ğ ––b„Ö÷W6U÷6—F–öâç‚Â#’°Ğ ––b†FÖ…÷6—F–öâç’ÒÃÒf–Wu÷6—F–öâç’ÇÂFÖ–å÷6—F–öâç‚ãÒf–Wu÷6—F–öâç‚’°Ğ ––b†FÖ…÷6—F–öâç’Òâf–Wu÷6—F–öâç’’°Ğ •f–Wu÷6—F–öâç’²³°Ğ —ĞĞ ––b†FÖ–å÷6—F–öâç‚Âf–Wu÷6—F–öâç‚’°Ğ •f–Wu÷6—F–öâç‚ÒÓ°Ğ —ĞĞ —ÒVÇ6R°Ğ •f–Wu÷6—F–öâç’²³°Ğ •f–Wu÷6—F–öâç‚ÒÓ°Ğ —ĞĞ —ĞĞ ––b„Ö÷W6U÷6—F–öâç‚âvå67&VVåv–GF‚Ò#’°Ğ ––b†FÖ…÷6—F–öâç‚ÒÃÒf–Wu÷6—F–öâç‚ÇÂFÖ–å÷6—F–öâç’ãÒf–Wu÷6—F–öâç’’°Ğ ––b†FÖ…÷6—F–öâç‚Òâf–Wu÷6—F–öâç‚’°Ğ •f–Wu÷6—F–öâç‚²³°Ğ —ĞĞ ––b†FÖ–å÷6—F–öâç’Âf–Wu÷6—F–öâç’’°Ğ •f–Wu÷6—F–öâç’ÒÓ°Ğ —ĞĞ —ÒVÇ6R°Ğ •f–Wu÷6—F–öâç’ÒÓ°Ğ •f–Wu÷6—F–öâç‚²³°Ğ —ĞĞ —ĞĞ ––b„Ö÷W6U÷6—F–öâç’Â#’°Ğ ––b†FÖ–å÷6—F–öâç’ãÒf–Wu÷6—F–öâç’ÇÂFÖ–å÷6—F–öâç‚ãÒf–Wu÷6—F–öâç‚’°Ğ ––b†FÖ–å÷6—F–öâç’Âf–Wu÷6—F–öâç’’°Ğ •f–Wu÷6—F–öâç’ÒÓ°Ğ —ĞĞ ––b†FÖ–å÷6—F–öâç‚Âf–Wu÷6—F–öâç‚’°Ğ •f–Wu÷6—F–öâç‚ÒÓ°Ğ —ĞĞ —ÒVÇ6R°Ğ •f–Wu÷6—F–öâç‚ÒÓ°Ğ •f–Wu÷6—F–öâç’ÒÓ°Ğ —ĞĞ —ĞĞ ––b„Ö÷W6U÷6—F–öâç’âvå67&VVä†V–v‡BÒ#’°Ğ ––b†FÖ…÷6—F–öâç’ÒÃÒf–Wu÷6—F–öâç’ÇÂFÖ…÷6—F–öâç‚ÒÃÒf–Wu÷6—F–öâç‚’°Ğ ––b†FÖ…÷6—F–öâç’Òâf–Wu÷6—F–öâç’’°Ğ •f–Wu÷6—F–öâç’²³°Ğ —ĞĞ ––b†FÖ…÷6—F–öâç‚Òâf–Wu÷6—F–öâç‚’°Ğ •f–Wu÷6—F–öâç‚²³°Ğ —ĞĞ —ÒVÇ6R°Ğ •f–Wu÷6—F–öâç‚²³°Ğ •f–Wu÷6—F–öâç’²³°Ğ —ĞĞ —ĞĞ§ĞĞ¢6VæF–`Ğ Ğ§fö–BVæ&ÆTg&ÖT6÷VçB‚Ğ§°Ğ –g&ÖVfÆrÒG'VS°Ğ –Æ7Dg5WFFT–ä×2Ò4DÅôvWEF–6·2‚“°Ğ§ĞĞ Ğ§fö–B67&öÆÇ'EöG&uövÖU÷67&VVâ‚Ğ§°Ğ ––b„†VFÆW74ÖöFRĞ —&WGW&ã°Ğ Ğ ––çB†wBÒ°Ğ Ğ ––b„—5&VG&tWfW'—F†–ær‚’’°Ğ •&VG&t6ö×ÆWFR‚“°Ğ –†wBÒvå67&VVä†V–v‡C°Ğ —ĞĞ Ğ –6öç7B7W&f6Rf÷WBÒvÆö&Ä&6´'VffW"‚“°Ğ •VæG&t7W'6÷"†÷WB“°Ğ ”G&t7W'6÷"†÷WB“°Ğ ”G&tÖ–â††wBÂfÇ6RÂfÇ6RÂfÇ6RÂfÇ6RÂfÇ6R“°Ğ¢6–fFVbõó4E5õğĞ ’òòF†RT’Æ–W"7F–ÆÂ†2F†R¶W’—†VÇ2öbF†RÆ7BgVÆÂg&ÖRàĞ ”7G%v÷&ÆE&WVDg&ÖR‚“°Ğ¢6VæF–`Ğ •&VæFW%&W6VçB‚“°Ğ§ĞĞ¢6–fFVbõó4E5õğĞ¦–çB7G$W‡W&–Væ6Tf–ÆÂ‚Ğ§°Ğ –6öç7BÆ–W"gÆ–W"Ò¤×•Æ–W#°Ğ ––b‡Æ–W"æ—4Ö„6†&7FW$ÆWfVÂ‚’Ğ —&WGW&â7G$&÷GFöÔW‡W&–Væ6T&"ç6—¦Rçv–GFƒ°Ğ –6öç7B–çBÆWfVÂÒÆ–W"ævWD6†&7FW$ÆWfVÂ‚“°Ğ –6öç7BV–çCcE÷B&Wf–÷W2ÒvWDæW‡DW‡W&–Væ6UF‡&W6†öÆDf÷$ÆWfVÂ†ÆWfVÂÒ“°Ğ –6öç7BV–çCcE÷BæW‡BÒvWDæW‡DW‡W&–Væ6UF‡&W6†öÆDf÷$ÆWfVÂ†ÆWfVÂ“°Ğ ––b†æW‡BÃÒ&Wf–÷W2ÇÂÆ–W"å÷W‡W&–Væ6RÃÒ&Wf–÷W2Ğ —&WGW&â°Ğ —&WGW&â7FF–5ö67CÆ–çCâ‡7FC£¦Ö–ãÇV–çCcE÷Câ„7G$&÷GFöÔW‡W&–Væ6T&"ç6—¦Rçv–GF‚ÀĞ ’‡Æ–W"å÷W‡W&–Væ6RÒ&Wf–÷W2’¢7G$&÷GFöÔW‡W&–Væ6T&"ç6—¦Rçv–GF‚ò†æW‡BÒ&Wf–÷W2’’“°Ğ§ĞĞ Ğ§fö–BG&t7G$&÷GFöÔ‡VB†6öç7B7W&f6Rf÷WBĞ§°Ğ —7FF–27FC£¦÷F–öæÃÄ÷væVE7W&f6Sâ'VffW#°Ğ ––b‚'VffW"Ğ –'VffW"æV×Æ6Rƒ3#Â#C“°Ğ •7W&f6Rf‡VBÒ¦'VffW#°Ğ ”f–ÆÅ&V7B†‡VBÂÂÂ3#Â#CÂ“°Ğ ”G&t7G$&$'Gv÷&²†‡VBÂ7G$&$'Gv÷&³£¤†VÇF‚Â7G$&÷GFöÔ†VÇF„&"ç÷6—F–öâÂ7FC£¦6Æ×„×•Æ–W"Óå÷…W"ÂÂƒ’òƒãb“°Ğ ”G&t7G$&$'Gv÷&²†‡VBÂ7G$&$'Gv÷&³£¤ÖæÂ7G$&÷GFöÔÖæ&"ç÷6—F–öâÂ7FC£¦6Æ×„×•Æ–W"Óå÷ÖæW"ÂÂƒ’òƒãb“°Ğ ”G&t7G$&÷GFöÔ&6¶w&÷VæB†‡VB“°Ğ ”G&t7G$–çd&VÇB†‡VB“°Ğ ”G&t7G$‡VE7VÆÄ–6öç2†‡VB“°Ğ •&W&T7G$‡VD÷fW&Æ’„7G$W‡W&–Væ6Tf–ÆÂ‚’ÂvÖVçUö—5ö7F—fR‚’Â×•Æ–W$—4FVB“°Ğ ”G&t–æfô&÷‚†‡VB“°Ğ ––b…7VÆÆ&öö´fÆrĞ ”G&t7G%7VÆÄFW67&—F–öâ†‡VB“°Ğ –WFòG&t&%fÇVRÒ²f‡VEÒ†–çB7W'%fÇVRÂ–çBÖ…fÇVRÂ–çB6VçFW%‚Â–çB’’°Ğ –6öç7B7FC£§7G&–ærFW‡BÒ7G$6B†7W'%fÇVRÂ"ò"ÂÖ…fÇVR“°Ğ –6öç7BV”fÆw26öÆ÷"ÒV”fÆw3£¤6öÆ÷$vöÆC°Ğ –6öç7B–çBFW‡Ev–GF‚ÒvWDÆ–æUv–GF‚‡FW‡BÂvÖTföçC"Â“°Ğ –6öç7B–çB‚Ò7FC£¦Ö‚ƒÂ6VçFW%‚ÒFW‡Ev–GF‚ò"“°Ğ ”G&u7G&–ær†‡VBÂFW‡BÂ²²‚ÒÂ’ÒÂ²‡VBçr‚’Ò‡‚Ò’ÂÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷$&Æ6²ÂV”fÆw3£¤¶W&æ–ætf—E76–ærÂç76–ærÒÒ“°Ğ ”G&u7G&–ær†‡VBÂFW‡BÂ²²‚²Â’ÒÂ²‡VBçr‚’Ò‡‚²’ÂÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷$&Æ6²ÂV”fÆw3£¤¶W&æ–ætf—E76–ærÂç76–ærÒÒ“°Ğ ”G&u7G&–ær†‡VBÂFW‡BÂ²²‚Â’ÒÒÂ²‡VBçr‚’Ò‚ÂÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷$&Æ6²ÂV”fÆw3£¤¶W&æ–ætf—E76–ærÂç76–ærÒÒ“°Ğ ”G&u7G&–ær†‡VBÂFW‡BÂ²²‚Â’²ÒÂ²‡VBçr‚’Ò‚ÂÒÒÀĞ ’²æfÆw2ÒV”fÆw3£¤6öÆ÷$&Æ6²ÂV”fÆw3£¤¶W&æ–ætf—E76–ærÂç76–ærÒÒ“°Ğ ”G&u7G&–ær†‡VBÂFW‡BÂ²²‚Â’ÒÂ²‡VBçr‚’Ò‚ÂÒÒÀĞ ’²æfÆw2Ò6öÆ÷"ÂV”fÆw3£¤¶W&æ–ætf—E76–ærÂç76–ærÒÒ“°Ğ —Ó°Ğ Ğ ––b‚¤vWD÷F–öç2‚’ävÖWÆ’ç6†÷t†VÇF…fÇVW2’°Ğ –6öç7B–çB7W'$…Ò7FC£¦Ö‚ƒÂ×•Æ–W"Óå÷†—Eö–çG2ãâb“°Ğ –6öç7B–çBÖ„…Ò×•Æ–W"Óå÷Ö„…ãâc°Ğ –G&t&%fÇVR†7W'$…ÂÖ„…ÀĞ ’7G$&÷GFöÔ†VÇF„&"ç÷6—F–öâç‚²7G$&÷GFöÔ†VÇF„&"ç6—¦Rçv–GF‚ò"ÀĞ ’7G$&÷GFöÔ†VÇF„&"ç÷6—F–öâç’ÒR“°Ğ —ĞĞ ––b‚¤vWD÷F–öç2‚’ävÖWÆ’ç6†÷tÖæfÇVW2’°Ğ –6öç7B–çB7W'$ÖæÒ„†4ç”öb„×•Æ–W"Óå÷”fÆw2Â—FVÕ7V6–ÄVffV7C£¤æôÖæ’ÇÂ×•Æ–W"Óæ†4æôÖæ‚’’ò¢7FC£¦Ö‚ƒÂ×•Æ–W"Óå÷Öæãâb“°Ğ –6öç7B–çBÖ„ÖæÒ†4ç”öb„×•Æ–W"Óå÷”fÆw2Â—FVÕ7V6–ÄVffV7C£¤æôÖæ’ò¢×•Æ–W"Óå÷Ö„Öæãâc°Ğ –G&t&%fÇVR†7W'$ÖæÂÖ„ÖæÀĞ ’7G$&÷GFöÔÖæ&"ç÷6—F–öâç‚²7G$&÷GFöÔÖæ&"ç6—¦Rçv–GF‚ò"ÀĞ ’7G$&÷GFöÔÖæ&"ç÷6—F–öâç’ÒR“°Ğ —ĞĞ ––b„×•Æ–W$—4FVB’°Ğ —V–çC…÷B¦G7BÒ‡VBæ&Vv–â‚“°Ğ —V–çC…÷B§F&ÂÒvWEW6UE$â‚“°Ğ –f÷"†–çB‚Ò‡VBæ‚‚“²‚Ò²‚ÒÒÂG7B³Ò‡VBç—F6‚‚’Ò‡VBçr‚’’°Ğ –f÷"†–çBrÒ‡VBçr‚“²rÒ²rÒÒ’°Ğ ––b†ÆWfVÇG—RÒEE•Uô„TÄÂÇÂ¦G7BãÒ3"Ğ ’¦G7BÒF&Å²¦G7EÓ°Ğ –G7B²³°Ğ —ĞĞ —ĞĞ —ĞĞ –÷WBå66ÆT&Æ—Dg&öÒ†‡VBÂ²ÂÂ3#Â#CÒÂ²Â#CÂvå67&VVåv–GF‚Â#CÒ“°Ğ§ĞĞ¢6VæF–`Ğ Ğ§fö–BG&tæD&Æ—B‚Ğ§°Ğ ––b‚v%'VävÖRÇÂ†VFÆW74ÖöFR’°Ğ —&WGW&ã°Ğ —ĞĞ Ğ ––çB†wBÒ°Ğ –&ööÂG&t†VÇF‚Ò—5&VG&t6ö×öæVçB…æVÄG&t6ö×öæVçC£¤†VÇF‚“°Ğ –&ööÂG&tÖæÒ—5&VG&t6ö×öæVçB…æVÄG&t6ö×öæVçC£¤Öæ“°Ğ –&ööÂG&t6öçG&öÄ'WGFöç2Ò—5&VG&t6ö×öæVçB…æVÄG&t6ö×öæVçC£¤6öçG&öÄ'WGFöç2“°Ğ –&ööÂG&t&VÇBÒ—5&VG&t6ö×öæVçB…æVÄG&t6ö×öæVçC£¤&VÇB“°Ğ –6öç7B&ööÂG&t6†D–çWBÒ6†DfÆs°Ğ –&ööÂG&t–æfô&÷‚ÒfÇ6S°Ğ –&ööÂG&t7G&ÅâÒfÇ6S°Ğ Ğ¢6–fæFVbõó4E5õğĞ –6öç7B&V7FævÆRfÖ–åæVÂÒvWDÖ–åæVÂ‚“°Ğ¢6VæF–`Ğ¢6–fFVbõó4E5õğĞ –G&t†VÇF‚ÒG'VS°Ğ –G&tÖæÒG'VS°Ğ –G&t6öçG&öÄ'WGFöç2ÒG'VS°Ğ –G&t&VÇBÒG'VS°Ğ –G&t–æfô&÷‚ÒfÇ6S°Ğ –G&t7G&ÅâÒG'VS°Ğ –†wBÒvå67&VVä†V–v‡C°Ğ¢6VÇ6PĞ ––b†vå67&VVåv–GF‚âÖ–åæVÂç6—¦Rçv–GF‚ÇÂ—5&VG&tWfW'—F†–ær‚’’°Ğ –G&t†VÇF‚ÒG'VS°Ğ –G&tÖæÒG'VS°Ğ –G&t6öçG&öÄ'WGFöç2ÒG'VS°Ğ –G&t&VÇBÒG'VS°Ğ –G&t–æfô&÷‚ÒfÇ6S°Ğ –G&t7G&ÅâÒG'VS°Ğ –†wBÒvå67&VVä†V–v‡C°Ğ —ÒVÇ6R–b„—5&VG&uf–Ww÷'B‚’’°Ğ –G&t–æfô&÷‚ÒG'VS°Ğ –G&t7G&ÅâÒfÇ6S°Ğ –†wBÒvåf–Ww÷'D†V–v‡C°Ğ —ĞĞ¢6VæF–`Ğ Ğ –6öç7B7W&f6Rf÷WBÒvÆö&Ä&6´'VffW"‚“°Ğ •VæG&t7W'6÷"†÷WB“°Ğ Ğ –çF‡&VEõWFFU&öw&W75FôæW‡DvÖUF–6²‚“°Ğ Ğ —F†—5÷6FÅ÷F‡&VC£§––VÆB‚“°Ğ¢6–fFVbõó4E5õğĞ –6öç7BV–çC3"&Æ6´6öÆ÷"Ò4DÅôÖ$t"†÷WBç7W&f6RÓæf÷&ÖBÂÂÂ“°Ğ •4DÅõ&V7B&÷GFöÕ&V7BÒÖ¶U6FÅ&V7BƒÂ#CÂvå67&VVåv–GF‚Âvå67&VVä†V–v‡BÒ#C“°Ğ •4DÅôf–ÆÅ&V7B†÷WBç7W&f6RÂf&÷GFöÕ&V7BÂ&Æ6´6öÆ÷"“°Ğ¢6VæF–`Ğ ”G&uf–Wr†÷WBÂf–Wu÷6—F–öâ“°Ğ¢6–fFVbõó4E5õğĞ ”G&t7G$&÷GFöÔ‡VB†÷WB“°Ğ¢6VÇ6PĞ ––b†G&t7G&Åâ’°Ğ ”G&tÖ–åæVÂ†÷WB“°Ğ —ĞĞ ––b†G&t†VÇF‚’°Ğ ”G&tÆ–fTfÆ6´Æ÷vW"†÷WBÂG&t7G&Åâ“°Ğ —ĞĞ ––b†G&tÖæ’°Ğ ”G&tÖæfÆ6´Æ÷vW"†÷WBÂG&t7G&Åâ“°Ğ Ğ ”G&u7VÆÂ†÷WB“°Ğ —ĞĞ ––b†G&t6öçG&öÄ'WGFöç2’°Ğ ”G&tÖ–åæVÄ'WGFöç2†÷WB“°Ğ —ĞĞ ––b†G&t&VÇB’°Ğ ”G&t–çd&VÇB†÷WB“°Ğ —ĞĞ¢6VæF–`Ğ ––b†G&t6†D–çWB’°Ğ ”G&t6†D&÷‚†÷WB“°Ğ —ĞĞ¢6–fæFVbõó4E5õğĞ ”G&u…&"†÷WB“°Ğ ––b‚¤vWD÷F–öç2‚’ävÖWÆ’ç6†÷t†VÇF…fÇVW2Ğ ”G&tfÆ6µfÇVW2†÷WBÂ²Ö–åæVÂç÷6—F–öâç‚²3BÂÖ–åæVÂç÷6—F–öâç’²#‚ÒÂ×•Æ–W"Óå÷†—Eö–çG2ãâbÂ×•Æ–W"Óå÷Ö„…ãâb“°Ğ ––b‚¤vWD÷F–öç2‚’ävÖWÆ’ç6†÷tÖæfÇVW2Ğ ”G&tfÆ6µfÇVW2†÷WBÂ²Ö–åæVÂç÷6—F–öâç‚²Ö–åæVÂç6—¦Rçv–GF‚Ò3‚ÂÖ–åæVÂç÷6—F–öâç’²#‚ÒÀĞ ’„†4ç”öb„–ç7V7EÆ–W"Óå÷”fÆw2Â—FVÕ7V6–ÄVffV7C£¤æôÖæ’ÇÂ×•Æ–W"Óæ†4æôÖæ‚’’ò¢×•Æ–W"Óå÷ÖæãâbÀĞ ’†4ç”öb„–ç7V7EÆ–W"Óå÷”fÆw2Â—FVÕ7V6–ÄVffV7C£¤æôÖæ’ò¢×•Æ–W"Óå÷Ö„Öæãâb“°Ğ¢6VæF–`Ğ ––b‚¤vWD÷F–öç2‚’ävÖWÆ’æfÆöF–æt–æfô&÷‚Ğ ”G&tfÆöF–æt–æfô&÷‚†÷WB“°Ğ Ğ ––b‚¤vWD÷F–öç2‚’ävÖWÆ’ç6†÷t×VÇF—Æ–W%'G”–æfòbb'G•6–FUæVÄ÷VâĞ ”G&u'G”ÖVÖ&W$–æfõæVÂ†÷WB“°Ğ Ğ ”G&t7W'6÷"†÷WB“°Ğ Ğ ”G&te2†÷WB“°Ğ Ğ –ÇV£¤vÖTG&t6ö×ÆWFR‚“°Ğ Ğ —F†—5÷6FÅ÷F‡&VC£§––VÆB‚“°Ğ ”G&tÖ–â††wBÂG&t–æfô&÷‚ÂG&t†VÇF‚ÂG&tÖæÂG&t&VÇBÂG&t6öçG&öÄ'WGFöç2“°Ğ Ğ¢6–fFVbôDT%TpĞ ”G&t6öç6öÆR†÷WB“°Ğ¢6VæF–`Ğ Ğ •&VG&t6ö×ÆWFR‚“°Ğ –f÷"†6öç7BæVÄG&t6ö×öæVçB6ö×öæVçB¢VçVÕ÷fÇVW3ÅæVÄG&t6ö×öæVçCâ‚’’°Ğ ––b„—5&VG&t6ö×öæVçB†6ö×öæVçB’’°Ğ •&VG&t6ö×öæVçD6ö×ÆWFR†6ö×öæVçB“°Ğ —ĞĞ —ĞĞ Ğ •&VæFW%&W6VçB‚“°Ğ§ĞĞ Ğ§ÒòòæÖW76RFWf–ÇWF–öàĞ 