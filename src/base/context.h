#pragma once

// =============================================================================
// Overview:
// ---------
//
// This file defines macros for determining the compilation context.
// The macros are always defined as either 0 or 1.
//
//     COMPILER_GCC
//     COMPILER_CLANG
//
//     OS_LINUX
//     OS_WINDOWS
//
//     IF_BUILD_DEBUG
//     IF_BUILD_RELEASE
//
// In addition to the above the build script has to provide these:
//
//     BUILD_DEBUG
//     BUILD_RELEASE
//     ASAN_ENABLED
//
// =============================================================================
#if defined(__clang__)
    #define COMPILER_CLANG 1
    #if defined(__gnu_linux__) || defined(__linux__)
        #define OS_LINUX 1
    #else
        #error "Unsupported os."
    #endif
#elif defined(__GNUC__) || defined(__GNUG__)
    #define COMPILER_GCC 1
    #if defined(__gnu_linux__) || defined(__linux__)
        #define OS_LINUX 1
    #else
        #error "Unsupported os."
    #endif
#else
    #error "Unsupported compiler."
#endif

// =============================================================================
// Short form IF_BUILD() macros:
// =============================================================================
#if BUILD_DEBUG
    #define IF_BUILD_DEBUG(...) __VA_ARGS__
    #define IF_BUILD_RELEASE(...)
#elif BUILD_RELEASE
    #define IF_BUILD_RELEASE(...) __VA_ARGS__
    #define IF_BUILD_DEBUG(...)
#endif

// =============================================================================
// Define unset parameters as 0:
// =============================================================================
#if !defined(COMPILER_GCC)
    #define COMPILER_GCC 0
#endif
#if !defined(COMPILER_CLANG)
    #define COMPILER_CLANG 0
#endif
#if !defined(OS_WINDOWS)
    #define OS_WINDOWS 0
#endif
#if !defined(OS_LINUX)
    #define OS_LINUX 0
#endif
