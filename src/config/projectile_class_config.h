#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace Bvh {

// Per-classname options parsed from the config file. Unknown tokens are
// ignored so future flags stay forward compatible.
struct CClassFlags {
    bool trustThink = false;
};

// std::map with transparent comparator (C++14) so per-entity lookups can use
// a string_view without constructing a temporary std::string. libstdc++ only
// offers transparent hashing for unordered containers in C++20 mode, and the
// configured-classname set is tiny enough that ordered search is at parity.
class CProjectileClassConfig {
public:
    [[nodiscard]] bool Load();
    [[nodiscard]] const CClassFlags* FindFlags(const char* classname) const;
    [[nodiscard]] bool IsManaged(const char* classname) const;
    [[nodiscard]] std::size_t GetClassnameCount() const;
    [[nodiscard]] bool WasDefaultCreated() const;

    [[nodiscard]] static const char* GetPath();

private:
    std::map<std::string, CClassFlags, std::less<>> m_classnames;
    bool m_defaultCreated = false;
};

}  // namespace Bvh
