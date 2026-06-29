cmake_minimum_required(VERSION 3.16)
file(STRINGS include/hid-parser/hid-parser.h HIDP_VERSION_LINES REGEX "^#define HIDP_VERSION_(MAJOR|MINOR|PATCH) ")
string(REGEX REPLACE ".*MAJOR ([0-9]+).*MINOR ([0-9]+).*PATCH ([0-9]+).*" "\\1.\\2.\\3" HIDP_HEADER_VERSION "${HIDP_VERSION_LINES}")
project(hid-parser VERSION ${HIDP_HEADER_VERSION} DESCRIPTION "Parse HID report descriptors and read or build HID reports" LANGUAGES C)

option(HID_PARSER_BUILD_TESTS "Build tests" OFF)
option(HID_PARSER_BUILD_EXAMPLES "Build examples" OFF)
option(HID_PARSER_BUILD_FUZZ "Build libFuzzer targets" OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(hid-parser src/tokenize.c src/parse.c src/desc.c src/names.c src/usage_names.c src/classify.c)
add_library(hid-parser::hid-parser ALIAS hid-parser)
target_include_directories(hid-parser PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
set_target_properties(hid-parser PROPERTIES
    C_STANDARD 99
    C_VISIBILITY_PRESET hidden
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR})

set(PC_CFLAGS "")
if(BUILD_SHARED_LIBS)
    target_compile_definitions(hid-parser PRIVATE HIDP_BUILD_SHARED INTERFACE HIDP_SHARED)
    set(PC_CFLAGS "-DHIDP_SHARED")
endif()

install(TARGETS hid-parser EXPORT hid-parserTargets)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT hid-parserTargets
    NAMESPACE hid-parser::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hid-parser)

configure_package_config_file(cmake/hid-parserConfig.cmake.in
    ${PROJECT_BINARY_DIR}/hid-parserConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hid-parser)
write_basic_package_version_file(${PROJECT_BINARY_DIR}/hid-parserConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${PROJECT_BINARY_DIR}/hid-parserConfig.cmake
    ${PROJECT_BINARY_DIR}/hid-parserConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/hid-parser)

configure_file(hid-parser.pc.in ${PROJECT_BINARY_DIR}/hid-parser.pc @ONLY)
install(FILES ${PROJECT_BINARY_DIR}/hid-parser.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

if(HID_PARSER_BUILD_TESTS)
    enable_testing()
    add_executable(test_hid-parser tests/test_hid-parser.c)
    target_link_libraries(test_hid-parser PRIVATE hid-parser)
    add_test(NAME hid-parser COMMAND test_hid-parser)
    add_executable(test_usage_names tests/test_usage_names.c)
    target_link_libraries(test_usage_names PRIVATE hid-parser)
    add_test(NAME usage_names COMMAND test_usage_names)
    add_executable(hiddump_test examples/hiddump.c)
    target_link_libraries(hiddump_test PRIVATE hid-parser)
    add_test(NAME corpus COMMAND ${CMAKE_COMMAND} -DDUMP=$<TARGET_FILE:hiddump_test>
        -DCORPUS=${PROJECT_SOURCE_DIR}/tests/corpus -P ${PROJECT_SOURCE_DIR}/tests/corpus/check.cmake)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME kernel_oracle COMMAND sh ${PROJECT_SOURCE_DIR}/tests/kernel-oracle/run.sh
            $<TARGET_FILE:hiddump_test>)
    endif()
    add_library(hid-parser_header_check OBJECT tests/header_check.c)
    target_include_directories(hid-parser_header_check PRIVATE include)
endif()

if(HID_PARSER_BUILD_EXAMPLES)
    add_executable(hiddump examples/hiddump.c)
    target_link_libraries(hiddump PRIVATE hid-parser)
endif()

if(HID_PARSER_BUILD_FUZZ)
    add_executable(fuzz_parse fuzz/fuzz_parse.c)
    target_link_libraries(fuzz_parse PRIVATE hid-parser)
    target_link_options(fuzz_parse PRIVATE -fsanitize=fuzzer)
endif()
