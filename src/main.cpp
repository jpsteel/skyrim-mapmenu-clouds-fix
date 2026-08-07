#include <spdlog/sinks/basic_file_sink.h>

#include "Utility.h"
#include "Hooks.h"

extern "C" [[maybe_unused]] __declspec(dllexport) bool SKSEPlugin_Load(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    SetupLog();
    spdlog::set_level(spdlog::level::info);

    LoadDataFromINI();
    InstallHooks();
    logger::info("Successfully loaded MapMenuCloudsFix.dll!");
    return true;
}