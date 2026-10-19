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
    nfd
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

# Copia os shaders GLSL da engine (Prism/shaders) para perto do executavel.
# Em desenvolvimento a engine le direto da pasta do codigo-fonte (hot reload);
# esta copia vale quando o .exe roda longe da arvore do projeto.
add_custom_command(TARGET PrismEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PRISM_SHADER_DIR}
        $<TARGET_FILE_DIR:PrismEditor>/shaders
)

# Documentacao da engine (docs/*.md): painel "Documentacao" do editor (F1).
# Em desenvolvimento le direto da pasta do codigo-fonte; fora dela, da copia
# ao lado do executavel.
set(PRISM_DOCS_DIR ${CMAKE_SOURCE_DIR}/docs)
target_compile_definitions(PrismEditor PRIVATE PRISM_DOCS_SOURCE_DIR="${PRISM_DOCS_DIR}")
# remove_directory antes: evita sobrar paginas antigas (renomeadas/apagadas) na copia.
add_custom_command(TARGET PrismEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
        $<TARGET_FILE_DIR:PrismEditor>/docs
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PRISM_DOCS_DIR}
        $<TARGET_FILE_DIR:PrismEditor>/docs
)

# Copia as fontes (UI in-game) para perto do executavel - mesmo esquema dos shaders.
add_custom_command(TARGET PrismEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PRISM_FONT_DIR}
        $<TARGET_FILE_DIR:PrismEditor>/fonts
)

# SDK dos scripts em C++ (caminhos de include da engine) - lido pelo editor
# ao gerar o projeto de scripts do usuario (NativeScripting::GetSdkFile).
add_custom_command(TARGET PrismEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/PrismSDK.cmake
        $<TARGET_FILE_DIR:PrismEditor>
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
