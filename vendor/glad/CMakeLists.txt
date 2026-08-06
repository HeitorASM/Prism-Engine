# glad - loader de funcoes OpenGL 4.5 Core, gerado a partir da especificacao
# oficial do Khronos Group (via glad2, https://glad.dav1d.de equivalente).
# Vendorizado diretamente no repositorio (nao baixado em build) porque:
#   1. E um unico arquivo .c gerado, nao muda entre builds.
#   2. Evita depender de rede/geracao em toda maquina que compilar o projeto.
add_library(glad STATIC
    src/gl.c
)

target_include_directories(glad PUBLIC include)
