#include "engine/model_provider.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>

#include <extdll.h>
#include <meta_api.h>
#include <com_model.h>

#include "runtime/debug_log.h"

namespace {

#ifdef _WIN32
// Sven Co-op's sv.models reference used by the FallGuys Bullet integration.
constexpr char kSvModelsSignature[] =
    "\x3D\xFE\x1F\x00\x00\x2A\x2A\x0F\xAE\xE8\xFF\x34\x8D";
constexpr std::ptrdiff_t kSvModelsOffset = 13;
#else
constexpr char kGotPltPrologSignature[] =
    "\x53\x83\xEC\x18\x8B\x44\x24\x20\xE8\x2A\x2A\x2A\x2A\x81\xC3\x2A\x2A\x2A\x2A";
constexpr char kSvModelReferenceSignature[] =
    "\x8B\x84\x82\x2A\x2A\x2A\x00\x89\x04\x24\xE8";
constexpr std::ptrdiff_t kSvModelsOffset = 0x276148;

bool IsInRange(const char* address, const char* begin, const char* end)
{
    return address >= begin && address < end;
}

bool ReadPicMemoryDisplacement(const std::uint8_t* instruction, std::int32_t& displacement)
{
    size_t offset = 0;
    while (instruction[offset] == 0x66 || instruction[offset] == 0x67 ||
           instruction[offset] == 0xF0 || instruction[offset] == 0xF2 ||
           instruction[offset] == 0xF3) {
        ++offset;
    }

    if (instruction[offset] != 0x8B && instruction[offset] != 0x8D) {
        return false;
    }

    const std::uint8_t modrm = instruction[++offset];
    if ((modrm >> 6) != 2) {
        return false;
    }

    if ((modrm & 7) == 4) {
        ++offset;  // SIB byte
    }

    std::memcpy(&displacement, instruction + ++offset, sizeof(displacement));
    return true;
}
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

    DebugLog(1, "Resolved Windows sv.models at %p.", m_precachedModels);
    return true;
#else
    if (gpMetaUtilFuncs == nullptr || gpMetaUtilFuncs->pfnGetEngineHandle == nullptr ||
        gpMetaUtilFuncs->pfnGetProcAddress == nullptr) {
        LOG_ERROR(PLID, "MetaMod does not provide the Linux engine symbol API.");
        return false;
    }

    void* serverState = nullptr;
    const DLHANDLE engineHandle = gpMetaUtilFuncs->pfnGetEngineHandle();
    if (engineHandle != nullptr) {
        serverState = gpMetaUtilFuncs->pfnGetProcAddress(engineHandle, "sv");
    }

#if defined(__i386__)
    if (serverState == nullptr && gpMetaUtilFuncs->pfnGetEngineBase != nullptr &&
        gpMetaUtilFuncs->pfnGetImageSize != nullptr &&
        gpMetaUtilFuncs->pfnGetCodeBase != nullptr &&
        gpMetaUtilFuncs->pfnGetCodeSize != nullptr &&
        gpMetaUtilFuncs->pfnSearchPattern != nullptr) {
        char* imageBase = static_cast<char*>(gpMetaUtilFuncs->pfnGetEngineBase());
        const size_t imageSize = imageBase == nullptr ? 0 :
            gpMetaUtilFuncs->pfnGetImageSize(imageBase);
        char* imageEnd = imageBase == nullptr ? nullptr : imageBase + imageSize;
        char* codeBase = imageBase == nullptr ? nullptr :
            static_cast<char*>(gpMetaUtilFuncs->pfnGetCodeBase(imageBase));
        const size_t codeSize = imageBase == nullptr ? 0 :
            gpMetaUtilFuncs->pfnGetCodeSize(imageBase);

        char* gotPltProlog = codeBase == nullptr ? nullptr :
            static_cast<char*>(gpMetaUtilFuncs->pfnSearchPattern(
                codeBase, codeSize, kGotPltPrologSignature,
                sizeof(kGotPltPrologSignature) - 1));
        char* svModelReference = codeBase == nullptr ? nullptr :
            static_cast<char*>(gpMetaUtilFuncs->pfnSearchPattern(
                codeBase, codeSize, kSvModelReferenceSignature,
                sizeof(kSvModelReferenceSignature) - 1));

        if (gotPltProlog != nullptr && svModelReference != nullptr &&
            IsInRange(gotPltProlog + 19, codeBase, codeBase + codeSize) &&
            IsInRange(svModelReference - 7, codeBase, codeBase + codeSize)) {
            std::int32_t gotPltDisplacement = 0;
            std::memcpy(&gotPltDisplacement, gotPltProlog + 15,
                        sizeof(gotPltDisplacement));
            char* gotPlt = gotPltProlog + 13 + gotPltDisplacement;

            for (const int backtrack : {5, 6, 7}) {
                std::int32_t memoryDisplacement = 0;
                const auto* instruction = reinterpret_cast<const std::uint8_t*>(
                    svModelReference - backtrack);
                if (!ReadPicMemoryDisplacement(instruction, memoryDisplacement)) {
                    continue;
                }

                char* candidate = gotPlt + memoryDisplacement;
                if (imageEnd != nullptr && IsInRange(candidate, imageBase, imageEnd)) {
                    serverState = candidate;
                    break;
                }
            }
        }
    }
#endif

    if (serverState == nullptr) {
        LOG_ERROR(PLID, "Unable to locate sv on this Linux engine build.");
        return false;
    }

    m_precachedModels = *reinterpret_cast<model_t* (**)[8192]>(
        static_cast<char*>(serverState) + kSvModelsOffset);
    if (m_precachedModels == nullptr) {
        LOG_ERROR(PLID, "The Linux sv.models address resolved to null.");
        return false;
    }

    DebugLog(1, "Resolved Linux sv.models at %p.", m_precachedModels);
    return true;
#endif
}

}  // namespace Bvh
