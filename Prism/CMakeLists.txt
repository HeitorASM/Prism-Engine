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
)

target_compile_features(Prism PUBLIC cxx_std_20)

# PRISM_PLATFORM_WINDOWS entra aqui quando formos abstrair diferencas de
# plataforma de verdade (ex: dialogos nativos de arquivo). Por ora so
# marcamos a definicao para uso futuro.
if(WIN32)
    target_compile_definitions(Prism PUBLIC PRISM_PLATFORM_WINDOWS)
endif()
