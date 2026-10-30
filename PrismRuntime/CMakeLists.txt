# ============================================================================
# PrismRuntime/CMakeLists.txt
# O executavel que RODA um projeto sem o editor: carrega o .prismproj, abre o
# mapa inicial e joga (Lua + scripts C++). Mesmo main() generico do editor
# (EntryPoint.h); so implementa Prism::CreateApplication() (ver RuntimeApp.cpp).
# Uso:  PrismRuntime <pasta-do-projeto | arquivo.prismproj>
# ============================================================================

file(GLOB_RECURSE PRISM_RUNTIME_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)

add_executable(PrismRuntime ${PRISM_RUNTIME_SOURCES})

target_include_directories(PrismRuntime PRIVATE
    src
)

target_link_libraries(PrismRuntime PRIVATE
    Prism
)

target_compile_features(PrismRuntime PUBLIC cxx_std_20)

# /utf-8 aplicada direto a este alvo - mesmo motivo de Prism/CMakeLists.txt.
if(MSVC)
    target_compile_options(PrismRuntime PRIVATE /utf-8)
endif()

# Windows Release: aplicacao GUI (sem console atras da janela). Em Debug o
# console fica, para ver os logs.
if(WIN32)
    set_target_properties(PrismRuntime PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

# Shaders e fontes ao lado do executavel (mesmo esquema do editor): num jogo
# distribuido o caminho absoluto do codigo-fonte nao existe, e a engine usa
# estas copias (ver Renderer.cpp, FindShaderDirectory).
add_custom_command(TARGET PrismRuntime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PRISM_SHADER_DIR}
        $<TARGET_FILE_DIR:PrismRuntime>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PRISM_FONT_DIR}
        $<TARGET_FILE_DIR:PrismRuntime>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:assimp>
        $<TARGET_FILE_DIR:PrismRuntime>
)
