# ============================================================================
# Prism/CMakeLists.txt
# A engine em si, como biblioteca estatica. Nao tem main() nenhum - quem
# fornece main() e o EntryPoint.h, incluido pelo executavel que consome a lib
# (ver PrismEditor/CMakeLists.txt).
# ============================================================================

file(GLOB_RECURSE PRISM_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)

add_library(Prism STATIC ${PRISM_SOURCES})

target_include_directories(Prism PUBLIC
    src
)

target_link_libraries(Prism PUBLIC
    glad
    glfw
    glm
    imgui
    EnTT::EnTT
    lua
    sol2
    Jolt
    stb
    assimp
)

target_compile_features(Prism PUBLIC cxx_std_20)

# /utf-8 (codigo-fonte da engine em UTF-8, mensagens de log com acentos -
# ver PRISM_CORE_ERROR/PRISM_WARN em Core/Log.h) aplicada DIRETAMENTE a
# este alvo, com PRIVATE (nao propaga para quem faz target_link_libraries
# com Prism, como PrismEditor - que aplica a MESMA flag por conta propria
# no seu proprio CMakeLists.txt) - ver comentario grande no CMakeLists.txt
# da RAIZ sobre por que a flag NAO fica mais em add_compile_options()
# global: precisa nunca alcancar vendor/ (as dependencias FetchContent,
# incluindo o Assimp, que colide com /utf-8 - erro D8016 do MSVC).
if(MSVC)
    target_compile_options(Prism PRIVATE /utf-8)
endif()

# PRISM_PLATFORM_WINDOWS entra aqui quando formos abstrair diferencas de
# plataforma de verdade (ex: dialogos nativos de arquivo). Por ora so
# marcamos a definicao para uso futuro.
if(WIN32)
    target_compile_definitions(Prism PUBLIC PRISM_PLATFORM_WINDOWS)
endif()
