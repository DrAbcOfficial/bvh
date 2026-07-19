#include "engine/model_provider.h"

#include <cstddef>

#include <extdll.h>
#include <meta_api.h>
#include <com_model.h>

namespace {

#ifdef _WIN32
// Sven Co-op's sv.models reference used by the FallGuys Bullet integration.
constexpr char kSvModelsSignature[] =
    "\x3D\xFE\x1F\x00\x00\x2A\x2A\x0F\xAE\xE8\xFF\x34\x8D";
constexpr std::ptrdiff_t kSvModelsOffset = 13;
#endif

}  // namespace

namespace Bvh {

model_t* CModelProvider::GetModel(const edict_t* entity)
{
    if (entity == nullptr || !ResolvePrecachedModels()) {
        return nullptr;
    }

    const int modelIndex = entity->v.modelindex;
    if (modelIndex < 0 || modelIndex >= 8192) {
        LOG_ERROR(PLID, "Invalid world model index %d.", modelIndex);
        return nullptr;
    }

    return (*m_precachedModels)[modelIndex];
}

bool CModelProvider::ResolvePrecachedModels()
{
    if (m_precachedModels != nullptr) {
        return true;
    }
    if (m_resolutionAttempted) {
        return false;
    }
    m_resolutionAttempted = true;

#ifdef _WIN32
    if (gpMetaUtilFuncs == nullptr || gpMetaUtilFuncs->pfnGetEngineBase == nullptr ||
        gpMetaUtilFuncs->pfnGetCodeBase == nullptr ||
        gpMetaUtilFuncs->pfnGetCodeSize == nullptr ||
        gpMetaUtilFuncs->pfnSearchPattern == nullptr) {
        LOG_ERROR(PLID, "MetaMod does not provide the engine image search API.");
        return false;
    }

    void* engineBase = gpMetaUtilFuncs->pfnGetEngineBase();
    if (engineBase == nullptr) {
        LOG_ERROR(PLID, "Engine image base is unavailable.");
        return false;
    }

    void* engineCodeBase = gpMetaUtilFuncs->pfnGetCodeBase(engineBase);
    const size_t engineCodeSize = gpMetaUtilFuncs->pfnGetCodeSize(engineBase);
    if (engineCodeBase == nullptr || engineCodeSize == 0) {
        LOG_ERROR(PLID, "Engine code range is unavailable.");
        return false;
    }

    auto* match = static_cast<char*>(gpMetaUtilFuncs->pfnSearchPattern(
        engineCodeBase, engineCodeSize, kSvModelsSignature,
        sizeof(kSvModelsSignature) - 1));
    if (match == nullptr) {
        LOG_ERROR(PLID, "Unable to locate sv.models for this Windows engine build.");
        return false;
    }

    m_precachedModels = *reinterpret_cast<model_t* (**)[8192]>(match + kSvModelsOffset);
    if (m_precachedModels == nullptr) {
        LOG_ERROR(PLID, "The sv.models signature resolved to a null address.");
        return false;
    }

    LOG_MESSAGE(PLID, "Resolved sv.models at %p.", m_precachedModels);
    return true;
#else
    LOG_ERROR(PLID, "World BSP extraction is currently supported only on Windows.");
    return false;
#endif
}

}  // namespace Bvh
