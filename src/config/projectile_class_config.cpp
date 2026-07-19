#include "config/projectile_class_config.h"

#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>

#include <extdll.h>
#include <enginecallback.h>

namespace {

constexpr char kProjectileClassConfigPath[] =
    "addons/metamod/configs/bvh/projectile_classnames.cfg";
constexpr char kDefaultProjectileClassConfig[] =
    "# One entity classname per line. Lines can include # or // comments.\n"
    "# An empty file intentionally disables projectile BVH management.\n"
    "\n"
    "bdsc_bullet_proj\n";

bool ResolveConfigPath(std::filesystem::path& path)
{
    if (g_engfuncs.pfnGetGameDir == nullptr) {
        return false;
    }

    std::array<char, 1024> gameDirectory{};
    g_engfuncs.pfnGetGameDir(gameDirectory.data());
    if (gameDirectory.front() == '\0') {
        return false;
    }

    path = std::filesystem::path(gameDirectory.data()) / kProjectileClassConfigPath;
    return true;
}

bool CreateDefaultConfig(const std::filesystem::path& path)
{
    std::error_code error;
    if (std::filesystem::exists(path, error) || error) {
        return false;
    }

    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file << kDefaultProjectileClassConfig;
    return file.good();
}

std::string_view Trim(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

}  // namespace

namespace Bvh {

bool CProjectileClassConfig::Load()
{
    m_classnames.clear();
    m_defaultCreated = false;

    std::filesystem::path configPath;
    if (!ResolveConfigPath(configPath)) {
        return false;
    }

    std::ifstream file(configPath, std::ios::binary);
    std::string text;
    if (!file.is_open()) {
        if (!CreateDefaultConfig(configPath)) {
            return false;
        }

        text = kDefaultProjectileClassConfig;
        m_defaultCreated = true;
    } else {
        text.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xef &&
        static_cast<unsigned char>(text[1]) == 0xbb &&
        static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }

    std::size_t lineStart = 0;
    while (lineStart < text.size()) {
        const std::size_t lineEnd = text.find_first_of("\r\n", lineStart);
        std::string_view line(text.data() + lineStart,
                              (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);

        const std::size_t commentStart = line.find_first_of("#");
        const std::size_t slashCommentStart = line.find("//");
        if (commentStart != std::string_view::npos &&
            (slashCommentStart == std::string_view::npos || commentStart < slashCommentStart)) {
            line = line.substr(0, commentStart);
        } else if (slashCommentStart != std::string_view::npos) {
            line = line.substr(0, slashCommentStart);
        }

        line = Trim(line);
        if (!line.empty()) {
            m_classnames.emplace(line.data(), line.size());
        }

        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 1;
        if (text[lineEnd] == '\r' && lineStart < text.size() && text[lineStart] == '\n') {
            ++lineStart;
        }
    }

    return true;
}

bool CProjectileClassConfig::IsManaged(const char* classname) const
{
    return classname != nullptr && m_classnames.find(classname) != m_classnames.end();
}

std::size_t CProjectileClassConfig::GetClassnameCount() const
{
    return m_classnames.size();
}

bool CProjectileClassConfig::WasDefaultCreated() const
{
    return m_defaultCreated;
}

const char* CProjectileClassConfig::GetPath()
{
    return kProjectileClassConfigPath;
}

}  // namespace Bvh
