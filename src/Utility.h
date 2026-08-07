#ifndef UTILITY_H
#define UTILITY_H

#include <SimpleIni.h>

namespace logger = SKSE::log;

extern bool enableClouds;

inline const std::string INI_FILE_PATH = "Data/Map Menu Clouds Fix.ini";

void SetupLog();
void LoadDataFromINI();

#endif  // UTILITY_H