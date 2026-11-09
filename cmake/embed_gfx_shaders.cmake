# Generates a C++ source that embeds every gfx HLSL file as a raw string.
# Usage: cmake -DGFX_SHADER_DIR=<dir> -DOUTPUT=<file> -P embed_gfx_shaders.cmake

if(NOT DEFINED GFX_SHADER_DIR OR NOT DEFINED OUTPUT)
  message(FATAL_ERROR "GFX_SHADER_DIR and OUTPUT are required")
endif()

file(GLOB SHADER_FILES "${GFX_SHADER_DIR}/*.hlsl")
list(SORT SHADER_FILES)

file(WRITE "${OUTPUT}" "#include \"hikari/gfx/shader_library.hpp\"\n\n")
file(APPEND "${OUTPUT}" "namespace hikari::gfx {\n\n")
file(APPEND "${OUTPUT}" "const std::unordered_map<std::string, std::string>& embedded_shader_sources() {\n")
file(APPEND "${OUTPUT}" "    static const std::unordered_map<std::string, std::string> sources = {\n")

foreach(SHADER_FILE ${SHADER_FILES})
  get_filename_component(SHADER_NAME "${SHADER_FILE}" NAME)
  file(READ "${SHADER_FILE}" SHADER_CONTENT)
  file(APPEND "${OUTPUT}" "        {\"${SHADER_NAME}\", R\"GFXSRC(${SHADER_CONTENT})GFXSRC\"},\n")
endforeach()

file(APPEND "${OUTPUT}" "    };\n")
file(APPEND "${OUTPUT}" "    return sources;\n")
file(APPEND "${OUTPUT}" "}\n\n")
file(APPEND "${OUTPUT}" "} // namespace hikari::gfx\n")
