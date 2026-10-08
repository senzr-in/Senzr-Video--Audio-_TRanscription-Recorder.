#pragma once

/**
 * @file export.hpp
 * @brief Public symbol visibility macros for libsenzr (Senzr Edge OS SDK).
 *
 * Defines SENZR_API and SENZR_LOCAL macros to support hidden symbol visibility
 * (-fvisibility=hidden) and dynamic/shared library exports.
 */

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(SENZR_STATIC_DEFINE)
        #define SENZR_API
        #define SENZR_LOCAL
    #elif defined(SENZR_EXPORTS)
        #define SENZR_API __declspec(dllexport)
        #define SENZR_LOCAL
    #else
        #define SENZR_API __declspec(dllimport)
        #define SENZR_LOCAL
    #endif
#else
    #if defined(SENZR_EXPORTS)
        #define SENZR_API __attribute__((visibility("default")))
        #define SENZR_LOCAL __attribute__((visibility("hidden")))
    #else
        #define SENZR_API
        #define SENZR_LOCAL
    #endif
#endif
