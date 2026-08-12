# ============================================================================
# PrismEditor/CMakeLists.txt
# O executavel do editor. Linka a engine (Prism) como biblioteca e fornece
# a implementacao de Prism::CreateApplication() (ver EditorApp.cpp), que e
# o unico ponto de contato entre o main() generico da engine e o codigo
# especifico do editor.
# ============================================================================

file(GLOB_RECURSE PRISM_EDITOR_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
)

add_executable(PrismEditor ${PRISM_EDITOR_SOURCES})

target_include_directories(PrismEditor PRIVATE
    src
)

target_link_libraries(PrismEditor PRIVATE
    Prism
    imguicolortextedit
    imguizmo
)

target_compile_features(PrismEditor PUBLIC cxx_std_20)

# No Windows, em builds Release queremos uma aplicacao GUI (sem console
# aparecendo atras da janela). Em Debug, mantemos o console para ver os
# logs (PRISM_CORE_INFO etc) enquanto o Console panel do editor nao esta
# capturando a saida do Log.h internamente.
if(WIN32)
    set_target_properties(PrismEditor PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
endif()

# Copia a pasta de assets do editor (icones, fontes, etc.) para perto do
# executavel, para que caminhos relativos funcionem tanto rodando de dentro
# do Visual Studio quanto executando o .exe final diretamente.
add_custom_command(TARGET PrismEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/assets
        $<TARGET_FILE_DIR:PrismEditor>/assets
)
