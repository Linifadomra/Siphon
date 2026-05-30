enable_testing()

set(SIPHON_TEST_WORKDIR ${CMAKE_SOURCE_DIR})

add_executable(test_rarc test_rarc.c)
target_link_libraries(test_rarc PRIVATE siphon)
add_test(
    NAME    test_rarc
    COMMAND test_rarc
    WORKING_DIRECTORY ${SIPHON_TEST_WORKDIR}
)

add_executable(test_aes test_aes.c)
target_link_libraries(test_aes PRIVATE siphon)
target_include_directories(test_aes PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME test_aes COMMAND test_aes)

add_executable(test_lzma test_lzma.c)
target_link_libraries(test_lzma PRIVATE siphon)
target_include_directories(test_lzma PRIVATE ${CMAKE_SOURCE_DIR}/src/disc)
add_test(NAME test_lzma COMMAND test_lzma)

add_executable(test_disc_local test_disc_local.c)
target_link_libraries(test_disc_local PRIVATE siphon)
add_test(NAME test_disc_local COMMAND test_disc_local)
set_tests_properties(test_disc_local PROPERTIES SKIP_RETURN_CODE 77)

macro(add_disc_test name)
    add_executable(test_${name} test_${name}.c test_disc_common.c)
    target_link_libraries(test_${name} PRIVATE siphon)
    target_compile_definitions(test_${name} PRIVATE
        SIPHON_TEST_ROOT="${CMAKE_SOURCE_DIR}")
    add_test(NAME test_${name} COMMAND test_${name})
endmacro()

add_disc_test(ciso_gc)
