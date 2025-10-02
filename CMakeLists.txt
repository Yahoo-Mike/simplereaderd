cmake_minimum_required(VERSION 3.28)
project(simplereaderd)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(SQLite3 REQUIRED)
find_package(Drogon REQUIRED)

# Pick up all .cpp files from src/
file(GLOB SRC_FILES ${PROJECT_SOURCE_DIR}/src/*.cpp)

add_executable(simplereaderd ${SRC_FILES})

# Add include/ folder for headers
target_include_directories(simplereaderd PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(simplereaderd PRIVATE Drogon::Drogon SQLite::SQLite3 sodium)

# build the add_user tool (to add a user to database)
add_executable(add_user tools/add_user.c)
target_link_libraries(add_user PRIVATE SQLite::SQLite3 sodium)
