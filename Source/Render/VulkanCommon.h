// VulkanCommon.h - shared Vulkan includes and error handling.
//
// The renderer loads Vulkan dynamically through volk, so the project builds and links
// without the Vulkan SDK installed; only a driver-provided vulkan-1.dll is needed at run time.
#pragma once

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif

#include <volk.h>

#include "../Core/Types.h"
#include "../Core/Log.h"

#include <stdexcept>

namespace woc
{
    const char* VulkanResultToString(VkResult result);

    /// Throws on failure: Vulkan errors here are unrecoverable set-up faults, and the
    /// application layer turns the exception into a message box plus a clean shutdown.
    inline void VulkanCheck(VkResult result, const char* expression, const char* file, int line)
    {
        if (result == VK_SUCCESS) return;
        const std::string message =
            std::string("Vulkan call failed: ") + expression + " -> " + VulkanResultToString(result) +
            " (" + file + ":" + std::to_string(line) + ")";
        WOC_LOG_ERROR(message);
        throw std::runtime_error(message);
    }
}

#define VK_CHECK(expr) ::woc::VulkanCheck((expr), #expr, __FILE__, __LINE__)
