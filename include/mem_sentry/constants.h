#pragma once

namespace MEM_SENTRY::constants {
    /// @brief signature value for valid active memory
    constexpr int MEMSYSTEM_SIGNATURE = 0xDEADC0DE;

    /// @brief signature value for memory that has already been freed (Dead Land)
    /// Used to detect Double Free errors.
    constexpr int MEMSYSTEM_FREED_SIGNATURE = 0xFEDC0DE;

    /// @brief endmarker to make sure we don't free beyond the array.
    constexpr int MEMSYSTEM_ENDMARKER = 0XEEDC0DE;



    /*------------- MEM SENTRY CONFIG -----------------*/

    /// @breif check if use defined MEM_SENTRY_ENABLE already.
    #ifndef MEM_SENTRY_ENABLE
        // default: enable only in debug mode 
        #ifdef _DEBUG
            #define MEM_SENTRY_ENABLE 1
        #else 
            #define MEM_SENTRY_ENABLE 0
        #endif
    #endif

    constexpr size_t CACHE_LINE_SIZE = std::hardware_destructive_interference_size;
};

