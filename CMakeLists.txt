cmake_minimum_required(VERSION 3.16)
project(siphon VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# == Compiler warnings ==
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# == Options ==
option(BUILD_SIPHON_BINARY  "Build the siphon CLI executable"  OFF)
option(BUILD_SHARED_LIBS    "Build siphon as a shared library" OFF)
option(SIPHON_BUILD_TESTS "Build siphon tests"                 OFF)

# == Dependencies ==
set(CONFLUENCE_DOL_YAML OFF CACHE BOOL "" FORCE)

find_package(Confluence REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig)
if(PkgConfig_FOUND)
    pkg_check_modules(ZSTD libzstd)
endif()

if(NOT ZSTD_FOUND)
    find_library(ZSTD_LIBRARIES zstd)
    find_path(ZSTD_INCLUDE_DIRS zstd.h)
    if(ZSTD_LIBRARIES AND ZSTD_INCLUDE_DIRS)
        set(ZSTD_FOUND TRUE)
    endif()
endif()

# == Includes / Building ==

add_library(gc_util STATIC
    src/util/siphon_log.c
)
target_include_directories(gc_util PUBLIC src/util)

add_library(gc_disc STATIC
    src/disc/gc_disc.c
    src/disc/gc_disc_fst.c
    src/disc/gc_iso.c
    src/disc/gc_ciso.c
    src/disc/gc_gcz.c
    src/disc/gc_wia.c
    src/disc/gc_wbfs.c
    src/disc/gc_rvz.c
)

target_include_directories(gc_disc PUBLIC include PRIVATE src/disc)
target_link_libraries(gc_disc PRIVATE ZLIB::ZLIB PUBLIC gc_util Confluence::confluence)

if(ZSTD_FOUND)
    target_include_directories(gc_disc PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_directories(gc_disc PUBLIC ${ZSTD_LIBRARY_DIRS})
    target_link_libraries(gc_disc PUBLIC ${ZSTD_LIBRARIES})
endif()

add_library(gc_dol STATIC
    src/dol/gc_symbols.c
    src/dol/gc_json.c
)
target_include_directories(gc_dol PUBLIC include src/dol)
target_link_libraries(gc_dol PUBLIC Confluence::confluence gc_disc)

if(BUILD_SIPHON_BINARY)
    add_executable(siphon cmd/cli.c src/siphon.c)
    target_link_libraries(siphon PRIVATE gc_dol)
else()
    add_library(siphon src/siphon.c)
    target_include_directories(siphon PUBLIC include)
    target_link_libraries(siphon PUBLIC gc_dol)
    set_target_properties(siphon PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

if(SIPHON_BUILD_TESTS)
    enable_testing()

    add_executable(test_rarc test/test_rarc.c)
    target_link_libraries(test_rarc PRIVATE gc_disc)
    add_test(
        NAME test_rarc
        COMMAND test_rarc
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
