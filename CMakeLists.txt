cmake_minimum_required(VERSION 3.10)
project(video_downloader)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(CURL REQUIRED)
find_package(nlohmann_json 3.2.0 REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(video_downloader
    main.cc
    video_downloader.cc
)

target_link_libraries(video_downloader
    PRIVATE
    CURL::libcurl
    nlohmann_json::nlohmann_json
    OpenSSL::Crypto
)
