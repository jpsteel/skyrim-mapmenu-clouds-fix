#include "Hooks.h"

#include "RE/T/TESWorldSpace.h"
#include "Utility.h"

namespace {
    using Accept_t = void(RE::MapMenu*, RE::FxDelegateHandler::CallbackProcessor*);

    REL::Relocation<Accept_t> g_accept;

    RE::TESWorldSpace* g_pendingCloudWorld = nullptr;

    bool g_cloudReloadPending = false;
    bool g_mapMenuClosing = false;

    namespace NativeMapClouds {
        using detach_t = void (*)();
        using release_t = void (*)();
        using load_t = void (*)(RE::TESWorldSpace*);
        using configure_t = void (*)(float);

        // Skyrim SE/AE 1.6.1170 addresses
        static REL::Relocation<detach_t> detach{REL::Offset(0x9892B0)};

        static REL::Relocation<release_t> release{REL::Offset(0x989130)};

        static REL::Relocation<load_t> load{REL::Offset(0x9895D0)};

        static REL::Relocation<configure_t> configure{REL::Offset(0x989210)};

        // qword_7FF6461FFA00 on SkyrimSE.exe 1.6.1170
        static REL::Relocation<std::uintptr_t> cloudCacheAddress{REL::Offset(0x31AFA00)};

        bool g_cloudAttached = false;

        void Unload() {
            auto** cache = reinterpret_cast<void**>(cloudCacheAddress.address());

            if (cache && *cache) {
                detach();
            }

            g_cloudAttached = false;

            release();
        }

        bool Reload(RE::TESWorldSpace* a_resolverWorld) {
            if (!a_resolverWorld) {
                logger::warn("[MapClouds] Reload skipped: null worldspace");

                Unload();
                return false;
            }

            Unload();

            auto* effectiveWorld = a_resolverWorld->parentWorld ? a_resolverWorld->parentWorld : a_resolverWorld;

            constexpr RE::FormID kSkyrimWorldspaceID = 0x3C;

            const char* cloudModel = effectiveWorld->GetModel();

            if (effectiveWorld->GetFormID() != kSkyrimWorldspaceID && (!cloudModel || cloudModel[0] == '\0')) {
                logger::info(
                    "[MapClouds] '{}' has no cloud model; leaving map clouds disabled",
                    effectiveWorld->GetName());

                return true;
            }

            load(a_resolverWorld);

            auto** cache = reinterpret_cast<void**>(cloudCacheAddress.address());

            if (!cache || !*cache) {
                logger::warn("[MapClouds] Failed to load cloud model for '{}'", effectiveWorld->GetName());

                g_cloudAttached = false;
                return false;
            }

            configure(0.0f);

            g_cloudAttached = true;

            logger::info("[MapClouds] Loaded clouds for '{}'", effectiveWorld->GetName());

            return true;
        }
    }

    RE::TESWorldSpace* ResolveMapWorld(RE::MapMenu* a_menu, RE::PlayerCharacter* a_player) {
        if (!a_player) {
            return nullptr;
        }

        RE::TESWorldSpace* world = a_player->GetWorldspace();

        if (!world && a_menu) {
            world = a_menu->GetRuntimeData2().worldSpace;
        }

        if (!world) {
            world = a_player->GetPlayerRuntimeData().cachedWorldSpace;
        }

        if (!world) {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();

            if (dataHandler) {
                world = dataHandler->LookupForm<RE::TESWorldSpace>(0x3C, "Skyrim.esm");
            }
        }

        return world;
    }

    void QueueCloudApplication(RE::MapMenu* a_menu, RE::PlayerCharacter* a_player) {
        g_pendingCloudWorld = ResolveMapWorld(a_menu, a_player);

        g_cloudReloadPending = true;

        if (!g_pendingCloudWorld && enableClouds) {
            logger::warn("[MapClouds] No usable map worldspace resolved");
        }
    }

    void ApplyPendingCloudSetting() {
        if (!g_cloudReloadPending) {
            return;
        }

        auto* resolverWorld = g_pendingCloudWorld;

        g_cloudReloadPending = false;
        g_pendingCloudWorld = nullptr;

        if (!enableClouds) {
            NativeMapClouds::Unload();

            logger::info("[MapClouds] Clouds disabled by INI");

            return;
        }

        if (!resolverWorld) {
            NativeMapClouds::Unload();

            logger::warn("[MapClouds] Cloud reload cancelled: no resolver worldspace");

            return;
        }

        NativeMapClouds::Reload(resolverWorld);
    }

    void HookAccept(RE::MapMenu* a_menu, RE::FxDelegateHandler::CallbackProcessor* a_processor) {
        auto* player = RE::PlayerCharacter::GetSingleton();

        if (player && a_menu) {
            const auto* parentCell = player->GetParentCell();

            const bool openedFromInterior = parentCell && parentCell->IsInteriorCell();

            QueueCloudApplication(a_menu, player);

            logger::info("[MapClouds] Cloud operation scheduled; interior={}, enabled={}", openedFromInterior, enableClouds);
        } else {
            g_cloudReloadPending = false;
            g_pendingCloudWorld = nullptr;
        }

        g_accept(a_menu, a_processor);
    }

    struct MapCameraSetRootHook {
        static void thunk(RE::MapCamera* a_camera, RE::NiNode* a_root, const RE::NiPoint3& a_mapPosition) {
            func(a_camera, a_root, a_mapPosition);

            if (g_mapMenuClosing) {
                return;
            }

            ApplyPendingCloudSetting();
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static void Install() {
            REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_MapCamera[0]};

            func = vtable.write_vfunc(0x03, thunk);
        }
    };

    struct MapMenuProcessMessageHook {
        static RE::UI_MESSAGE_RESULTS thunk(RE::MapMenu* a_menu, RE::UIMessage& a_message) {
            const auto messageType = a_message.type.get();

            if (messageType == RE::UI_MESSAGE_TYPE::kHide || messageType == RE::UI_MESSAGE_TYPE::kForceHide) {
                g_mapMenuClosing = true;

                g_cloudReloadPending = false;
                g_pendingCloudWorld = nullptr;

                const auto result = func(a_menu, a_message);

                NativeMapClouds::Unload();

                g_mapMenuClosing = false;

                logger::trace("[MapClouds] Map closed; cloud cache released");

                return result;
            }

            return func(a_menu, a_message);
        }

        static inline REL::Relocation<decltype(thunk)> func;

        static void Install() {
            REL::Relocation<std::uintptr_t> vtable{RE::VTABLE_MapMenu[0]};

            func = vtable.write_vfunc(0x04, thunk);
        }
    };
}

void Reset() {
    g_pendingCloudWorld = nullptr;
    g_cloudReloadPending = false;
    g_mapMenuClosing = false;

    NativeMapClouds::Unload();

    logger::info("[MapClouds] Runtime state reset");
}

void InstallHooks() {
    MapCameraSetRootHook::Install();
    MapMenuProcessMessageHook::Install();

    REL::Relocation<std::uintptr_t> mapMenuVtable{RE::VTABLE_MapMenu[0]};

    g_accept = mapMenuVtable.write_vfunc(0x01, HookAccept);

    logger::info("All hooks installed.");
}