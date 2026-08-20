cmake_minimum_required(VERSION 3.20)
project(castam CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra)

add_library(castam_core STATIC
  src/lexer.cpp
)
target_include_directories(castam_core PUBLIC src)

add_executable(castam src/main.cpp)
target_link_libraries(castam PRIVATE castam_core)

enable_testing()

add_executable(lexer_test tests/lexer_test.cpp)
target_link_libraries(lexer_test PRIVATE castam_core)
add_test(NAME lexer_test COMMAND lexer_test)
