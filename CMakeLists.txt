cmake_minimum_required(VERSION 3.20)
project(PrismEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /utf-8 NAO vai em add_compile_options() global: alcancaria vendor/ e colide
# com o /source-charset:utf-8 do Assimp (erro D8016). Fica por alvo.
if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Debug/Release definem PRISM_DEBUG para habilitar PRISM_ASSERT etc.
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DPRISM_DEBUG")

# Nome/versao/build vem do Version.py (gera <build>/generated/PrismVersion.h).
include(cmake/PrismVersion.cmake)

add_subdirectory(vendor)
add_subdirectory(Prism)
add_subdirectory(PrismEditor)
add_subdirectory(PrismRuntime)

# O editor exporta jogos usando o PrismRuntime (ver PrismEditor/CMakeLists.txt):
# o Runtime precisa estar compilado antes do passo final do editor.
add_dependencies(PrismEditor PrismRuntime)

# Testes automatizados: opt-in (nao afetam o build normal). Ver tests/CMakeLists.txt.
option(PRISM_BUILD_TESTS "Compila os testes automatizados" OFF)
if(PRISM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
