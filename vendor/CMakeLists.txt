# ============================================================================
# vendor/CMakeLists.txt
# Dependencias de terceiros da Prism Engine.
#
# glad e vendorizado localmente (arquivo gerado, ver vendor/glad/).
# GLFW, glm e Dear ImGui sao baixados automaticamente pelo CMake na primeira
# configuracao do projeto (FetchContent) - isso e o que resolve o problema
# de "como eu configuro tudo isso no Visual Studio": voce nao configura nada
# manualmente, o CMake baixa e liga tudo sozinho.
#
# Isso exige conexao com a internet na PRIMEIRA vez que voce configurar o
# projeto (CMake vai clonar os repositorios do GitHub). Depois disso, fica
# em cache local e builds seguintes nao precisam mais de rede.
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
# Components. Header-only, sem custo de build adicional (so inclui um
# header), e e o que Hazel/muitas engines caseiras C++ usam pelo mesmo
# motivo: um ECS robusto e rapido sem reinventar a roda numa fase inicial
# do projeto onde o foco deveria estar no editor e no renderer, nao em
# escrever um sparse-set do zero.
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
# geramos dois arquivos ".cpp" pequenos (ver bloco IMGUI_IMPL_CUSTOM_LOADER
#_WRAPPERS abaixo) que garantem, de forma independente do compilador e do
# gerador do CMake (Ninja, MSBuild/Visual Studio, Makefiles...), que
# glad/gl.h e incluido ANTES do backend original. Isso substitui a
# abordagem anterior baseada em /FI (MSVC) e -include (GCC/Clang): aquelas
# flags colocam o caminho completo do header na linha de comando do
# compilador, e caminhos com espaco (comuns em pastas de download do
# Windows, ex: "PrismEngineV1_fixed (1)") quebravam o parsing dessa flag em
# alguns geradores do CMake, causando "Nao e possivel abrir arquivo
# incluir". Um #include normal dentro de um .cpp nao tem esse problema.
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
# Usamos o fork goossens/ImGuiColorTextEdit, nao o BalazsJako/ImGuiColorTextEdit
# original: o original esta sem atualizacoes desde 2019 (ver README do proprio
# fork), enquanto o fork do goossens e uma reescrita completa, ativamente
# mantida, compativel com a versao atual do Dear ImGui (docking, sem funcoes
# depreciadas) e endossada pelo proprio criador do ImGui (ocornut) como a
# opcao recomendada em 2026 - ver "Useful Extensions" na wiki do ocornut/imgui.
#
# So 2 arquivos (TextEditor.h/.cpp), sem CMakeLists.txt proprio - mesmo
# approach ja usado acima para ImGui/Lua: baixamos o source e montamos a lib
# manualmente aqui.
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
# NAO inclui TextDiff.cpp/TextDiff.h/dtl.h de proposito - esses arquivos so
# sao necessarios para o widget de comparacao lado-a-lado (TextDiff), que a
# engine nao usa ainda. Ver README do goossens/ImGuiColorTextEdit, secao
# "Integration": TextEditor.cpp/.h sozinhos sao suficientes para um editor de
# texto com highlight simples como o nosso.

# --- ImGuizmo (gizmos de mover/rotacionar/escalar objetos direto na
#     viewport 3D - CedricGuillemet/ImGuizmo, o repositorio original e
#     ainda ativamente mantido) ---
#
# Diferente de ImGuiColorTextEdit/Lua acima, este commit do ImGuizmo JA TEM
# um CMakeLists.txt proprio (cria o target "imguizmo" sozinho, biblioteca
# STATIC com ImGuizmo.cpp + os outros widgets do repositorio -
# GraphEditor/ImCurveEdit/ImGradient/ImSequencer/ImVectorEditor - e ja
# expoe o include directory certo) - FetchContent_MakeAvailable ja e
# suficiente, sem precisar montar a lib manualmente como fizemos para
# ImGuiColorTextEdit. NAO declarar add_library(imguizmo ...) de novo aqui:
# o proprio CMakeLists.txt do ImGuizmo ja faz isso, e um segundo
# add_library com o mesmo nome de target e um erro do CMake (CMP0002) -
# foi exatamente esse erro que apareceu na primeira tentativa desta
# integracao, corrigido removendo nosso add_library duplicado.
#
# O CMakeLists.txt do ImGuizmo tenta baixar seu PROPRIO ImGui (branch
# "master" upstream) so quando IMGUIZMO_BUILD_EXAMPLE esta ligado (default
# TRUE somente quando ele e o projeto raiz standalone) - como aqui ele e
# consumido como subdiretorio de um projeto maior (via FetchContent),
# IMGUIZMO_STANDALONE fica FALSE automaticamente e IMGUIZMO_BUILD_EXAMPLE
# tambem, entao nao baixa nada extra nem duplica o target "imgui" que ja
# criamos acima.
#
# Usado por EditorLayer::RenderTransformGizmo para desenhar as setas/aneis
# de Translate/Rotate/Scale sobre a entidade selecionada na viewport
# principal (ver EditorLayer.cpp) - antes desta integracao, a UNICA forma
# de mover/rotacionar uma entidade era digitar valores manualmente nos
# DragFloat3 da Properties panel.
#
# ATENCAO sobre a versao: o repositorio NAO publica releases semanticas
# (as unicas tags existentes sao 1.9/1.10/1.83, todas antigas - ver
# github.com/CedricGuillemet/ImGuizmo/tags) - todo desenvolvimento ativo
# acontece direto na branch "master". Por isso fixamos um COMMIT HASH
# especifico do master (nao "master" em si, que mudaria de build para
# build sem aviso) - atualizar este hash e uma escolha manual e
# deliberada, mesmo padrao ja usado para o Box3D acima (fixado em v0.1.0
# de proposito). GIT_SHALLOW FALSE aqui porque um checkout de commit
# especifico (em vez de branch/tag) precisa do historico completo.
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 18cef5e031d8c6973d80284c67f60549fafd78c1 # master em 08/ago/2026 - ver nota acima
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imguizmo)

# O CMakeLists.txt do ImGuizmo ja cria o target "imguizmo" (ver comentario
# acima) mas NAO o linka contra o nosso "imgui" (o dele so linkaria um
# imgui proprio, baixado so se IMGUIZMO_BUILD_EXAMPLE estivesse ligado, o
# que nao e o caso aqui - ver acima). Precisamos linkar manualmente contra
# o "imgui" que este arquivo ja monta mais acima, para que ImGuizmo.cpp
# enxergue os headers do Dear ImGui (imgui.h etc) na hora de compilar.
target_link_libraries(imguizmo PUBLIC imgui)

# --- Lua 5.4 (linguagem de script - ver guia do prototipo) ---
# Lua nao tem CMakeLists.txt oficial (o projeto original usa so um
# Makefile), entao baixamos o codigo-fonte puro e montamos a lib
# manualmente aqui - mesmo approach ja usado para o ImGui acima. O
# repositorio oficial e um mirror read-only (lua.org nao hospeda git),
# mantido pelos proprios mantenedores da linguagem.
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
#     outros .c da linguagem dentro de um unico translation unit (uma forma
#     alternativa de compilar Lua inteiro de uma vez, ver comentario no
#     topo do proprio arquivo no repositorio oficial). Se compilado JUNTO
#     com os .c individuais (lapi.c, lauxlib.c, lbaselib.c etc, que o GLOB
#     abaixo tambem pega), cada funcao Lua fica definida DUAS vezes no
#     mesmo binario - um multiply-defined symbol / LNK4006 (warnings tipo
#     "lua_next ja definido no onelua.c.obj; segunda definicao ignorada")
#     para cada funcao publica de Lua. A correcao e escolher UM dos dois
#     jeitos de compilar Lua, nunca os dois - aqui usamos os .c
#     individuais (mais facil de depurar arquivo por arquivo se algo der
#     errado) e excluimos onelua.c explicitamente.
file(GLOB LUA_SOURCES CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/*.c")
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua)\\.c$")

add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${lua_SOURCE_DIR})
# Lua e escrito em C puro - compilar como C (nao C++) evita qualquer
# friccao de linkage/ABI entre um .c puro e o resto da engine em C++20.
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)
# NAO definimos LUA_USE_WINDOWS manualmente: luaconf.h (o header de
# configuracao do proprio Lua, dentro do source baixado acima) ja se
# AUTODETECTA em Windows sozinho:
#     #if !defined(LUA_USE_C89) && defined(_WIN32) && !defined(_WIN32_WCE)
#     #define LUA_USE_WINDOWS
#     #endif
# (isso e assim desde o Lua 5.3 - ver luaconf.h no source baixado, procure
# por "LUA_USE_WINDOWS"). Definir a macro de novo aqui via
# target_compile_definitions colocava ela na linha de comando do
# compilador (/D...), que e processada ANTES do preprocessador chegar no
# #include de luaconf.h - quando luaconf.h roda seu proprio #if
# !defined(LUA_USE_WINDOWS)/#define, a macro ja existe (veio da linha de
# comando) e o MSVC acusa "redefinicao de macro" (C4005), uma vez para
# CADA um dos ~35 arquivos .c do Lua que estao sendo compilados. A
# correcao e simplesmente nao definir a macro - deixar luaconf.h fazer
# essa deteccao sozinho, como ele ja faz por padrao.

# --- sol2 (bindings C++ <-> Lua, header-only) ---
# Por que sol2 em vez de escrever bindings a mao com a C API do Lua
# (lua_pushnumber, lua_gettable, etc)? Porque a C API pura e propensa a
# erro (stack do Lua gerenciada manualmente, sem type-safety nenhuma) - o
# guia do prototipo ja cita sol2/LuaBridge como opcoes; sol2 foi escolhido
# por ser o mais usado atualmente e ter suporte solido a C++17/20
# (usertypes, overloads, heranca).
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.3.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(sol2)

# --- Jolt Physics (fisica 3D - jrouwe/JoltPhysics) ---
#
# Substituiu o Box3D (erincatto/box3d) nesta engine - ver git log / PR de
# migracao para o motivo completo. Resumo: Box3D era v0.1.0 alpha (unica
# release existente), com o proprio autor pedindo para nao mandar PRs
# ainda (API instavel por design, sem garantia de compatibilidade entre
# versoes). Jolt e o motor fisico usado em producao pela Horizon Forbidden
# West (Guerrilla Games) e por diversos engines indie C++ serios, com
# releases semanticas estaveis, documentacao completa e comunidade ativa -
# a escolha natural para uma engine que vai depender de fisica solida no
# longo prazo.
#
# GIT_TAG fixado numa release especifica (nao "master") pelo mesmo motivo
# de disciplina de versionamento ja usado no resto deste arquivo (glfw,
# glm, entt etc) - atualizar e uma escolha manual e deliberada.
set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
set(CROSS_PLATFORM_DETERMINISTIC OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OBJECT_LAYER_BITS 16 CACHE STRING "" FORCE)
set(USE_ASSERTS ON CACHE BOOL "" FORCE) # queremos os asserts do Jolt em Debug - ajudam a pegar erro de uso da API cedo, igual o assert do Box3D ja ajudava antes

# ENABLE_ALL_WARNINGS vem ON por padrao no CMakeLists do proprio Jolt (ver
# Build/CMakeLists.txt do repositorio oficial) - liga /Wall /WX no MSVC,
# que e PENSADO para os proprios devs do Jolt manterem o codigo deles
# limpo, nao para quem consome a lib como dependencia via FetchContent.
# /Wall no MSVC ativa avisos informativos de headers do proprio Windows
# SDK (ex: mudanca futura de tipo subjacente de enum com /Zc:enumTypes,
# em headers como windows.h/winuser.h que o Jolt inclui internamente) -
# com /WX, esses avisos viram ERRO FATAL e travam a build inteira, mesmo
# sem nenhum problema real no codigo do Jolt ou desta engine. Desligado
# de proposito aqui.
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.2.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build # o CMakeLists.txt real do Jolt fica em Build/, nao na raiz do repo - ver estrutura do repositorio oficial
)
FetchContent_MakeAvailable(joltphysics)
# FetchContent_MakeAvailable ja expoe o target "Jolt" (biblioteca estatica).
# Diferente do Box3D, Jolt nao usa namespace de target tipo "jolt::jolt" -
# e so "Jolt" mesmo, ver Build/CMakeLists.txt do repositorio oficial.