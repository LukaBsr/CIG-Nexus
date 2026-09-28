cmake_minimum_required(VERSION 3.20)

project(cig-nexus-tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp
)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp
)

list(FILTER SERVER_SOURCES EXCLUDE REGEX ".*/main\\.cpp$")

add_executable(cig-nexus-tests
    ${TEST_SOURCES}
    ${SERVER_SOURCES}
)

target_include_directories(cig-nexus-tests
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

# Frozen artifacts produced by other components (e.g. tests/fixtures/jwt: a
# token from the real web signer) — read at runtime by absolute path.
target_compile_definitions(cig-nexus-tests
    PRIVATE
        CIG_NEXUS_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

target_link_libraries(cig-nexus-tests
    PRIVATE
        Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        CURL::libcurl
)

include(CTest)
enable_testing()

add_test(
    NAME cig-nexus-tests
    COMMAND $<TARGET_FILE:cig-nexus-tests>
)
