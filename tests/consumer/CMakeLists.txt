cmake_minimum_required(VERSION 3.18)
project(eqmdsk_consumer LANGUAGES CXX)

find_package(eqmdsk 0.9 CONFIG REQUIRED)

add_executable(eqmdsk_consumer main.cpp)
target_link_libraries(eqmdsk_consumer PRIVATE eqmdsk::eqmdsk)

