cmake_minimum_required(VERSION 3.16)
project(torrent-client-prototype CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)

find_package(OpenSSL REQUIRED)
include_directories(${OPENSSL_INCLUDE_DIR})

include(FetchContent)

# Fetch cpr
set(CPR_USE_SYSTEM_CURL ON)
FetchContent_Declare(cpr GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG dec9422db3af470641f8b0d90e4b451c4daebf64) # The commit hash for 1.11.1. Replace with the latest from: https://github.com/libcpr/cpr/releases
FetchContent_MakeAvailable(cpr)


# Fetch spdlog
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG 8e5613379f5140fefb0b60412fbf1f5406e7c7f8 # The commit hash for 1.15.0
)
FetchContent_MakeAvailable(spdlog)

add_executable(
        ${PROJECT_NAME}
        main.cpp
        peer.h
        torrent_file.h
        peer_connect.cpp
        peer_connect.h
        tcp_connect.cpp
        tcp_connect.h
        torrent_tracker.cpp
        torrent_tracker.h
        torrent_file.cpp
        bencode.cpp
        bencode.h
        message.cpp
        message.h
        byte_tools.h
        byte_tools.cpp
        piece_storage.cpp
        piece_storage.h
        piece.cpp
        piece.h
        userIO.h
        userIO.cpp
)
target_link_libraries(${PROJECT_NAME} PUBLIC ${OPENSSL_LIBRARIES} cpr::cpr spdlog::spdlog)

