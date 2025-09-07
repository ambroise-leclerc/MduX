#!/bin/bash

# Compile GLSL shaders to SPIR-V
echo "Compiling shaders to SPIR-V..."

# Check if glslc is available (from Vulkan SDK)
if ! command -v glslc &> /dev/null; then
    echo "Error: glslc not found. Please install Vulkan SDK."
    exit 1
fi

mkdir -p shaders/compiled

# Compile vertex shader
glslc -fshader-stage=vertex shaders/cube_vertex.glsl -o shaders/compiled/cube_vertex.spv
if [ $? -eq 0 ]; then
    echo "✓ Vertex shader compiled successfully"
else
    echo "✗ Failed to compile vertex shader"
    exit 1
fi

# Compile fragment shader
glslc -fshader-stage=fragment shaders/cube_fragment.glsl -o shaders/compiled/cube_fragment.spv
if [ $? -eq 0 ]; then
    echo "✓ Fragment shader compiled successfully"
else
    echo "✗ Failed to compile fragment shader"
    exit 1
fi

# Convert SPIR-V to C++ header files
echo "Converting SPIR-V to C++ headers..."

# Function to convert binary to C++ array
spv_to_header() {
    local input_file=$1
    local output_file=$2
    local var_name=$3
    
    echo "// Auto-generated from $(basename $input_file)" > $output_file
    echo "#pragma once" >> $output_file
    echo "#include <vector>" >> $output_file
    echo "" >> $output_file
    echo "const std::vector<char> $var_name = {" >> $output_file
    
    # Use xxd to convert binary to hex array
    xxd -i < $input_file | sed 's/unsigned char stdin\[\]/char data[]/' | sed 's/unsigned int stdin_len/unsigned int data_len/' | grep -v "unsigned int" | sed 's/^/    /' >> $output_file
    
    echo "};" >> $output_file
}

spv_to_header "shaders/compiled/cube_vertex.spv" "CubeVertexShader.hpp" "cubeVertexShaderCode"
spv_to_header "shaders/compiled/cube_fragment.spv" "CubeFragmentShader.hpp" "cubeFragmentShaderCode"

echo "✓ Shader headers generated"
echo "✓ All shaders compiled successfully!"