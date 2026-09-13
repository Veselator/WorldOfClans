#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <charconv>

namespace woc
{
    namespace
    {
        const Json& NullJson()
        {
            static const Json s_null;
            return s_null;
        }

        const std::string& EmptyString()
        {
            static const std::string s_empty;
            return s_empty;
        }

        void EncodeString(const std::string& in, std::string& out)
        {
            out += '"';
            for (unsigned char c : in)
            {
                switch (c)
                {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                default:
                    // Non-ASCII bytes are UTF-8 payload and pass through verbatim.
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else
                    {
                        out += static_cast<char>(c);
                    }
                }
            }
            out += '"';
        }

        /// Recursive-descent parser over a null-terminated buffer.
        class Parser
        {
        public:
            explicit Parser(const std::string& text) : m_text(text) {}

            bool ParseValue(Json& out)
            {
                SkipWhitespace();
                if (m_pos >= m_text.size()) return Fail("unexpected end of input");

                switch (m_text[m_pos])
                {
                case '{': return ParseObject(out);
                case '[': return ParseArray(out);
                case '"': { std::string s; if (!ParseString(s)) return false; out = Json(std::move(s)); return true; }
                case 't':
                    if (m_text.compare(m_pos, 4, "true") == 0) { m_pos += 4; out = Json(true); return true; }
                    return Fail("invalid literal");
                case 'f':
                    if (m_text.compare(m_pos, 5, "false") == 0) { m_pos += 5; out = Json(false); return true; }
                    return Fail("invalid literal");
                case 'n':
                    if (m_text.compare(m_pos, 4, "null") == 0) { m_pos += 4; out = Json(); return true; }
                    return Fail("invalid literal");
                default: return ParseNumber(out);
                }
            }

            const std::string& Error() const { return m_error; }
            size_t Position() const { return m_pos; }

            void SkipWhitespace()
            {
                while (m_pos < m_text.size())
                {
                    const char c = m_text[m_pos];
                    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++m_pos; continue; }
                    // Line comments are tolerated so designers can annotate config files.
                    if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/')
                    {
                        while (m_pos < m_text.size() && m_text[m_pos] != '\n') ++m_pos;
                        continue;
                    }
                    if (c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '*')
                    {
                        m_pos += 2;
                        while (m_pos + 1 < m_text.size() && !(m_text[m_pos] == '*' && m_text[m_pos + 1] == '/')) ++m_pos;
                        m_pos = (m_pos + 1 < m_text.size()) ? m_pos + 2 : m_text.size();
                        continue;
                    }
                    break;
                }
            }

        private:
            bool Fail(const char* msg)
            {
                if (m_error.empty())
                {
                    std::ostringstream os;
                    os << msg << " at offset " << m_pos;
                    m_error = os.str();
                }
                return false;
            }

            bool ParseObject(Json& out)
            {
                ++m_pos; // consume '{'
                Json::Object obj;
                SkipWhitespace();
                if (m_pos < m_text.size() && m_text[m_pos] == '}') { ++m_pos; out = Json(std::move(obj)); return true; }

                while (true)
                {
                    SkipWhitespace();
                    std::string key;
                    if (!ParseString(key)) return false;
                    SkipWhitespace();
                    if (m_pos >= m_text.size() || m_text[m_pos] != ':') return Fail("expected ':'");
                    ++m_pos;
                    Json value;
                    if (!ParseValue(value)) return false;
                    obj[std::move(key)] = std::move(value);
                    SkipWhitespace();
                    if (m_pos >= m_text.size()) return Fail("unterminated object");
                    if (m_text[m_pos] == ',') { ++m_pos; continue; }
                    if (m_text[m_pos] == '}') { ++m_pos; break; }
                    return Fail("expected ',' or '}'");
                }
                out = Json(std::move(obj));
                return true;
            }

            bool ParseArray(Json& out)
            {
                ++m_pos; // consume '['
                Json::Array arr;
                SkipWhitespace();
                if (m_pos < m_text.size() && m_text[m_pos] == ']') { ++m_pos; out = Json(std::move(arr)); return true; }

                while (true)
                {
                    Json value;
                    if (!ParseValue(value)) return false;
                    arr.push_back(std::move(value));
                    SkipWhitespace();
                    if (m_pos >= m_text.size()) return Fail("unterminated array");
                    if (m_text[m_pos] == ',') { ++m_pos; continue; }
                    if (m_text[m_pos] == ']') { ++m_pos; break; }
                    return Fail("expected ',' or ']'");
                }
                out = Json(std::move(arr));
                return true;
            }

            bool ParseString(std::string& out)
            {
                if (m_pos >= m_text.size() || m_text[m_pos] != '"') return Fail("expected string");
                ++m_pos;
                out.clear();
                while (m_pos < m_text.size())
                {
                    const char c = m_text[m_pos++];
                    if (c == '"') return true;
                    if (c != '\\') { out += c; continue; }
                    if (m_pos >= m_text.size()) break;
                    const char esc = m_text[m_pos++];
                    switch (esc)
                    {
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/';  break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'u':
                    {
                        if (m_pos + 4 > m_text.size()) return Fail("truncated \\u escape");
                        const u32 cp = static_cast<u32>(std::strtoul(m_text.substr(m_pos, 4).c_str(), nullptr, 16));
                        m_pos += 4;
                        AppendUtf8(out, cp);
                        break;
                    }
                    default: return Fail("invalid escape");
                    }
                }
                return Fail("unterminated string");
            }

            static void AppendUtf8(std::string& out, u32 cp)
            {
                if (cp < 0x80) { out += static_cast<char>(cp); }
                else if (cp < 0x800)
                {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
                else
                {
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            bool ParseNumber(Json& out)
            {
                const size_t start = m_pos;
                if (m_pos < m_text.size() && (m_text[m_pos] == '-' || m_text[m_pos] == '+')) ++m_pos;
                while (m_pos < m_text.size())
                {
                    const char c = m_text[m_pos];
                    if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') ++m_pos;
                    else break;
                }
                if (m_pos == start) return Fail("expected value");
                out = Json(std::strtod(m_text.substr(start, m_pos - start).c_str(), nullptr));
                return true;
            }

            const std::string& m_text;
            size_t m_pos = 0;
            std::string m_error;
        };
    }

    bool Json::AsBool(bool fallback) const
    {
        if (m_type == Type::Bool) return m_bool;
        if (m_type == Type::Number) return m_number != 0.0;
        return fallback;
    }

    f64 Json::AsNumber(f64 fallback) const
    {
        if (m_type == Type::Number) return m_number;
        if (m_type == Type::Bool) return m_bool ? 1.0 : 0.0;
        return fallback;
    }

    const std::string& Json::AsString() const
    {
        return m_type == Type::String ? m_string : EmptyString();
    }

    std::string Json::AsString(const std::string& fallback) const
    {
        return m_type == Type::String ? m_string : fallback;
    }

    const Json::Array& Json::AsArray() const
    {
        static const Array s_empty;
        return m_type == Type::Array ? m_array : s_empty;
    }

    Json::Array& Json::AsArray()
    {
        if (m_type != Type::Array) { m_type = Type::Array; m_array.clear(); }
        return m_array;
    }

    const Json::Object& Json::AsObject() const
    {
        static const Object s_empty;
        return m_type == Type::Object ? m_object : s_empty;
    }

    Json::Object& Json::AsObject()
    {
        if (m_type != Type::Object) { m_type = Type::Object; m_object.clear(); }
        return m_object;
    }

    bool Json::Has(const std::string& key) const
    {
        return m_type == Type::Object && m_object.find(key) != m_object.end();
    }

    const Json& Json::operator[](const std::string& key) const
    {
        if (m_type != Type::Object) return NullJson();
        const auto it = m_object.find(key);
        return it == m_object.end() ? NullJson() : it->second;
    }

    Json& Json::operator[](const std::string& key)
    {
        if (m_type != Type::Object) { m_type = Type::Object; m_object.clear(); }
        return m_object[key];
    }

    const Json& Json::operator[](size_t index) const
    {
        if (m_type != Type::Array || index >= m_array.size()) return NullJson();
        return m_array[index];
    }

    Json& Json::operator[](size_t index)
    {
        if (m_type != Type::Array) { m_type = Type::Array; m_array.clear(); }
        if (index >= m_array.size()) m_array.resize(index + 1);
        return m_array[index];
    }

    size_t Json::Size() const
    {
        if (m_type == Type::Array) return m_array.size();
        if (m_type == Type::Object) return m_object.size();
        return 0;
    }

    void Json::Push(Json value)
    {
        if (m_type != Type::Array) { m_type = Type::Array; m_array.clear(); }
        m_array.push_back(std::move(value));
    }

    const Json& Json::Get(const std::string& path) const
    {
        const Json* current = this;
        size_t start = 0;
        while (start <= path.size())
        {
            const size_t slash = path.find('/', start);
            const std::string token = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!token.empty())
            {
                if (current->m_type == Type::Array)
                {
                    const long idx = std::strtol(token.c_str(), nullptr, 10);
                    if (idx < 0 || static_cast<size_t>(idx) >= current->m_array.size()) return NullJson();
                    current = &current->m_array[static_cast<size_t>(idx)];
                }
                else if (current->m_type == Type::Object)
                {
                    const auto it = current->m_object.find(token);
                    if (it == current->m_object.end()) return NullJson();
                    current = &it->second;
                }
                else
                {
                    return NullJson();
                }
            }
            if (slash == std::string::npos) break;
            start = slash + 1;
        }
        return *current;
    }

    void Json::DumpTo(std::string& out, int indent, int depth) const
    {
        const bool pretty = indent > 0;
        const std::string pad = pretty ? std::string(static_cast<size_t>(indent) * (depth + 1), ' ') : std::string();
        const std::string padEnd = pretty ? std::string(static_cast<size_t>(indent) * depth, ' ') : std::string();

        switch (m_type)
        {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += m_bool ? "true" : "false"; break;
        case Type::Number:
        {
            char buf[40];
            if (m_number == static_cast<f64>(static_cast<i64>(m_number)) &&
                m_number > -1e15 && m_number < 1e15)
            {
                std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(m_number));
            }
            else
            {
                // The shortest text that reads back as exactly the same number. Six
                // significant digits used to be written here, which turned an order to march
                // to 1234.567 into an order to march to 1234.57 on every machine but the one
                // that gave it - and a lockstep party out of step on the very first order.
                const auto result = std::to_chars(buf, buf + sizeof(buf) - 1, m_number);
                *result.ptr = '\0';
            }
            out += buf;
            break;
        }
        case Type::String: EncodeString(m_string, out); break;
        case Type::Array:
        {
            if (m_array.empty()) { out += "[]"; break; }
            out += '[';
            bool first = true;
            for (const Json& item : m_array)
            {
                if (!first) out += ',';
                first = false;
                if (pretty) { out += '\n'; out += pad; }
                item.DumpTo(out, indent, depth + 1);
            }
            if (pretty) { out += '\n'; out += padEnd; }
            out += ']';
            break;
        }
        case Type::Object:
        {
            if (m_object.empty()) { out += "{}"; break; }
            out += '{';
            bool first = true;
            for (const auto& [key, value] : m_object)
            {
                if (!first) out += ',';
                first = false;
                if (pretty) { out += '\n'; out += pad; }
                EncodeString(key, out);
                out += pretty ? ": " : ":";
                value.DumpTo(out, indent, depth + 1);
            }
            if (pretty) { out += '\n'; out += padEnd; }
            out += '}';
            break;
        }
        }
    }

    std::string Json::Dump(int indent) const
    {
        std::string out;
        out.reserve(1024);
        DumpTo(out, indent, 0);
        return out;
    }

    Json Json::Parse(const std::string& text, std::string* error)
    {
        Parser parser(text);
        Json result;
        if (!parser.ParseValue(result))
        {
            if (error) *error = parser.Error();
            return Json();
        }
        if (error) error->clear();
        return result;
    }

    Json Json::LoadFile(const std::string& path, std::string* error)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            if (error) *error = "cannot open file: " + path;
            return Json();
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        std::string text = buffer.str();
        // Strip a UTF-8 BOM so files saved from Windows editors parse cleanly.
        if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
            static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
        {
            text.erase(0, 3);
        }
        return Parse(text, error);
    }

    bool Json::SaveFile(const std::string& path, int indent) const
    {
        std::ofstream file(path, std::ios::binary);
        if (!file) return false;
        const std::string text = Dump(indent);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        return file.good();
    }
}
