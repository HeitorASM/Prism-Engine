# Extensao de exemplo (Prism). Gere/compile com o mesmo compilador e configuracao do editor:
#   cmake -S . -B build -DPRISM_SDK_FILE=<pasta do editor>/PrismSDK.cmake
#   cmake --build build --config Release
# O resultado vai para ./Binaries (ao lado do extension.ini).
cmake_minimum_required(VERSION 3.20)
project(HealthExtension LANGUAGES CXX)

set(PRISM_SDK_FILE "" CACHE FILEPATH "PrismSDK.cmake (fica ao lado do executavel do editor)")
if(NOT PRISM_SDK_FILE)
    message(FATAL_ERROR "Passe -DPRISM_SDK_FILE=<caminho>/PrismSDK.cmake")
endif()
include(${PRISM_SDK_FILE})

add_library(Health SHARED Health.cpp)
target_include_directories(Health PRIVATE ${PRISM_SDK_INCLUDE_DIRS})
target_compile_features(Health PRIVATE cxx_std_20)
set_target_properties(Health PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/Binaries")
foreach(CONFIG_NAME ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG_NAME} CONFIG_UPPER)
    set_target_properties(Health PROPERTIES
        LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/Binaries"
        RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${CMAKE_CURRENT_SOURCE_DIR}/Binaries")
endforeach()
if(MSVC)
    target_compile_options(Health PRIVATE /utf-8)
endif()
