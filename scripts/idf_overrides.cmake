# Global IDF overrides and fixes

# cmake release flags sets -O3 which is not stable
set(CMAKE_C_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_C_FLAGS_DEBUG "" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_DEBUG "" CACHE STRING "" FORCE)

function(message)
    if("${ARGV}" STREQUAL "")
        _message("")
        return()
    endif()
    string(JOIN " " _msg_text ${ARGV})
    if(ARGV0 STREQUAL "WARNING" AND _msg_text MATCHES "belongs to component (esp_wifi|wpa_supplicant)")
        return()
    endif()

    _message(${ARGV})
endfunction()