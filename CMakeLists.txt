cmake_minimum_required(VERSION 3.13)
project(proteus_clone LANGUAGES CXX)

# ----- Toolchain requirements -------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Point CMake at the MSYS2 mingw64 prefix if it cannot find SDL2 on its own.
# (Safe to override from the cache if already correct.)
if(NOT DEFINED CMAKE_PREFIX_PATH)
    set(CMAKE_PREFIX_PATH "C:/msys64/mingw64" CACHE PATH "MSYS2 mingw64 prefix")
endif()

# Make sure the mingw runtime DLLs are found at link time too.
link_directories(${CMAKE_PREFIX_PATH}/bin ${CMAKE_PREFIX_PATH}/lib)

# ----- Find SDL2 + SDL_ttf ----------------------------------------------------
find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

# ----- Executable -------------------------------------------------------------
# NOTE: We deliberately do NOT use the WIN32 subsystem / SDL2main here.
# MSYS2's static libSDL2main.a has a circular dependency on SDL2 symbols that
# breaks under MinGW link-ordering. A plain console executable with a normal
# main() avoids the whole problem. (A console window opens behind the GUI,
# which is fine during development.)
add_executable(${PROJECT_NAME} main.cpp)

# Link the SHARED SDL2 library only (skip SDL2main entirely).
if(TARGET SDL2::SDL2)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2)
else()
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2)
endif()

if(TARGET SDL2_ttf::SDL2_ttf)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2_ttf::SDL2_ttf)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2_ttf)
    target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_TTF_INCLUDE_DIRS})
endif()

# ----- Copy runtime DLLs next to the .exe ------------------------------------
# Complete transitive dependency set (resolved via ldd at setup time):
#   SDL2 → (none)
#   SDL2_ttf → freetype, harfbuzz
#   freetype → libpng, zlib, bz2, brotli
#   harfbuzz → glib, graphite2
#   glib → pcre2, intl, iconv, winpthread
# Plus the mingw runtime (gcc/stdc++/winpthread).
set(_MINGW_BIN "${CMAKE_PREFIX_PATH}/bin")
set(_RUNTIME_DLLS
    SDL2.dll
    SDL2_ttf.dll
    libfreetype-6.dll
    libharfbuzz-0.dll
    libpng16-16.dll
    zlib1.dll
    libbz2-1.dll
    libbrotlidec.dll
    libbrotlicommon.dll
    libglib-2.0-0.dll
    libgraphite2.dll
    libpcre2-8-0.dll
    libintl-8.dll
    libiconv-2.dll
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
)

foreach(_dll ${_RUNTIME_DLLS})
    set(_src "${_MINGW_BIN}/${_dll}")
    if(EXISTS "${_src}")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_src}" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${_dll}"
            COMMENT "Copying ${_dll}")
    else()
        message(WARNING "Runtime DLL not found: ${_src}")
    endif()
endforeach()
