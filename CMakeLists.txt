cmake_minimum_required(VERSION 3.20)
project(castam CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra)

add_library(castam_core STATIC
  src/lexer.cpp
  src/ast_dump.cpp
  src/parser.cpp
)
target_include_directories(castam_core PUBLIC src)

add_executable(castam src/main.cpp)
target_link_libraries(castam PRIVATE castam_core)

enable_testing()

add_executable(lexer_test tests/lexer_test.cpp)
target_link_libraries(lexer_test PRIVATE castam_core)
add_test(NAME lexer_test COMMAND lexer_test)

add_executable(ast_dump_test tests/ast_dump_test.cpp)
target_link_libraries(ast_dump_test PRIVATE castam_core)
add_test(NAME ast_dump_test COMMAND ast_dump_test)

add_executable(parser_test tests/parser_test.cpp)
target_link_libraries(parser_test PRIVATE castam_core)
add_test(NAME parser_test COMMAND parser_test)

add_executable(examples_test tests/examples_test.cpp)
target_link_libraries(examples_test PRIVATE castam_core)
target_compile_definitions(examples_test PRIVATE
  EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/examples")
add_test(NAME examples_test COMMAND examples_test)
