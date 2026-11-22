#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

using namespace std::literals;

// The build type sets MAGIC_DEBUG_LOGGING (CMakeLists.txt, Debug only). Never define it, or DEBUG, by hand (spec 010).
#ifdef DEBUG
    #error "DEBUG is not the debug logging switch any more: the build type sets MAGIC_DEBUG_LOGGING (spec 010)"
#endif

#ifdef MAGIC_DEBUG_LOGGING
    #define MAGIC_DEBUG_LOG(...) spdlog::info(__VA_ARGS__)
#else
    #define MAGIC_DEBUG_LOG(...) ((void)0)
#endif