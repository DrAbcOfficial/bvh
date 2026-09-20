#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace Bvh {

// std::set with transparent comparator (C++14) so per-entity lookups can use
// a string_view without constructing a temporary std::string. libstdc++ only
// offers transparent hashing for unordered containers in C++20 mode, and the
// configured-classname set is tiny enough that ordered search is at parity.
class CProjectileClassConfig {
public:
    [[nodiscard]] bool Load();
    [[nodiscard]] bool IsManaged(const char* classname) const;
    [[nodiscard]] std::size_t GetClassnameCount() const;
    [[nodiscard]] bool WasDefaultCreated() const;

    [[nodiscard]] static const char* GetPath();

private:
    std::set<std::string, std::less<>> m_classnames;
    bool m_defaultCreated = false;
};

}  // namespace Bvh
