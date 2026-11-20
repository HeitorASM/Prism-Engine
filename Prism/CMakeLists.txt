# ============================================================================
# Prism/CMakeLists.txt
# A engine em si, como biblioteca estatica. Nao tem main() nenhum - quem
# fornece main() e o EntryPoint.h, incluido pelo executavel que consome a lib
# (ver PrismEditor/CMakeLists.txt).
# ============================================================================

file(GLOB_RECURSE PRISM_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
    src/*.h
    src/*.c
)

add_library(Prism STATIC ${PRISM_SOURCES})

target_include_directories(Prism PUBLIC
    src
)

target_link_libraries(Prism PUBLIC
    prism_version
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
    prism_miniaudio
)

target_compile_features(Prism PUBLIC cxx_std_20)

# SDK dos scripts em C++: arquivo CMake com os caminhos de include que um
# modulo de projeto precisa (engine, GLM, EnTT). Gerado a cada configure e
# copiado para perto do executavel do editor (ver PrismEditor/CMakeLists.txt);
# o CMakeLists.txt do projeto de scripts faz include() dele - ver
# NativeScripting::GenerateProjectFiles.
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/PrismSDK.cmake CONTENT
"set(PRISM_SDK_INCLUDE_DIRS \"${CMAKE_CURRENT_SOURCE_DIR}/src;$<TARGET_PROPERTY:glm,INTERFACE_INCLUDE_DIRECTORIES>;$<TARGET_PROPERTY:EnTT::EnTT,INTERFACE_INCLUDE_DIRECTORIES>\")\n")

# /utf-8 por alvo (PRIVATE), nunca global: ver CMakeLists.txt da raiz.
if(MSVC)
    # /bigobj: ScriptEngine.cpp (sol2 + muitos usertypes) estoura o limite de secoes do .obj padrao.
    target_compile_options(Prism PRIVATE /utf-8 /bigobj)
endif()

# Shaders: em desenvolvimento o hot reload le o caminho absoluto do codigo-fonte;
# num jogo distribuido vale a copia 'shaders/' ao lado do executavel (POST_BUILD).
set(PRISM_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders CACHE INTERNAL "Pasta de shaders da engine")
target_compile_definitions(Prism PRIVATE PRISM_SHADER_SOURCE_DIR="${PRISM_SHADER_DIR}")

# Fonte da UI in-game: mesmo esquema dos shaders (fonte do projeto em dev, copia ao lado do exe).
set(PRISM_FONT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/fonts CACHE INTERNAL "Pasta de fontes da engine")
target_compile_definitions(Prism PRIVATE PRISM_FONT_SOURCE_DIR="${PRISM_FONT_DIR}")

if(WIN32)
    target_compile_definitions(Prism PUBLIC PRISM_PLATFORM_WINDOWS)
endif()
