cmake_minimum_required(VERSION 3.5)
project(simplereaderd)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Drogon REQUIRED)
# Add your include/ folder for headers
include_directories(${PROJECT_SOURCE_DIR}/include)

# Pick up all .cpp files from src/
file(GLOB SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)

add_executable(simplereaderd ${SRC_FILES})
target_link_libraries(simplereaderd PRIVATE Drogon::Drogon)
