find_program(BT_GLSLANG
    NAMES glslang glslangValidator
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
)

if(NOT BT_GLSLANG)
    message(WARNING "glslang not found (set VULKAN_SDK); shaders will not be compiled")
    return()
endif()

set(BT_SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY "${BT_SHADER_OUT_DIR}")

set(BT_SHADERS
    rt/hello.rgen
    rt/hello.rmiss
    rt/hello.rchit
    rt/voxel.rgen
    rt/voxel.rmiss
    rt/voxel.rchit
    rt/voxel.rint
    rt/shadow.rmiss
    rt/shadow.rahit
)

set(BT_SPIRV_OUTPUTS "")
foreach(shader IN LISTS BT_SHADERS)
    get_filename_component(shader_name "${shader}" NAME)
    set(spirv "${BT_SHADER_OUT_DIR}/${shader_name}.spv")
    add_custom_command(
        OUTPUT "${spirv}"
        COMMAND "${BT_GLSLANG}" -V --target-env vulkan1.3 -o "${spirv}"
                "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${shader}"
        COMMENT "glslang ${shader}"
        VERBATIM
    )
    list(APPEND BT_SPIRV_OUTPUTS "${spirv}")
endforeach()

add_custom_target(bricktrace-shaders ALL DEPENDS ${BT_SPIRV_OUTPUTS})
