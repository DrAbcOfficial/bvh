#pragma once

struct edict_s;
typedef struct edict_s edict_t;

struct model_s;
typedef struct model_s model_t;

namespace Bvh {

class CModelProvider {
public:
    [[nodiscard]] model_t* GetModel(const edict_t* entity);

private:
    [[nodiscard]] bool ResolvePrecachedModels();

    model_t* (*m_precachedModels)[8192] = nullptr;
    bool m_resolutionAttempted = false;
};

}  // namespace Bvh
