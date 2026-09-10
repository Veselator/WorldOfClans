// Paths.h - resolves asset locations relative to the executable or the source tree.
#pragma once

#include "Singleton.h"
#include "Types.h"

namespace woc
{
    /// Locates the data root once at startup by walking upwards from the executable
    /// until a directory containing "Config/game.json" is found. Running from the
    /// Visual Studio output folder and from the project root therefore both work.
    class Paths final : public Singleton<Paths>
    {
        friend class Singleton<Paths>;
    public:
        void Initialise();

        const std::string& Root() const { return m_root; }
        std::string Config(const std::string& file) const { return m_root + "/Config/" + file; }
        std::string Shader(const std::string& file) const { return m_root + "/Shaders/" + file; }
        std::string Sprite(const std::string& file) const { return m_root + "/Sprites/" + file; }
        std::string Map(const std::string& mapName, const std::string& file) const
        {
            return m_root + "/Maps/" + mapName + "/" + file;
        }
        std::string MapsDirectory() const { return m_root + "/Maps"; }
        std::string SavesDirectory() const { return m_root + "/Saves"; }

        static bool FileExists(const std::string& path);
        static bool DirectoryExists(const std::string& path);
        static bool EnsureDirectory(const std::string& path);
        static bool RemoveFile(const std::string& path);
        static std::vector<std::string> ListDirectories(const std::string& path);
        static std::vector<std::string> ListFiles(const std::string& path, const std::string& extension);

    private:
        Paths() = default;
        ~Paths() = default;

        std::string m_root = ".";
    };
}
