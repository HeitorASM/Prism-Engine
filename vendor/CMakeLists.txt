# ============================================================================
# vendor/CMakeLists.txt
# Dependencias de terceiros da Prism Engine.
#
# glad e stb_image sao vendorizados localmente (vendor/glad, vendor/stb).
# As demais dependencias sao baixadas pelo CMake na primeira configuracao
# (FetchContent), o que exige conexao com a internet nessa primeira vez;
# depois ficam em cache local.
# ============================================================================

include(FetchContent)

# --- glad (vendorizado localmente, ja gerado) ---
add_subdirectory(glad)

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glfw)

# --- glm (matematica: vec/mat/quat) ---
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# --- EnTT (ECS header-only) ---
# Usado pela Scene (ver Prism/src/Prism/Scene/) para armazenar Entities e
# Components.
FetchContent_Declare(
    entt
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG v3.13.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(entt)

# --- Dear ImGui (docking branch - necessario para dockspace/paineis do editor) ---
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imgui)

# ImGui nao tem CMakeLists.txt proprio nesta branch, entao montamos a lib
# manualmente aqui - core + backends GLFW/OpenGL3, que sao os unicos backends
# que esta engine usa.
#
# imgui_impl_glfw.cpp e imgui_impl_opengl3.cpp NAO sao compilados
# diretamente do _deps/imgui-src baixado pelo FetchContent. Em vez disso,
# geramos dois arquivos ".cpp" pequenos (ver IMGUI_GLFW_WRAPPER e
# IMGUI_OPENGL3_WRAPPER abaixo) que garantem, de forma independente do
# compilador e do gerador do CMake (Ninja, MSBuild/Visual Studio,
# Makefiles...), que glad/gl.h e incluido ANTES do backend original. Nao
# usamos /FI (MSVC) nem -include (GCC/Clang) porque essas flags colocam o
# caminho completo do header na linha de comando, e caminhos com espaco
# (comuns em pastas de download do Windows) quebram o parsing dela em
# alguns geradores. Um #include normal dentro de um .cpp nao tem esse
# problema.
set(IMGUI_CUSTOM_LOADER_HEADER
    "${CMAKE_CURRENT_SOURCE_DIR}/imgui_config/imgui_impl_opengl3_loader_custom.h")

set(IMGUI_GLFW_WRAPPER  "${CMAKE_CURRENT_BINARY_DIR}/imgui_wrap/imgui_impl_glfw_wrap.cpp")
set(IMGUI_OPENGL3_WRAPPER "${CMAKE_CURRENT_BINARY_DIR}/imgui_wrap/imgui_impl_opengl3_wrap.cpp")

file(WRITE "${IMGUI_GLFW_WRAPPER}"
"// Gerado por vendor/CMakeLists.txt - nao editar a mao.\n"
"// Garante que glad/gl.h entra ANTES do backend original do ImGui, sem\n"
"// depender de flags de compilador como /FI ou -include.\n"
"#include \"${IMGUI_CUSTOM_LOADER_HEADER}\"\n"
"#include \"${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp\"\n"
)

file(WRITE "${IMGUI_OPENGL3_WRAPPER}"
"// Gerado por vendor/CMakeLists.txt - nao editar a mao.\n"
"// Garante que glad/gl.h entra ANTES do backend original do ImGui, sem\n"
"// depender de flags de compilador como /FI ou -include.\n"
"#include \"${IMGUI_CUSTOM_LOADER_HEADER}\"\n"
"#include \"${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp\"\n"
)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${IMGUI_GLFW_WRAPPER}
    ${IMGUI_OPENGL3_WRAPPER}
)

target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui_config
)

target_link_libraries(imgui PUBLIC glfw)

# glad precisa ser visivel para o backend imgui_impl_opengl3.cpp incluir os
# prototipos de funcao OpenGL antes do GLFW, seguindo a mesma regra de ordem
# de includes que o resto da engine usa.
target_link_libraries(imgui PUBLIC glad)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_CUSTOM)

# --- ImGuiColorTextEdit (editor de codigo com syntax highlight para o painel
#     de scripts do editor - ver PrismEditor/Panels/ScriptEditorPanel) ---
#
# Usamos o fork goossens/ImGuiColorTextEdit, nao o BalazsJako original: o
# original esta sem atualizacoes desde 2019, enquanto o fork e uma
# reescrita ativamente mantida e compativel com a versao atual do Dear
# ImGui (docking, sem funcoes depreciadas).
#
# So 2 arquivos (TextEditor.h/.cpp), sem CMakeLists.txt proprio: baixamos
# o source e montamos a lib manualmente aqui.
FetchContent_Declare(
    imguicolortextedit
    GIT_REPOSITORY https://github.com/goossens/ImGuiColorTextEdit.git
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(imguicolortextedit)

add_library(imguicolortextedit STATIC
    ${imguicolortextedit_SOURCE_DIR}/TextEditor.cpp
)
target_include_directories(imguicolortextedit PUBLIC ${imguicolortextedit_SOURCE_DIR})
target_link_libraries(imguicolortextedit PUBLIC imgui)
# NAO inclui TextDiff.cpp/TextDiff.h/dtl.h de proposito - so sao
# necessarios para o widget de comparacao lado-a-lado, que a engine nao
# usa.

# --- ImGuizmo (gizmos de mover/rotacionar/escalar na viewport 3D -
#     CedricGuillemet/ImGuizmo) ---
#
# Diferente de ImGuiColorTextEdit/Lua, o ImGuizmo JA TEM um CMakeLists.txt
# proprio que cria o target "imguizmo" (biblioteca STATIC com ImGuizmo.cpp
# e os outros widgets do repositorio); FetchContent_MakeAvailable ja e
# suficiente. NAO declarar add_library(imguizmo ...) aqui: um segundo
# target com o mesmo nome e erro do CMake (CMP0002).
#
# O CMakeLists.txt do ImGuizmo so baixa seu proprio ImGui quando
# IMGUIZMO_BUILD_EXAMPLE esta ligado, o que nao acontece quando ele e
# consumido como subdiretorio (IMGUIZMO_STANDALONE fica FALSE), entao nao
# duplica o target "imgui" criado acima.
#
# Versao: o repositorio nao publica releases semanticas (as unicas tags
# sao antigas: 1.9/1.10/1.83) e o desenvolvimento acontece na branch
# "master". Por isso fixamos um COMMIT HASH especifico, e nao "master"
# (que mudaria de build para build). Atualizar o hash e uma escolha
# manual. GIT_SHALLOW FALSE porque o checkout de um commit especifico
# precisa do historico completo.
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1 # master em 08/ago/2026
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imguizmo)

# O target "imguizmo" nao e linkado contra o nosso "imgui" pelo
# CMakeLists.txt dele. Linkamos manualmente para que ImGuizmo.cpp enxergue
# os headers do Dear ImGui na hora de compilar.
target_link_libraries(imguizmo PUBLIC imgui)

# --- Lua 5.4 (linguagem de script) ---
# Lua nao tem CMakeLists.txt oficial (usa so um Makefile), entao baixamos o
# codigo-fonte puro e montamos a lib manualmente aqui. O repositorio e um
# mirror read-only mantido pelos proprios mantenedores da linguagem.
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.7
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lua)

# lua.c, luac.c e onelua.c sao tres arquivos especiais que NAO fazem
# parte da biblioteca (liblua) de verdade:
#   - lua.c / luac.c: contem main() do interpretador `lua` e do compilador
#     `luac` de linha de comando (executaveis standalone, nao a lib).
#   - onelua.c: um "amalgamation" alternativo que da #include em TODOS os
#     outros .c da linguagem num unico translation unit. Compilado JUNTO
#     com os .c individuais (que o GLOB abaixo tambem pega), cada funcao do
#     Lua fica definida DUAS vezes no mesmo binario (LNK4006). E preciso
#     escolher UM dos dois jeitos de compilar; usamos os .c individuais e
#     excluimos onelua.c explicitamente.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua)\\.c$")

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
# Lua e escrito em C puro - compilar como C (nao C++) evita qualquer
# friccao de linkage/ABI entre um .c puro e o resto da engine em C++20.
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)
# NAO definimos LUA_USE_WINDOWS manualmente: luaconf.h ja se autodetecta
# em Windows sozinho:
#     #if !defined(LUA_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#     #define LUA_USE_WINDOWS
#     #endif
# Definir a macro via target_compile_definitions a poe na linha de comando
# do compilador, e o #define de luaconf.h faz o MSVC acusar "redefinicao de
# macro" (C4005) em cada um dos ~35 arquivos .c do Lua.

# --- sol2 (bindings C++ <-> Lua, header-only) ---
# Usado no lugar da C API pura do Lua (stack gerenciada manualmente, sem
# type-safety), com suporte a usertypes, overloads e heranca em C++17/20.
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.3.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sol2)

# --- Jolt Physics (fisica 3D - jrouwe/JoltPhysics) ---
#
# GIT_TAG fixado numa release especifica (nao "master") para builds
# reproduziveis; atualizar e uma escolha manual.
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)
set(USE_ASSERTS ON CACHE BOOL "" FORCE) # asserts do Jolt em Debug: ajudam a pegar erro de uso da API cedo

# USE_STATIC_MSVC_RUNTIME_LIBRARY: opcao do CMakeLists.txt do Jolt, default
# ON no MSVC. Isso compila o Jolt com /MTd (runtime estatico), enquanto o
# resto do projeto usa o default do CMake no Windows, /MDd (runtime
# dinamico). Duas runtimes no mesmo executavel fazem o linker ver DUAS
# copias da biblioteca padrao e falhar com LNK2005, LNK2038 e LNK1169.
# Forcamos OFF para o Jolt usar /MD (Release) e /MDd (Debug), como o resto
# do projeto.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)

# ENABLE_ALL_WARNINGS vem ON por padrao no Jolt e liga /Wall /WX no MSVC,
# pensado para os desenvolvedores do proprio Jolt, nao para quem o consome
# via FetchContent. /Wall ativa avisos informativos de headers do Windows
# SDK (ex: windows.h/winuser.h) e, com /WX, eles viram ERRO FATAL e travam
# a build sem nenhum problema real no codigo. Desligado de proposito.
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build # o CMakeLists.txt real do Jolt fica em Build/, nao na raiz do repo
)
FetchContent_MakeAvailable(joltphysics)
# FetchContent_MakeAvailable ja expoe o target "Jolt" (biblioteca estatica),
# sem namespace de target.

# --- Assimp (importacao de modelos 3D: .obj/.fbx/.gltf/.glb/...) ---
# Usado por Prism::ModelLoader (ver Prism/src/Prism/Assets/ModelLoader.h)
# para converter arquivos de modelo de terceiros no MESMO formato interno
# (GeneratedMesh: vertices MeshVertex + indices) que PrimitiveMeshFactory ja
# produz para as primitivas embutidas - o Renderer nunca precisa saber se
# uma malha veio de um Assimp::Importer ou de uma formula (esfera/cubo/...).
#
# Desligamos TUDO que a engine nao usa (exportadores, ferramentas de linha
# de comando, testes) para nao inflar o tempo de build - so os IMPORTADORES
# dos formatos documentados em docs/formatos-de-arquivo.md continuam
# ligados.
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE) # a engine nao ja tem zlib vendorizado - deixa o Assimp trazer o dele
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE) # so importacao - a engine nunca EXPORTA modelos
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE) # cobre .gltf E .glb (mesmo importador no Assimp)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(assimp)
# FetchContent_MakeAvailable ja expoe o target "assimp" (biblioteca,
# estatica ou dinamica conforme BUILD_SHARED_LIBS - deixamos o default do
# proprio Assimp, que e ON; ver comentario em Prism/CMakeLists.txt sobre
# copiar a .dll/.so para perto do executavel no Windows).

# --- stb_image (carregamento de PNG/JPG/etc) ---
# Header-only, um unico arquivo (stb_image.h), sem depender de
# libpng/libjpeg/zlib. Usado por Prism::Texture2D para ler a imagem do
# disco e entregar os pixels crus.
#
# Fica vendorizado em vendor/stb/stb_image.h (v2.30, dominio publico, de
# nothings/stb) em vez de baixado por FetchContent: o build funciona
# offline e nao depende de um commit ou tag externos. Para atualizar, basta
# substituir o arquivo.
add_library(stb INTERFACE)
target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)
