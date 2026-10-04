#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#define JSMN_STATIC
#define JSMN_PARENT_LINKS
#include <jsmn.h>

namespace devilution {
// Read a top-level string field, never a matching word in release notes/assets.
inline std::optional<std::string> CtrJsonString(std::string_view json, std::string_view key)
{
	std::vector<jsmntok_t> tokens(128);
	int count;
	for (;;) {
		jsmn_parser parser;
		jsmn_init(&parser);
		count = jsmn_parse(&parser, json.data(), json.size(), tokens.data(), tokens.size());
		if (count != JSMN_ERROR_NOMEM || tokens.size() >= 32768)
			break;
		tokens.resize(tokens.size() * 2);
	}
	if (count < 1 || tokens[0].type != JSMN_OBJECT)
		return std::nullopt;
	for (int i = 1; i + 1 < count; ++i) {
		const auto &t = tokens[i];
		if (t.type != JSMN_STRING || t.parent != 0 || json.substr(t.start, t.end - t.start) != key)
			continue;
		const auto &value = tokens[i + 1];
		if (value.type != JSMN_STRING || value.parent != i)
			return std::nullopt;
		std::string result;
		for (int p = value.start; p < value.end; ++p) {
			char c = json[p];
			if (c == '\\') {
				if (++p >= value.end) return std::nullopt;
				c = json[p];
				if (c != '/' && c != '"' && c != '\\') return std::nullopt;
			}
			result.push_back(c);
		}
		return result;
	}
	return std::nullopt;
}

inline bool CtrValidReleaseTag(std::string_view tag)
{
	if (tag.empty() || tag.size() > 100) return false;
	for (unsigned char c : tag)
		if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '-' && c != '_' && c != '.') return false;
	return true;
}
} // namespace devilution
