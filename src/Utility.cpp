#include "Utility.h"

#include <spdlog/sinks/basic_file_sink.h>

bool enableClouds = true;

void SetupLog() {
    auto logsFolder = SKSE::log::log_directory();

    if (!logsFolder) {
        SKSE::stl::report_and_fail("SKSE log_directory not provided");
    }

    const auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();

    const auto logFilePath = *logsFolder / std::format("{}.log", pluginName);

    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);

    auto loggerInstance = std::make_shared<spdlog::logger>("log", std::move(fileSink));

    spdlog::set_default_logger(std::move(loggerInstance));

    spdlog::set_level(spdlog::level::trace);

    spdlog::flush_on(spdlog::level::trace);
}

void LoadDataFromINI() {
    CSimpleIniA ini;
    ini.SetUnicode();

    const auto result = ini.LoadFile(INI_FILE_PATH.c_str());

    if (result < 0) {
        logger::warn("Could not load '{}'; using defaults", INI_FILE_PATH);
    }

    enableClouds = ini.GetBoolValue("MapMenu", "bEnableMapClouds", true);

    logger::info("Settings: bEnableMapClouds={}", enableClouds);
}