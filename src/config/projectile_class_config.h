#pragma once

#include <cstddef>
#include <string>
#include <unordered_set>

namespace Bvh {

class CProjectileClassConfig {
public:
    [[nodiscard]] bool Load();
    [[nodiscard]] bool IsManaged(const char* classname) const;
    [[nodiscard]] std::size_t GetClassnameCount() const;
    [[nodiscard]] bool WasDefaultCreated() const;

    [[nodiscard]] static const char* GetPath();

private:
    std::unordered_set<std::string> m_classnames;
    bool m_defaultCreated = false;
};

}  // namespace Bvh
