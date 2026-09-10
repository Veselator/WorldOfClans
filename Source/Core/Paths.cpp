#include "Paths.h"
#include "Log.h"

#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace woc
{
    namespace
    {
        bool LooksLikeRoot(const fs::path& p)
        {
            std::error_code ec;
            return fs::exists(p / "Config" / "game.json", ec);
        }

        /// Walks up to `levels` parents looking for the data root marker.
        bool SearchUpwards(fs::path start, std::string& out, int levels = 8)
        {
            for (int i = 0; i < levels; ++i)
            {
                if (LooksLikeRoot(start)) { out = start.string(); return true; }
                if (!start.has_parent_path() || start.parent_path() == start) break;
                start = start.parent_path();
            }
            return false;
        }

        fs::path ExecutableDirectory()
        {
#ifdef _WIN32
            wchar_t buffer[MAX_PATH]{};
            const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
            if (length > 0) return fs::path(std::wstring(buffer, length)).parent_path();
#endif
            std::error_code ec;
            return fs::current_path(ec);
        }
    }

    void Paths::Initialise()
    {
        std::error_code ec;
        const fs::path workingDir = fs::current_path(ec);

        // The working directory differs between "run from Visual Studio" (project dir)
        // and "double-click the exe" (output dir), so try both anchors.
        if (!SearchUpwards(ExecutableDirectory(), m_root) && !SearchUpwards(workingDir, m_root))
        {
            m_root = workingDir.string();
        }

        // Normalise to forward slashes so logs and json paths stay readable.
        for (char& c : m_root) if (c == '\\') c = '/';
        WOC_LOG_INFO("Data root resolved to ", m_root);
    }

    bool Paths::FileExists(const std::string& path)
    {
        std::error_code ec;
        return fs::is_regular_file(path, ec);
    }

    bool Paths::DirectoryExists(const std::string& path)
    {
        std::error_code ec;
        return fs::is_directory(path, ec);
    }

    bool Paths::EnsureDirectory(const std::string& path)
    {
        std::error_code ec;
        if (fs::is_directory(path, ec)) return true;
        return fs::create_directories(path, ec);
    }

    bool Paths::RemoveFile(const std::string& path)
    {
        std::error_code ec;
        return fs::remove(path, ec);
    }

    std::vector<std::string> Paths::ListDirectories(const std::string& path)
    {
        std::vector<std::string> result;
        std::error_code ec;
        if (!fs::is_directory(path, ec)) return result;
        for (const auto& entry : fs::directory_iterator(path, ec))
        {
            if (entry.is_directory()) result.push_back(entry.path().filename().string());
        }
        return result;
    }

    std::vector<std::string> Paths::ListFiles(const std::string& path, const std::string& extension)
    {
        std::vector<std::string> result;
        std::error_code ec;
        if (!fs::is_directory(path, ec)) return result;
        for (const auto& entry : fs::directory_iterator(path, ec))
        {
            if (!entry.is_regular_file()) continue;
            if (!extension.empty() && entry.path().extension().string() != extension) continue;
            result.push_back(entry.path().filename().string());
        }
        return result;
    }
}
