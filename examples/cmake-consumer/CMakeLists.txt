cmake_minimum_required(VERSION 3.20)
project(OpenModelConsumer LANGUAGES CXX)

find_package(OpenModel 0.1 CONFIG REQUIRED)

add_executable(openmodel_consumer main.cpp)
target_link_libraries(openmodel_consumer PRIVATE OpenModel::Core)
