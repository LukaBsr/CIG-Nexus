cmake_minimum_required(VERSION 3.20)

project(cig-nexus-server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

option(BUILD_TESTS "Build the test suite" ON)

include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# RS256 JWT verification (auth/JwtVerifier) and the internal API client
# (http/CurlInternalApiClient) — design doc §8.2's two new C++ dependencies.
find_package(OpenSSL REQUIRED)
find_package(CURL REQUIRED)

file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

add_executable(cig-nexus-server ${SERVER_SOURCES})

target_include_directories(cig-nexus-server
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cig-nexus-server
    PRIVATE
        nlohmann_json::nlohmann_json
        OpenSSL::Crypto
        CURL::libcurl
)

if(BUILD_TESTS)
    include(CTest)
    enable_testing()

    add_subdirectory(tests)
endif()
