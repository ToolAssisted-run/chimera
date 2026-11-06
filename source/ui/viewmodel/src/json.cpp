#ifdef __APPLE__
#include "charconv_apple.hpp"   /* floating-point charconv is macOS 26 and later there */
#endif
#include "chimera/vm/json.hpp"
#include "chimera/vm/tr.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace chimera::vm::json {

namespace {
/* -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)? */
bool wellFormedNumber(const std::string &t)
{
	size_t i = 0;
	auto digits = [&] {
		const size_t from = i;
		while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++;
		return i > from;
	};
	if (i < t.size() && t[i] == '-') i++;
	if (i < t.size() && t[i] == '0') i++;
	else if (!digits()) return false;
	if (i < t.size() && t[i] == '.')
	{
		i++;
		if (!digits()) return false;
	}
	if (i < t.size() && (t[i] == 'e' || t[i] == 'E'))
	{
		i++;
		if (i < t.size() && (t[i] == '+' || t[i] == '-')) i++;
		if (!digits()) return false;
	}
	return i == t.size();
}


class Reader
{
public:
	explicit Reader(std::string_view s) : s_(s) {}

	bool document(Json &out, std::string &error)
	{
		if (!value(out, 0)) { error = error_; return false; }
		space();
		if (i_ != s_.size()) { error = tr("json.text_after_end", {std::to_string(i_)}); return false; }
		return true;
	}

private:
	bool fail(const std::string &what)
	{
		if (error_.empty()) error_ = tr("json.at_byte", {what, std::to_string(i_)});
		return false;
	}

	void space()
	{
		while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) i_++;
	}

	bool literal(std::string_view word)
	{
		if (s_.substr(i_, word.size()) != word) return fail(tr("json.unknown_word"));
		i_ += word.size();
		return true;
	}

	static void utf8(std::string &out, uint32_t c)
	{
		if (c < 0x80) out += static_cast<char>(c);
		else if (c < 0x800) { out += static_cast<char>(0xC0 | (c >> 6)); out += static_cast<char>(0x80 | (c & 0x3F)); }
		else if (c < 0x10000)
		{
			out += static_cast<char>(0xE0 | (c >> 12));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
		else
		{
			out += static_cast<char>(0xF0 | (c >> 18));
			out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (c & 0x3F));
		}
	}

	bool hex4(uint32_t &out)
	{
		if (i_ + 4 > s_.size()) return fail("short \\u escape");
		out = 0;
		for (int k = 0; k < 4; k++)
		{
			const char c = s_[i_++];
			out <<= 4;
			if (c >= '0' && c <= '9') out |= static_cast<uint32_t>(c - '0');
			else if (c >= 'a' && c <= 'f') out |= static_cast<uint32_t>(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') out |= static_cast<uint32_t>(c - 'A' + 10);
			else return fail("bad \\u escape");
		}
		return true;
	}

	bool string(std::string &out)
	{
		i_++; /* the opening quote */
		while (i_ < s_.size())
		{
			const char c = s_[i_++];
			if (c == '"') return true;
			if (c != '\\') { out += c; continue; }
			if (i_ >= s_.size()) break;
			const char e = s_[i_++];
			switch (e)
			{
			case '"': out += '"'; break;
			case '\\': out += '\\'; break;
			case '/': out += '/'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'n': out += '\n'; break;
			case 'r': out += '\r'; break;
			case 't': out += '\t'; break;
			case 'u':
			{
				uint32_t c1 = 0;
				if (!hex4(c1)) return false;
				if (c1 >= 0xD800 && c1 < 0xDC00 && s_.substr(i_, 2) == "\\u")
				{
					i_ += 2;
					uint32_t c2 = 0;
					if (!hex4(c2)) return false;
					c1 = 0x10000 + ((c1 - 0xD800) << 10) + (c2 - 0xDC00);
				}
				utf8(out, c1);
				break;
			}
			default: return fail(tr("json.bad_escape"));
			}
		}
		return fail(tr("json.unfinished_string"));
	}

	bool value(Json &out, int depth)
	{
		if (depth > 64) return fail(tr("json.too_deep"));
		space();
		if (i_ >= s_.size()) return fail(tr("json.unexpected_end"));
		const char c = s_[i_];
		if (c == '{')
		{
			out.kind = Json::Kind::Object;
			i_++;
			space();
			if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
			for (;;)
			{
				space();
				if (i_ >= s_.size() || s_[i_] != '"') return fail(tr("json.expected_name"));
				std::string name;
				if (!string(name)) return false;
				space();
				if (i_ >= s_.size() || s_[i_] != ':') return fail("expected ':'");
				i_++;
				Json v;
				if (!value(v, depth + 1)) return false;
				out.members.emplace_back(std::move(name), std::move(v));
				space();
				if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
				if (i_ < s_.size() && s_[i_] == '}') { i_++; return true; }
				return fail("expected ',' or '}'");
			}
		}
		if (c == '[')
		{
			out.kind = Json::Kind::Array;
			i_++;
			space();
			if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
			for (;;)
			{
				Json v;
				if (!value(v, depth + 1)) return false;
				out.items.push_back(std::move(v));
				space();
				if (i_ < s_.size() && s_[i_] == ',') { i_++; continue; }
				if (i_ < s_.size() && s_[i_] == ']') { i_++; return true; }
				return fail("expected ',' or ']'");
			}
		}
		if (c == '"') { out.kind = Json::Kind::String; return string(out.text); }
		if (c == 't') { out.kind = Json::Kind::Bool; out.boolean = true; return literal("true"); }
		if (c == 'f') { out.kind = Json::Kind::Bool; out.boolean = false; return literal("false"); }
		if (c == 'n') { out.kind = Json::Kind::Null; return literal("null"); }
		if (c == '-' || (c >= '0' && c <= '9'))
		{
			out.kind = Json::Kind::Number;
			while (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+' || s_[i_] == '.' || s_[i_] == 'e' || s_[i_] == 'E' || (s_[i_] >= '0' && s_[i_] <= '9')))
				out.text += s_[i_++];
			/* JSON's grammar, not just its characters: "-" and "1-2" were taken */
			if (!wellFormedNumber(out.text)) return fail(tr("json.bad_number", {out.text}));
			return true;
		}
		return fail(tr("json.unexpected_character"));
	}

	std::string_view s_;
	size_t i_ = 0;
	std::string error_;
};

} // namespace

const Json *Json::find(std::string_view name) const
{
	for (const auto &[k, v] : members)
		if (k == name) return &v;
	return nullptr;
}

double Json::number() const
{
	if (kind == Kind::Bool) return boolean ? 1.0 : 0.0;
	if (kind != Kind::Number) return 0.0;
	/* a dot whatever the locale (strtod stopped at the dot under a decimal comma) */
	double d = 0.0;
#ifdef __APPLE__
	apple::parsePrefix(text.data(), text.data() + text.size(), d);
#else
	std::from_chars(text.data(), text.data() + text.size(), d);
#endif
	return d;
}

Json Json::string(std::string s)
{
	Json j;
	j.kind = Kind::String;
	j.text = std::move(s);
	return j;
}

Json Json::number(double d)
{
	/* JSON has no NaN or infinity: written as null, which reads back as no value */
	if (!std::isfinite(d)) return Json();
	Json j;
	j.kind = Kind::Number;
	/* the shortest text that reads back as the same double, with a dot
	 * whatever the locale: snprintf wrote "0,5" under a decimal comma */
#ifdef __APPLE__
	j.text = apple::shortest(d);
#else
	char buf[40];
	const auto [end, ec] = std::to_chars(buf, buf + sizeof buf, d);
	j.text.assign(buf, ec == std::errc() ? end : buf);
#endif
	return j;
}

Json Json::integer(long long i)
{
	Json j;
	j.kind = Kind::Number;
	j.text = std::to_string(i);
	return j;
}

Json Json::boolean_(bool b)
{
	Json j;
	j.kind = Kind::Bool;
	j.boolean = b;
	return j;
}

bool parse(std::string_view text, Json &out, std::string &error)
{
	return Reader(text).document(out, error);
}

std::string quote(std::string_view s)
{
	std::string out = "\"";
	for (const char c : s)
	{
		switch (c)
		{
		case '"': out += "\\\""; break;
		case '\\': out += "\\\\"; break;
		case '\n': out += "\\n"; break;
		case '\r': out += "\\r"; break;
		case '\t': out += "\\t"; break;
		default:
			if (static_cast<unsigned char>(c) < 0x20)
			{
				char buf[8];
				std::snprintf(buf, sizeof buf, "\\u%04x", c);
				out += buf;
			}
			else
				out += c;
		}
	}
	return out + "\"";
}

std::string write(const Json &j)
{
	switch (j.kind)
	{
	case Json::Kind::Null: return "null";
	case Json::Kind::Bool: return j.boolean ? "true" : "false";
	case Json::Kind::Number: return j.text.empty() ? "0" : j.text;
	case Json::Kind::String: return quote(j.text);
	case Json::Kind::Array:
	{
		std::string out = "[";
		for (size_t i = 0; i < j.items.size(); i++) out += (i ? ", " : "") + write(j.items[i]);
		return out + "]";
	}
	case Json::Kind::Object:
	{
		std::string out = "{";
		for (size_t i = 0; i < j.members.size(); i++)
			out += (i ? ", " : "") + quote(j.members[i].first) + ": " + write(j.members[i].second);
		return out + "}";
	}
	}
	return "null";
}

} // namespace chimera::vm::json
