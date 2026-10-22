#!/usr/bin/env python3
"""Exercise extracted 3DS DrawStringWithColors layout with pixel positions."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get("TEXT_RENDER_SOURCE", ROOT / "Source/engine/render/text_render.cpp")).read_text()


def function(signature):
    start = SOURCE.index(signature)
    opening = SOURCE.index("\n{", start) + 1
    depth = 1
    end = opening + 1
    while depth:
        depth += (SOURCE[end] == "{") - (SOURCE[end] == "}")
        end += 1
    return SOURCE[start:end]


STUBS = r'''
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include "DiabloUI/ui_flags.hpp"
#include "engine/rectangle.hpp"
using namespace devilution;
enum class CtrTextScale { None, TopScreen, BottomScreen };
using GameFontTables = int;
enum class text_color { ColorDialogWhite, White };
constexpr int GameFont12 = 0;
constexpr char32_t ZWSP = 0x200b, Utf8DecodeError = 0xfffd;
constexpr int BaseLineOffset[] = {0, 0, 0, 0, 0, 0};
struct FakeSurface { int w = 640; };
struct Surface {
    FakeSurface *surface = nullptr;
    struct { int x = 0; int y = 0; } region;
    int w() const { return surface == nullptr ? 640 : surface->w; }
    int h() const { return 240; }
    Surface subregion(int, int, int, int) const { return *this; }
    template <typename A, typename B> void ScaleBlitFromSkipColorIndexZero(const A &, B, B) const {}
};
int NativeOriginFromCanvasX(int);
struct Glyph { uint8_t code = 0; int width() const { return 7; } int height() const { return 8; } };
using ClxSprite = Glyph;
struct DrawStringFormatArg {
    using Value = std::variant<std::string_view, int>;
    std::string_view value;
    UiFlags flags;
    std::string_view GetFormatted() const { return value; }
    UiFlags GetFlags() const { return flags; }
    bool HasFormatted() const { return true; }
    void SetFormatted(std::string &&) {}
    const Value &valueVariant() const { static Value v { std::string_view {} }; return v; }
};
struct CurrentFont {
    bool load(int, text_color, char32_t) { return true; }
    void clear() {}
    Glyph glyph(uint8_t code) const { return {code}; }
};
struct TextRenderOptions { UiFlags flags = UiFlags::None; int spacing = 1; int lineHeight = 16; int cursorPosition = -1; bool cursorStatic = false; };
struct DrawnGlyph { char32_t code; int nativeX; int y; };
std::vector<DrawnGlyph> drawn;
[[noreturn]] void app_fatal(const char *) { std::abort(); }
void LogError(const char *, ...) {}
std::string FormatRuntime(std::string_view, int) { return {}; }
char32_t DecodeFirstUtf8CodePoint(std::string_view s, size_t *n) { *n = 1; return s.empty() ? Utf8DecodeError : static_cast<unsigned char>(s[0]); }
text_color GetColorFromFlags(UiFlags) { return text_color::White; }
int GetFontSizeFromUiFlags(UiFlags) { return GameFont12; }
int GetLineHeight(std::string_view, DrawStringFormatArg *, size_t, int) { return 16; }
int CountNewlines(std::string_view fmt, DrawStringFormatArg *args, size_t n) {
    int count = std::count(fmt.begin(), fmt.end(), '\n');
    for (size_t i = 0; i < n; ++i) count += std::count(args[i].value.begin(), args[i].value.end(), '\n');
    return count;
}
struct FmtArgParser {
    FmtArgParser(std::string_view, DrawStringFormatArg *, size_t, size_t offset = 0) : next(offset) {}
    std::optional<size_t> operator()(std::string_view &rest) {
        if (rest.size() < 2 || rest[0] != '{' || rest[1] != '}') return std::nullopt;
        rest.remove_prefix(2);
        return next++;
    }
    size_t offset() const { return next; }
    size_t next;
};
bool IsConsumed(std::string_view s) { return s.empty() || s[0] == '\0'; }
struct SDL_Rect {};
SDL_Rect MakeSdlRect(int, int, int, int) { return {}; }
struct OwnedSurface {
    OwnedSurface(int, int) {}
    int w() const { return 64; }
    int h() const { return 64; }
    void *surface = nullptr;
};
void SDL_FillSurfaceRect(void *, const SDL_Rect *, int) {}
void RenderClxSprite(OwnedSurface &, ClxSprite, Point) {}
int PentSpn2Spin() { return 0; }
std::vector<ClxSprite> *pSPentSpn2Cels = nullptr;
void ClxDraw(const Surface &, Point, ClxSprite) {}
void MaybeWrap(Point &, int, int, int, int) {}
Surface ClipSurface(const Surface &out, Rectangle rect) {
    return out.subregion(0, 0, std::min(rect.position.x + rect.size.width, out.w()),
        std::min(rect.position.y + rect.size.height, out.h()));
}
void DrawFont(const Surface &out, Point p, ClxSprite glyph, text_color, bool, CtrTextScale scale) {
    int x = p.x;
    if (scale == CtrTextScale::TopScreen) x = NativeOriginFromCanvasX(out.region.x + p.x);
    drawn.push_back({glyph.code, x, p.y});
}
'''

TEST = r'''
void Check(bool condition, int line) { if (!condition) { std::fprintf(stderr, "check failed at %d\\n", line); for (const auto &g : drawn) std::fprintf(stderr, "  %c x=%d y=%d\\n", char(g.code), g.nativeX, g.y); std::abort(); } }
#define check(condition) Check((condition), __LINE__)
std::string Expand(std::string_view fmt, std::string_view arg) {
    const size_t pos = fmt.find("{}");
    if (pos == std::string_view::npos) return std::string(fmt);
    return std::string(fmt.substr(0, pos)) + std::string(arg) + std::string(fmt.substr(pos + 2));
}
void CheckCase(UiFlags flags, std::string_view fmt, std::string_view arg, int spacing, int rectWidth) {
    FakeSurface backing;
    const std::string expanded = Expand(fmt, arg);
    for (int phase = 0; phase < 8; ++phase) {
        drawn.clear();
        Surface out { &backing, {phase, 0} };
        Rectangle rect {{0, 0}, {rectWidth, 160}};
        TextRenderOptions opts {flags, spacing, 16};
        DrawStringFormatArg args[] = {{arg, UiFlags::ColorYellow}};
        DrawStringWithColors(out, fmt, args, fmt.find("{}") == std::string_view::npos ? 0 : 1, rect, opts);
        check(drawn.size() == std::count_if(expanded.begin(), expanded.end(), [](char c) { return c != '\n'; }));

        size_t textPos = 0;
        size_t glyphPos = 0;
        while (glyphPos < drawn.size()) {
            while (textPos < expanded.size() && expanded[textPos] == '\n') ++textPos;
            const int y = drawn[glyphPos].y;
            size_t count = 0;
            while (glyphPos + count < drawn.size() && drawn[glyphPos + count].y == y) ++count;
            const size_t end = expanded.find('\n', textPos);
            const size_t lineEnd = end == std::string::npos ? expanded.size() : end;
            check(count <= lineEnd - textPos);
            const size_t remainingCount = lineEnd - textPos;
            const int nativeWidth = remainingCount == 0 ? 0 : int(remainingCount) * 7 + int(remainingCount - 1) * spacing;
            const int canvasWidth = ScaleNativeWidth(nativeWidth, CtrTextScale::TopScreen);
            const int canvasStart = GetCtrLineStartX(flags, rect, canvasWidth, CtrTextScale::TopScreen);
            const int expectedNativeStart = NativeOriginFromCanvasX(phase + canvasStart);
            for (size_t i = 0; i < count; ++i) {
                const DrawnGlyph &g = drawn[glyphPos++];
                check(g.code == static_cast<unsigned char>(expanded[textPos + i]));
                check(g.y == y);
                check(g.nativeX == expectedNativeStart + int(i) * (7 + spacing));
            }
            textPos += count;
        }
        check(glyphPos == drawn.size());
    }
}
int main() {
    for (UiFlags align : {UiFlags::AlignCenter, UiFlags::AlignRight})
    for (int spacing : {1, 2}) {
        CheckCase(align, "abc\ndefgh", "", spacing, 80);
        CheckCase(align, "ab{}cd", "xyz\nuvw", spacing, 96);
        CheckCase(align, "abcdefghijklmnop", "", spacing, 48);
        CheckCase(align, "ab{}cd", "efghijklmnop", spacing, 48);
    }
}
'''

with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "colored.cpp"
    exe = Path(directory) / "colored"
    helper_sources = [function(signature) for signature in (
        "inline int ScaleCharWidth(", "inline int ScaleSpacing(", "inline int ScaleBoundary(",
        "inline int ScaleNativeWidth(", "inline int NativeOriginFromCanvasX(",
        "inline int CanvasXFromNativeOrigin(", "inline int AdvanceTextX(", "inline int GlyphEndX(",
        "int GetLineStartX(", "int GetCtrLineStartX(",
        "int GetLineWidth(std::string_view fmt, DrawStringFormatArg *args, std::size_t argsLen, size_t argsOffset, GameFontTables size, int spacing, int *charactersInLine,\n    std::optional<size_t> firstArgOffset, CtrTextScale scale)",
        "int GetLineWidthWithScaledSpacing(std::string_view fmt, DrawStringFormatArg *args, size_t argsLen, size_t argsOffset,")]
    draw = function("void DrawStringWithColors(")
    spacing_fit = r'''template <typename Measure>
int AdjustCtrSpacingToFitHorizontally(int maxSpacing, int count, int available, int &lineWidth, Measure measure) {
    if (count < 2) { lineWidth = measure(maxSpacing); return maxSpacing; }
    for (int s = maxSpacing; s >= -1; --s) { lineWidth = measure(s); if (lineWidth <= available) return s; }
    lineWidth = measure(-1); return -1;
}'''
    cpp.write_text(STUBS + "\n" + "\n".join(helper_sources) + "\n" + spacing_fit + "\n" + draw + "\n" + TEST)
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++17", "-D__3DS__", "-I" + str(ROOT / "Source"), str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("PASS: colored renderer glyph positions match native line widths across 8:5 phases, newlines, and wraps")
