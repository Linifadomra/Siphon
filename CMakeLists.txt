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
option(BUILD_TESTING        "Build tests"                      OFF)

# == Dependencies ==
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

find_package(yaml QUIET)
if(NOT yaml_FOUND)
    find_package(PkgConfig QUIET)

    if(PkgConfig_FOUND)
        pkg_check_modules(YAML IMPORTED_TARGET yaml-0.1)
    endif()

    if(NOT YAML_FOUND)
        find_library(YAML_LIBRARY  yaml)
        find_path   (YAML_INCLUDE  yaml.h)
        if(YAML_LIBRARY AND YAML_INCLUDE)
            add_library(yaml UNKNOWN IMPORTED)
            set_target_properties(yaml PROPERTIES
                IMPORTED_LOCATION             "${YAML_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${YAML_INCLUDE}")
        else()
            message(FATAL_ERROR
                "libyaml not found. Install it with your package manager"
                "or point YAML_LIBRARY and YAML_INCLUDE at your custom installation.")
        endif()
    endif()
endif()

if(TARGET PkgConfig::YAML)
    add_library(yaml_iface INTERFACE IMPORTED)
    target_link_libraries(yaml_iface INTERFACE PkgConfig::YAML)
    set(YAML_TARGET yaml_iface)
elseif(TARGET yaml)
    set(YAML_TARGET yaml)
else()
    message(FATAL_ERROR "Could not resolve a libyaml target.")
endif()

# == Includes / Building ==

add_library(gc_disc STATIC
    src/disc/gc_disc.c
    src/disc/gc_disc_fst.c
    src/disc/gc_iso.c
    src/disc/gc_ciso.c
    src/disc/gc_gcz.c
    src/disc/gc_wia.c
    src/disc/gc_wbfs.c
    src/disc/gc_rarc.c
    src/disc/gc_rvz.c
)

target_include_directories(gc_disc PUBLIC include PRIVATE src/disc)
target_link_libraries(gc_disc PRIVATE ZLIB::ZLIB)

if(ZSTD_FOUND)
    target_include_directories(gc_disc PRIVATE ${ZSTD_INCLUDE_DIRS})
    target_link_directories(gc_disc PUBLIC ${ZSTD_LIBRARY_DIRS})
    target_link_libraries(gc_disc PUBLIC ${ZSTD_LIBRARIES})
endif()

add_library(gc_yaml STATIC
    src/yaml/gc_yaml.c
)
target_include_directories(gc_yaml PUBLIC src/yaml)
target_link_libraries(gc_yaml PUBLIC yaml)

add_library(gc_dol STATIC
    src/dol/gc_symbols.c
    src/dol/gc_json.c
    src/dol/gc_yaz0.c
    src/dol/gc_dol.c
)
target_include_directories(gc_dol PUBLIC include src/dol)
target_link_libraries(gc_dol PUBLIC gc_yaml gc_disc)

if(BUILD_SIPHON_BINARY)
    add_executable(siphon cmd/cli.c src/siphon.c)
    target_link_libraries(siphon PRIVATE gc_dol)
else()
    add_library(siphon src/siphon.c)
    target_link_libraries(siphon PUBLIC gc_dol)
    set_target_properties(siphon PROPERTIES
    VERSION   ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

if(BUILD_TESTING)
    enable_testing()

    add_executable(test_rarc test/test_rarc.c)
    target_link_libraries(test_rarc PRIVATE gc_disc)
    add_test(
        NAME test_rarc 
        COMMAND test_rarc
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )

    add_executable(test_yaml test/test_yaml.c)
    target_link_libraries(test_yaml PRIVATE gc_yaml)
    add_test(
        NAME test_yaml 
        COMMAND test_yaml
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()