# ============================================================================
# Testes automatizados. OPT-IN: nao entram no build normal.
#
#   cmake -B build -S . -DPRISM_BUILD_TESTS=ON
#   cmake --build build --target PrismAssetTests
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
