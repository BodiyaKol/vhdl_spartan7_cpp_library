cmake_minimum_required(VERSION 3.16)
project(Spartan7DynOptLib VERSION 1.0.0 LANGUAGES CXX)

# ──────────────────────────────────────────────
set(CMAKE_CXX_STANDARD          20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

# ──────────────────────────────────────────────
# Опції збірки
# ──────────────────────────────────────────────
option(BUILD_EXAMPLES  "Збирати приклади"          ON)
option(BUILD_TESTS     "Збирати тести"             ON)
option(ENABLE_WARNINGS "Вмикнути всі попередження" ON)

# ──────────────────────────────────────────────
# Визначення цілей компіляції
# ──────────────────────────────────────────────
set(LIB_SOURCES
    src/cpp/hardware_mapper.cpp
    src/cpp/genetic_algorithm.cpp
    src/cpp/constraint_solver.cpp
    src/cpp/module_registry.cpp
    src/cpp/spartan7_optimizer.cpp
)

add_library(spartan7_opt STATIC ${LIB_SOURCES})

target_include_directories(spartan7_opt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/cpp>
        $<INSTALL_INTERFACE:include/spartan7>
)

if(ENABLE_WARNINGS)
    target_compile_options(spartan7_opt PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:
            -Wall -Wextra -Wpedantic
            -Wshadow -Wconversion -Wno-sign-conversion
        >
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    )
endif()

# ──────────────────────────────────────────────
# Приклади
# ──────────────────────────────────────────────
if(BUILD_EXAMPLES)
    add_executable(basic_usage examples/basic_usage.cpp)
    target_link_libraries(basic_usage PRIVATE spartan7_opt)
endif()

# ──────────────────────────────────────────────
# Тести
# ──────────────────────────────────────────────
if(BUILD_TESTS)
    enable_testing()

    add_executable(test_optimizer tests/test_optimizer.cpp)
    target_link_libraries(test_optimizer PRIVATE spartan7_opt)

    add_test(NAME optimizer_test COMMAND test_optimizer)
endif()

# ──────────────────────────────────────────────
# Встановлення
# ──────────────────────────────────────────────
install(TARGETS spartan7_opt
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES
    src/cpp/hardware_mapper.hpp
    src/cpp/genetic_algorithm.hpp
    src/cpp/constraint_solver.hpp
    src/cpp/module_registry.hpp
    src/cpp/spartan7_optimizer.hpp
    DESTINATION include/spartan7
)
