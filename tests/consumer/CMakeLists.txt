cmake_minimum_required(VERSION 3.18)
project(eqmdsk_consumer LANGUAGES CXX)

enable_testing()

find_package(eqmdsk 0.9 CONFIG REQUIRED)

add_executable(eqmdsk_consumer main.cpp)
target_link_libraries(eqmdsk_consumer PRIVATE eqmdsk::eqmdsk)
add_test(NAME eqmdsk_consumer COMMAND eqmdsk_consumer)
