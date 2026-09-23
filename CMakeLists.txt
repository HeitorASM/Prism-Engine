cmake_minimum_required(VERSION 3.20)
project(PrismEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Debug/Release definem PRISM_DEBUG para habilitar PRISM_ASSERT etc.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPRISM_DEBUG")

add_subdirectory(vendor)
add_subdirectory(Prism)
add_subdirectory(PrismEditor)

# Testes automatizados: opt-in (nao afetam o build normal). Ver tests/CMakeLists.txt.
option(PRISM_BUILD_TESTS "Compila os testes automatizados" OFF)
if(PRISM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
