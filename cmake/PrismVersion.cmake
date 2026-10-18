# ============================================================================
# PrismVersion.cmake
# Le o Version.py da raiz (fonte unica de nome/versao/build) e gera
# <build>/generated/PrismVersion.h a partir de Prism/src/Prism/Core/Version.h.in.
# O CMake reconfigura sozinho quando o Version.py muda.
#
# Para um build de CI: cmake -DPRISM_BUILD=1234 ... (sobrescreve o Build do arquivo).
# ============================================================================

set(PRISM_VERSION_FILE "${CMAKE_SOURCE_DIR}/Version.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PRISM_VERSION_FILE}")

# Padroes (usados se o arquivo ou uma chave nao existir).
set(PRISM_NAME "Prism")
set(PRISM_LONG_NAME "Prism Engine")
set(PRISM_VERSION "0.0.0")
set(PRISM_VERSION_STATE "")
set(PRISM_EDITOR_VERSION "0.0.0")
set(PRISM_BUILD "" CACHE STRING "Identificador do build (sobrescreve Build do Version.py)")

if(EXISTS "${PRISM_VERSION_FILE}")
    file(STRINGS "${PRISM_VERSION_FILE}" PRISM_VERSION_LINES ENCODING UTF-8)
    foreach(line IN LISTS PRISM_VERSION_LINES)
        # Chave = "valor"  (comentario depois do valor e ignorado)
        if(line MATCHES "^[ \t]*([A-Za-z_]+)[ \t]*=[ \t]*\"([^\"]*)\"")
            set(key "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")
            if(key STREQUAL "Name")
                set(PRISM_NAME "${value}")
            elseif(key STREQUAL "Long_name")
                set(PRISM_LONG_NAME "${value}")
            elseif(key STREQUAL "Version")
                set(PRISM_VERSION "${value}")
            elseif(key STREQUAL "Version_state")
                set(PRISM_VERSION_STATE "${value}")
            elseif(key STREQUAL "Editor_version")
                set(PRISM_EDITOR_VERSION "${value}")
            elseif(key STREQUAL "Build" AND PRISM_BUILD STREQUAL "")
                set(PRISM_BUILD "${value}")
            endif()
        endif()
    endforeach()
else()
    message(WARNING "Version.py nao encontrado em ${PRISM_VERSION_FILE}; usando versao padrao.")
endif()

configure_file(
    "${CMAKE_SOURCE_DIR}/Prism/src/Prism/Core/Version.h.in"
    "${CMAKE_BINARY_DIR}/generated/PrismVersion.h"
    @ONLY)

# Alvo INTERFACE: quem linkar (a engine, e por heranca o editor) enxerga <PrismVersion.h>.
add_library(prism_version INTERFACE)
target_include_directories(prism_version INTERFACE "${CMAKE_BINARY_DIR}/generated")

message(STATUS "${PRISM_LONG_NAME} ${PRISM_VERSION} ${PRISM_VERSION_STATE} (editor ${PRISM_EDITOR_VERSION})")
