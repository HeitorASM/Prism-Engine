# ============================================================================
# Testes automatizados. OPT-IN: nao entram no build normal.
#
#   cmake -B build -S . -DPRISM_BUILD_TESTS=ON
#   cmake --build build --target PrismAssetTests PrismShaderTests PrismExportTests PrismUITests
#   ctest --test-dir build --output-on-failure
#
# PrismAssetTests compila SO as fontes de que precisa (Assets/ + Project/) em
# vez de linkar a biblioteca Prism inteira
# ============================================================================

# Caminho relativo a ESTE arquivo (CMAKE_CURRENT_SOURCE_DIR), nao a raiz do
# build (CMAKE_SOURCE_DIR): se a engine for incluida como subprojeto de outro
# CMake, CMAKE_SOURCE_DIR passaria a apontar para o projeto de fora e este
# alvo deixaria de achar as fontes.
set(PRISM_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../Prism/src)

add_executable(PrismAssetTests
    AssetTests.cpp
    ${PRISM_SRC}/Prism/Assets/AssetID.cpp
    ${PRISM_SRC}/Prism/Assets/AssetMeta.cpp
    ${PRISM_SRC}/Prism/Assets/AssetRegistry.cpp
    ${PRISM_SRC}/Prism/Project/Project.cpp
    ${PRISM_SRC}/Prism/Project/ProjectSerializer.cpp
)

target_include_directories(PrismAssetTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismAssetTests PRIVATE cxx_std_20)

# /utf-8: mesmo motivo/padrao de Prism/CMakeLists.txt e
# PrismEditor/CMakeLists.txt (ver comentario grande no CMakeLists.txt da
# RAIZ) - este alvo tambem tem fontes em UTF-8 com acentos (ver
# AssetTests.cpp, ex: "Pedra áspera.png" usado como teste de path).
# PrismAssetTests nao linka o Assimp (compila so Assets/+Project/, ver
# comentario no topo deste arquivo), entao nao ha colisao possivel aqui -
# mesmo assim aplicada por target, nao globalmente, para manter UM SO
# padrao em todo o repositorio (nunca add_compile_options() na raiz).
if(MSVC)
    target_compile_options(PrismAssetTests PRIVATE /utf-8)
endif()

add_test(NAME AssetTests COMMAND PrismAssetTests)

# PrismShaderTests: carregador de shaders (#include, defines, #line) - so
# preprocessamento de texto, sem GPU. Tambem expande TODOS os shaders reais de
# Prism/shaders, entao pega include quebrado ou arquivo renomeado em CI.
add_executable(PrismShaderTests
    ShaderSourceTests.cpp
    ${PRISM_SRC}/Prism/Renderer/ShaderSource.cpp
)
target_include_directories(PrismShaderTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismShaderTests PRIVATE cxx_std_20)
target_compile_definitions(PrismShaderTests PRIVATE
    PRISM_TEST_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../Prism/shaders")
if(MSVC)
    target_compile_options(PrismShaderTests PRIVATE /utf-8)
endif()
add_test(NAME ShaderTests COMMAND PrismShaderTests)

# PrismExportTests: exportador de jogos (Project/GameExporter) - so std::filesystem,
# usa pastas temporarias com arquivos de mentira. Sem GPU nem a engine inteira.
add_executable(PrismExportTests
    ExportTests.cpp
    ${PRISM_SRC}/Prism/Project/GameExporter.cpp
)
target_include_directories(PrismExportTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismExportTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(PrismExportTests PRIVATE /utf-8)
endif()
add_test(NAME ExportTests COMMAND PrismExportTests)

# PrismUITests: nucleo da UI em texto (.prismui) - parser XML, layout e hover/clique.
# So std (sem GPU, GLFW, EnTT nem GLM): UI/UIMarkup, UILayout e UIInput.
add_executable(PrismUITests
    UITests.cpp
    ${PRISM_SRC}/Prism/UI/UIMarkup.cpp
    ${PRISM_SRC}/Prism/UI/UILayout.cpp
    ${PRISM_SRC}/Prism/UI/UIInput.cpp
)
target_include_directories(PrismUITests PRIVATE ${PRISM_SRC})
target_compile_features(PrismUITests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(PrismUITests PRIVATE /utf-8)
endif()
add_test(NAME UITests COMMAND PrismUITests)

# PrismCullingTests: frustum culling e camadas de visibilidade (Renderer/Culling.h, so std).
add_executable(PrismCullingTests
    CullingTests.cpp
)
target_include_directories(PrismCullingTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismCullingTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(PrismCullingTests PRIVATE /utf-8)
endif()
add_test(NAME CullingTests COMMAND PrismCullingTests)

# PrismPhysicsLayersTests: camadas de fisica e matriz de colisao (Physics/PhysicsLayers.h, so std).
add_executable(PrismPhysicsLayersTests
    PhysicsLayersTests.cpp
)
target_include_directories(PrismPhysicsLayersTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismPhysicsLayersTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(PrismPhysicsLayersTests PRIVATE /utf-8)
endif()
add_test(NAME PhysicsLayersTests COMMAND PrismPhysicsLayersTests)

# PrismSceneTests: serializacao de cena, ComponentRegistry e PrefabSyncer. Linka a
# biblioteca Prism inteira (Scene depende de Fisica/Audio/Scripts), entao so existe
# num build completo. Sem janela nem GPU.
add_executable(PrismSceneTests
    SceneTests.cpp
)
target_include_directories(PrismSceneTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismSceneTests PRIVATE cxx_std_20)
target_link_libraries(PrismSceneTests PRIVATE Prism)
if(MSVC)
    target_compile_options(PrismSceneTests PRIVATE /utf-8)
endif()
add_test(NAME SceneTests COMMAND PrismSceneTests)

# PrismProfilerTests: medidor de desempenho (Core/Profiler.h, so std, relogio falso).
add_executable(PrismProfilerTests
    ProfilerTests.cpp
)
target_include_directories(PrismProfilerTests PRIVATE ${PRISM_SRC})
target_compile_features(PrismProfilerTests PRIVATE cxx_std_20)
if(MSVC)
    target_compile_options(PrismProfilerTests PRIVATE /utf-8)
endif()
add_test(NAME ProfilerTests COMMAND PrismProfilerTests)
