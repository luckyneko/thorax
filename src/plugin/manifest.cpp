/*
 *  Created by LuckyNeko on 23/05/2026.
 *  Copyright 2026 LuckyNeko
 *
 *  Distributed under the MIT Software License
 *  (See accompanying file LICENSE.md)
 */

#include "thx/plugin/manifest.h"
#include "thx/to_string.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

namespace thx::plugin
{

namespace
{
	// Minimal JSON reader tailored to the manifest schema. Supports objects,
	// arrays, strings (with the common escape sequences), and non-negative
	// integers. No floats, no `null`, no booleans, no unicode escapes.
	// Sufficient for our four-field manifest.
	class Reader
	{
	public:
		Reader(char const* begin, char const* end) noexcept
			: m_begin(begin), m_pos(begin), m_end(end) {}

		bool eof() const noexcept { return m_pos == m_end; }
		char peek() const noexcept { return m_pos < m_end ? *m_pos : '\0'; }
		void advance() noexcept { if (m_pos < m_end) ++m_pos; }

		void skipWs() noexcept
		{
			while (m_pos < m_end)
			{
				char c = *m_pos;
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
					++m_pos;
				else
					break;
			}
		}

		// Returns 1-based line number of the current position. Used in
		// error messages so authors can find the bad character quickly.
		std::size_t line() const noexcept
		{
			std::size_t n = 1;
			for (auto p = m_begin; p < m_pos; ++p)
				if (*p == '\n') ++n;
			return n;
		}

	private:
		char const* m_begin;
		char const* m_pos;
		char const* m_end;
	};

	// Build a malformed-manifest error with a position hint.
	Result<PluginManifest, Error> fail(Reader const& r, std::string const& path,
	                                   std::string const& msg)
	{
		std::ostringstream oss;
		oss << "Malformed manifest '" << path << "' (line " << r.line() << "): " << msg;
		return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest, oss.str()});
	}

	// Parse a quoted JSON string. Reader points at the opening '"' on entry;
	// on success, points past the closing '"'. Supports the common escape
	// sequences: \" \\ \/ \n \r \t \b \f.
	bool parseString(Reader& r, std::string& out)
	{
		if (r.peek() != '"') return false;
		r.advance();
		out.clear();
		while (!r.eof())
		{
			char c = r.peek();
			if (c == '"')
			{
				r.advance();
				return true;
			}
			if (c == '\\')
			{
				r.advance();
				char esc = r.peek();
				switch (esc)
				{
					case '"':  out += '"';  break;
					case '\\': out += '\\'; break;
					case '/':  out += '/';  break;
					case 'n':  out += '\n'; break;
					case 'r':  out += '\r'; break;
					case 't':  out += '\t'; break;
					case 'b':  out += '\b'; break;
					case 'f':  out += '\f'; break;
					default:   return false; // unknown escape
				}
				r.advance();
			}
			else
			{
				out += c;
				r.advance();
			}
		}
		return false; // unterminated
	}

	// Parse a non-negative integer. Reader points at the first digit on entry.
	bool parseInt(Reader& r, std::int64_t& out)
	{
		if (!std::isdigit(static_cast<unsigned char>(r.peek())))
			return false;
		std::int64_t v = 0;
		while (!r.eof() && std::isdigit(static_cast<unsigned char>(r.peek())))
		{
			v = v * 10 + (r.peek() - '0');
			r.advance();
		}
		out = v;
		return true;
	}

	// Parse a "X.Y.Z" version triple. The string is the JSON value; we use a
	// separate helper because Version doesn't have a string parser of its own.
	bool parseVersionTriple(std::string const& s, Version& out)
	{
		std::uint32_t parts[3] = {0, 0, 0};
		std::size_t idx = 0;
		std::size_t i   = 0;
		while (i < s.size() && idx < 3)
		{
			if (!std::isdigit(static_cast<unsigned char>(s[i])))
				return false;
			std::uint32_t v = 0;
			while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])))
			{
				v = v * 10 + static_cast<std::uint32_t>(s[i] - '0');
				++i;
			}
			parts[idx++] = v;
			if (i < s.size())
			{
				if (s[i] != '.') return false;
				++i;
			}
		}
		// Must have consumed exactly three components.
		if (idx != 3 || i != s.size())
			return false;
		out = Version{parts[0], parts[1], parts[2]};
		return true;
	}

	// Maximum nesting depth for objects/arrays passed to skipValue. The
	// parser only recurses on unknown fields (the manifest's own schema is
	// flat), so 32 levels is generously beyond anything legitimate while
	// still bounding the stack against adversarial input.
	constexpr int kSkipValueMaxDepth = 32;

	// Skip a JSON value (any type). Used for unknown fields so they don't
	// abort the parse. Reader points at the start of the value on entry;
	// on success, points just past it. Returns false if the nesting depth
	// exceeds kSkipValueMaxDepth — guards against adversarial input that
	// could otherwise stack-overflow.
	bool skipValue(Reader& r, int depth = 0)
	{
		if (depth >= kSkipValueMaxDepth)
			return false;

		r.skipWs();
		char c = r.peek();
		if (c == '"')
		{
			std::string discard;
			return parseString(r, discard);
		}
		if (std::isdigit(static_cast<unsigned char>(c)))
		{
			std::int64_t discard;
			return parseInt(r, discard);
		}
		if (c == '[')
		{
			r.advance();
			r.skipWs();
			if (r.peek() == ']') { r.advance(); return true; }
			while (true)
			{
				if (!skipValue(r, depth + 1)) return false;
				r.skipWs();
				if (r.peek() == ',') { r.advance(); r.skipWs(); continue; }
				if (r.peek() == ']') { r.advance(); return true; }
				return false;
			}
		}
		if (c == '{')
		{
			r.advance();
			r.skipWs();
			if (r.peek() == '}') { r.advance(); return true; }
			while (true)
			{
				std::string key;
				if (!parseString(r, key)) return false;
				r.skipWs();
				if (r.peek() != ':') return false;
				r.advance();
				if (!skipValue(r, depth + 1)) return false;
				r.skipWs();
				if (r.peek() == ',') { r.advance(); r.skipWs(); continue; }
				if (r.peek() == '}') { r.advance(); return true; }
				return false;
			}
		}
		return false;
	}

	// Parse a single ManifestRequirement object: {"id": "...", "version": "X.Y.Z"}.
	// Both fields required.
	bool parseRequirement(Reader& r, ManifestRequirement& out)
	{
		r.skipWs();
		if (r.peek() != '{') return false;
		r.advance();
		r.skipWs();

		bool haveId      = false;
		bool haveVersion = false;

		if (r.peek() == '}') return false; // empty object, missing both fields

		while (true)
		{
			std::string key;
			if (!parseString(r, key)) return false;
			r.skipWs();
			if (r.peek() != ':') return false;
			r.advance();
			r.skipWs();

			if (key == "id")
			{
				if (!parseString(r, out.id)) return false;
				haveId = true;
			}
			else if (key == "version")
			{
				std::string vs;
				if (!parseString(r, vs)) return false;
				if (!parseVersionTriple(vs, out.version)) return false;
				haveVersion = true;
			}
			else
			{
				if (!skipValue(r)) return false;
			}

			r.skipWs();
			if (r.peek() == ',') { r.advance(); r.skipWs(); continue; }
			if (r.peek() == '}') { r.advance(); break; }
			return false;
		}

		return haveId && haveVersion;
	}

	// Parse the whole manifest object.
	Result<PluginManifest, Error> parseDocument(Reader& r, std::string const& path)
	{
		r.skipWs();
		if (r.peek() != '{')
			return fail(r, path, "expected top-level object '{'");
		r.advance();
		r.skipWs();

		PluginManifest m;
		bool haveSchema   = false;
		bool haveName     = false;
		bool haveVersion  = false;
		bool haveProvides = false;
		bool haveRequires = false;

		if (r.peek() == '}')
			return fail(r, path, "manifest is empty");

		while (true)
		{
			std::string key;
			if (!parseString(r, key))
				return fail(r, path, "expected string key");
			r.skipWs();
			if (r.peek() != ':')
				return fail(r, path, "expected ':' after key '" + key + "'");
			r.advance();
			r.skipWs();

			if (key == "schema")
			{
				std::int64_t v;
				if (!parseInt(r, v))
					return fail(r, path, "'schema' must be an integer");
				m.schema = static_cast<int>(v);
				haveSchema = true;
			}
			else if (key == "name")
			{
				if (!parseString(r, m.name))
					return fail(r, path, "'name' must be a string");
				haveName = true;
			}
			else if (key == "version")
			{
				std::string vs;
				if (!parseString(r, vs))
					return fail(r, path, "'version' must be a string");
				if (!parseVersionTriple(vs, m.version))
					return fail(r, path, "'version' must be in major.minor.patch form (got '" + vs + "')");
				haveVersion = true;
			}
			else if (key == "provides")
			{
				if (r.peek() != '[')
					return fail(r, path, "'provides' must be an array");
				r.advance();
				r.skipWs();
				if (r.peek() == ']') { r.advance(); haveProvides = true; }
				else
				{
					while (true)
					{
						std::string item;
						if (!parseString(r, item))
							return fail(r, path, "'provides' entry must be a string");
						m.provides.push_back(std::move(item));
						r.skipWs();
						if (r.peek() == ',') { r.advance(); r.skipWs(); continue; }
						if (r.peek() == ']') { r.advance(); break; }
						return fail(r, path, "expected ',' or ']' in 'provides'");
					}
					haveProvides = true;
				}
			}
			else if (key == "requires")
			{
				if (r.peek() != '[')
					return fail(r, path, "'requires' must be an array");
				r.advance();
				r.skipWs();
				if (r.peek() == ']') { r.advance(); haveRequires = true; }
				else
				{
					while (true)
					{
						ManifestRequirement req;
						if (!parseRequirement(r, req))
							return fail(r, path, "'requires' entry must be an object with id and version");
						m.requirements.push_back(std::move(req));
						r.skipWs();
						if (r.peek() == ',') { r.advance(); r.skipWs(); continue; }
						if (r.peek() == ']') { r.advance(); break; }
						return fail(r, path, "expected ',' or ']' in 'requires'");
					}
					haveRequires = true;
				}
			}
			else
			{
				// Unknown field: skip silently for forward-compat within a schema.
				if (!skipValue(r))
					return fail(r, path, "malformed value for unknown field '" + key + "'");
			}

			r.skipWs();
			if (r.peek() == ',') { r.advance(); r.skipWs(); continue; }
			if (r.peek() == '}') { r.advance(); break; }
			return fail(r, path, "expected ',' or '}' between fields");
		}

		r.skipWs();
		if (!r.eof())
			return fail(r, path, "trailing characters after top-level object");

		if (!haveSchema)
			return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest,
				"Manifest '" + path + "' is missing required field 'schema'"});
		if (m.schema != 1)
			return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest,
				"Manifest '" + path + "' uses unsupported schema " + std::to_string(m.schema)
				+ " (this loader supports schema 1)"});
		if (!haveName)
			return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest,
				"Manifest '" + path + "' is missing required field 'name'"});
		if (!haveVersion)
			return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest,
				"Manifest '" + path + "' is missing required field 'version'"});
		if (!haveProvides)
			return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest,
				"Manifest '" + path + "' is missing required field 'provides'"});
		if (!haveRequires)
			return Result<PluginManifest, Error>::err({ErrorCode::MalformedManifest,
				"Manifest '" + path + "' is missing required field 'requires'"});

		return Result<PluginManifest, Error>::ok(std::move(m));
	}
} // namespace

Result<PluginManifest, Error> parseManifest(std::string const& jsonPath)
{
	std::ifstream in(jsonPath);
	if (!in)
		return Result<PluginManifest, Error>::err({ErrorCode::FileNotFound,
			"Cannot open manifest: " + jsonPath});

	std::ostringstream buf;
	buf << in.rdbuf();
	std::string contents = buf.str();

	Reader r(contents.data(), contents.data() + contents.size());
	return parseDocument(r, jsonPath);
}

namespace
{
	// JSON-string escape for the subset of escapes parseString() understands.
	std::string escapeJsonString(std::string_view s)
	{
		std::string out;
		out.reserve(s.size() + 2);
		for (char c : s)
		{
			switch (c)
			{
				case '"':  out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n";  break;
				case '\r': out += "\\r";  break;
				case '\t': out += "\\t";  break;
				case '\b': out += "\\b";  break;
				case '\f': out += "\\f";  break;
				default:   out += c;
			}
		}
		return out;
	}
} // namespace

std::string serialiseManifest(PluginManifest const& m)
{
	std::ostringstream out;
	out << "{\n";
	out << "  \"schema\":   " << m.schema << ",\n";
	out << "  \"name\":     \"" << escapeJsonString(m.name) << "\",\n";
	out << "  \"version\":  \"" << thx::toString(m.version) << "\",\n";

	out << "  \"provides\": [";
	if (!m.provides.empty())
	{
		out << "\n";
		for (std::size_t i = 0; i < m.provides.size(); ++i)
		{
			out << "    \"" << escapeJsonString(m.provides[i]) << "\"";
			if (i + 1 < m.provides.size()) out << ",";
			out << "\n";
		}
		out << "  ";
	}
	out << "],\n";

	out << "  \"requires\": [";
	if (!m.requirements.empty())
	{
		out << "\n";
		for (std::size_t i = 0; i < m.requirements.size(); ++i)
		{
			out << "    {\"id\": \"" << escapeJsonString(m.requirements[i].id)
			    << "\", \"version\": \"" << thx::toString(m.requirements[i].version) << "\"}";
			if (i + 1 < m.requirements.size()) out << ",";
			out << "\n";
		}
		out << "  ";
	}
	out << "]\n";
	out << "}\n";
	return out.str();
}

} // namespace thx::plugin
