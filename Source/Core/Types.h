// Types.h - fundamental scalar aliases and small helpers shared by every module.
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>

namespace woc
{
    using u8  = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i8  = std::int8_t;
    using i16 = std::int16_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;
    using f32 = float;
    using f64 = double;

    /// Stable handle used instead of raw pointers so that containers may reallocate freely.
    using EntityId = u32;
    inline constexpr EntityId kInvalidId = 0xFFFFFFFFu;

    template <typename T> using Ref = std::shared_ptr<T>;
    template <typename T> using Scope = std::unique_ptr<T>;

    template <typename T, typename... Args>
    Ref<T> MakeRef(Args&&... args) { return std::make_shared<T>(std::forward<Args>(args)...); }

    template <typename T, typename... Args>
    Scope<T> MakeScope(Args&&... args) { return std::make_unique<T>(std::forward<Args>(args)...); }
}
