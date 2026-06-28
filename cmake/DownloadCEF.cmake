# Downloads the CEF binary distribution for <platform>/<version> into
# <download_dir> and sets CEF_ROOT (global) to the extracted location.
# Vendored from chromiumembedded/cef-project (BSD). Versions/platforms at:
#   https://cef-builds.spotifycdn.com/index.html
function(DownloadCEF platform channel version download_dir)
    if(channel STREQUAL "beta")
        set(channel_part "_beta")
    else()
        set(channel_part "")
    endif()

    set(CEF_DISTRIBUTION "cef_binary_${version}_${platform}${channel_part}")
    set(CEF_ROOT "${download_dir}/${CEF_DISTRIBUTION}" CACHE INTERNAL "CEF_ROOT")

    if(NOT IS_DIRECTORY "${CEF_ROOT}")
        set(dl "${download_dir}/${CEF_DISTRIBUTION}.tar.bz2")
        if(NOT EXISTS "${dl}")
            set(url "https://cef-builds.spotifycdn.com/${CEF_DISTRIBUTION}.tar.bz2")
            string(REPLACE "+" "%2B" url "${url}")
            message(STATUS "Downloading ${url}.sha1 ...")
            file(DOWNLOAD "${url}.sha1" "${dl}.sha1")
            file(READ "${dl}.sha1" CEF_SHA1)
            message(STATUS "Downloading CEF (~1GB) ...")
            file(DOWNLOAD "${url}" "${dl}" EXPECTED_HASH SHA1=${CEF_SHA1} SHOW_PROGRESS)
        endif()
        message(STATUS "Extracting ${dl} ...")
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${dl}"
                        WORKING_DIRECTORY "${download_dir}")
        file(REMOVE "${dl}" "${dl}.sha1")
    endif()
endfunction()
