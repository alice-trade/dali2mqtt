# Global overrides and fixes

function(message)
    string(JOIN " " _msg_text ${ARGV})
    if(ARGV0 STREQUAL "WARNING" AND _msg_text MATCHES "belongs to component (esp_wifi|wpa_supplicant)")
        return()
    endif()
    _message(${ARGV})
endfunction()