#pragma once

/**
 * @file export.hpp
 * @brief Cross-platform symbol visibility and export macro definitions for Senzr OS SDK.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(SENZR_BUILD_SHARED)
        #if defined(SENZR_EXPORTS)
            #define SENZR_API __declspec(dllexport)
        #else
            #define SENZR_API __declspec(dllimport)
        #endif
    #else
        #define SENZR_API
    #endif
    #define SENZR_LOCAL
#else
    #if defined(SENZR_BUILD_SHARED)
        #if defined(SENZR_EXPORTS)
            #define SENZR_API __attribute__((visibility("default")))
        #else
            #define SENZR_API __attribute__((visibility("default")))
        #endif
        #define SENZR_LOCAL __attribute__((visibility("hidden")))
    #else
        #define SENZR_API
        #define SENZR_LOCAL
    #endif
#endif
