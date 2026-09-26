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

# /utf-8 aplicada diretamente a este alvo - mesmo motivo e mesmo padrao de
# Prism/CMakeLists.txt (ver comentario grande la e no CMakeLists.txt da
# RAIZ): nao pode voltar a ser um add_compile_options() global, senao
# alcanca vendor/ de novo e recria a colisao com o /source-charset:utf-8
# que o Assimp adiciona internamente para si mesmo (erro D8016).
if(MSVC)
    target_compile_options(PrismEditor PRIVATE /utf-8)
endif()

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

# Assimp builda como biblioteca DINAMICA por padrao (assimp-vc***.dll no
# Windows) - sem copiar essa DLL para perto do .exe, o editor abre uma
# janela de erro "assimp-vc***.dll nao encontrada" ao iniciar, mesmo com o
# link bem-sucedido em tempo de COMPILACAO (o erro so aparece em tempo de
# EXECUCAO). $<TARGET_FILE:assimp> resolve para o caminho da DLL so
# quando ela existe (biblioteca SHARED); em uma build eventualmente
# configurada como STATIC (BUILD_SHARED_LIBS=OFF) este comando ainda roda
# mas so copia o .lib/.a, o que e inofensivo.
add_custom_command(TARGET PrismEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:assimp>
        $<TARGET_FILE_DIR:PrismEditor>
)
