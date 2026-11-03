cmake_minimum_required(VERSION 3.20)
project(OpenModelAssimpConsumer LANGUAGES CXX)

find_package(OpenModel CONFIG REQUIRED COMPONENTS Assimp)

add_executable(openmodel-assimp-consumer main.cpp)
target_link_libraries(openmodel-assimp-consumer PRIVATE OpenModel::Assimp)
target_compile_features(openmodel-assimp-consumer PRIVATE cxx_std_17)
