// Json.h - dependency-free JSON DOM used by every data-driven part of the game.
//
// The whole project is configuration-first: unit stats, terrain rules, buildings,
// race definitions, map objects and saved games all round-trip through this type.
#pragma once

#include "Types.h"
#include <map>
#include <initializer_list>

namespace woc
{
    class Json
    {
    public:
        enum class Type { Null, Bool, Number, String, Array, Object };

        using Array = std::vector<Json>;
        using Object = std::map<std::string, Json>;

        Json() = default;
        Json(std::nullptr_t) {}
        Json(bool v) : m_type(Type::Bool), m_bool(v) {}
        Json(int v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}
        Json(u32 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}
        Json(i64 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}
        Json(u64 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}
        Json(f32 v) : m_type(Type::Number), m_number(static_cast<f64>(v)) {}
        Json(f64 v) : m_type(Type::Number), m_number(v) {}
        Json(const char* v) : m_type(Type::String), m_string(v ? v : "") {}
        Json(std::string v) : m_type(Type::String), m_string(std::move(v)) {}
        Json(Array v) : m_type(Type::Array), m_array(std::move(v)) {}
        Json(Object v) : m_type(Type::Object), m_object(std::move(v)) {}

        static Json MakeArray() { return Json(Array{}); }
        static Json MakeObject() { return Json(Object{}); }

        Type GetType() const { return m_type; }
        bool IsNull() const { return m_type == Type::Null; }
        bool IsBool() const { return m_type == Type::Bool; }
        bool IsNumber() const { return m_type == Type::Number; }
        bool IsString() const { return m_type == Type::String; }
        bool IsArray() const { return m_type == Type::Array; }
        bool IsObject() const { return m_type == Type::Object; }

        // --- typed accessors with defaults -------------------------------------------------
        bool AsBool(bool fallback = false) const;
        f64 AsNumber(f64 fallback = 0.0) const;
        f32 AsFloat(f32 fallback = 0.0f) const { return static_cast<f32>(AsNumber(fallback)); }
        i32 AsInt(i32 fallback = 0) const { return static_cast<i32>(AsNumber(static_cast<f64>(fallback))); }
        u32 AsUInt(u32 fallback = 0) const { return static_cast<u32>(AsNumber(static_cast<f64>(fallback))); }
        const std::string& AsString() const;
        std::string AsString(const std::string& fallback) const;

        const Array& AsArray() const;
        Array& AsArray();
        const Object& AsObject() const;
        Object& AsObject();

        // --- object / array navigation ------------------------------------------------------
        bool Has(const std::string& key) const;
        const Json& operator[](const std::string& key) const;
        Json& operator[](const std::string& key);
        const Json& operator[](size_t index) const;
        Json& operator[](size_t index);
        size_t Size() const;
        void Push(Json value);

        /// Slash-separated lookup, e.g. Get("camera/zoom/min"). Returns a null Json if missing.
        const Json& Get(const std::string& path) const;

        // convenience readers that walk a path and coerce
        f32 GetFloat(const std::string& path, f32 fallback = 0.0f) const { return Get(path).AsFloat(fallback); }
        i32 GetInt(const std::string& path, i32 fallback = 0) const { return Get(path).AsInt(fallback); }
        bool GetBool(const std::string& path, bool fallback = false) const { return Get(path).AsBool(fallback); }
        std::string GetString(const std::string& path, const std::string& fallback = "") const
        {
            return Get(path).AsString(fallback);
        }

        // --- serialisation -------------------------------------------------------------------
        std::string Dump(int indent = 2) const;
        static Json Parse(const std::string& text, std::string* error = nullptr);
        static Json LoadFile(const std::string& path, std::string* error = nullptr);
        bool SaveFile(const std::string& path, int indent = 2) const;

    private:
        void DumpTo(std::string& out, int indent, int depth) const;

        Type m_type = Type::Null;
        bool m_bool = false;
        f64 m_number = 0.0;
        std::string m_string;
        Array m_array;
        Object m_object;
    };
}
