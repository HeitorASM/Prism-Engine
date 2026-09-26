cmake_minimum_required(VERSION 3.20)
project(PrismEngine LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# NAO usar add_compile_options(/utf-8) aqui: add_compile_options() no
# escopo do diretorio RAIZ e herdado por TODOS os add_subdirectory()
# abaixo, inclusive vendor/ (as dependencias de terceiros, buildadas via
# FetchContent). O Assimp (ver vendor/CMakeLists.txt, adicionado para
# importacao de modelos 3D) ja adiciona /source-charset:utf-8 nas
# flags de alguns dos seus proprios arquivos-fonte internamente - o MSVC
# (cl.exe) rejeita compilar quando as DUAS flags aparecem juntas na mesma
# linha de comando ("opcoes de linha de comando '/utf-8' e
# '/source-charset:utf-8' sao incompativeis", erro D8016), mesmo sendo
# equivalentes em efeito (/utf-8 e so um atalho para
# /source-charset:utf-8 /execution-charset:utf-8 juntos).
#
# A flag e aplicada, em vez disso, DIRETAMENTE aos alvos da PROPRIA
# engine (Prism/CMakeLists.txt e PrismEditor/CMakeLists.txt, via
# target_compile_options com PRIVATE) - vendor/ nunca a recebe, entao
# nunca colide com o que o Assimp configura para si mesmo. O comentario
# original desta flag (codigo-fonte da engine em UTF-8, mensagens de
# PRISM_CORE_ERROR/PRISM_WARN com acentos) continua valendo, so o ESCOPO
# de aplicacao mudou.
if(MSVC)
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
