cmake_minimum_required(VERSION 3.18)
project(HPL2LlamaBackend LANGUAGES C CXX)

option(HPL2_LLAMA_CUDA "Build the embedded CUDA backend alongside CPU support" OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "HPL2 llama.cpp integration requires a 64-bit build")
endif()

# Keep llama/ggml embedded, with no programs or model downloads.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_APP OFF CACHE BOOL "" FORCE)
set(LLAMA_BUILD_MTMD ON CACHE BOOL "" FORCE)
set(LLAMA_OPENSSL OFF CACHE BOOL "" FORCE)
set(LLAMA_SUBPROCESS OFF CACHE BOOL "" FORCE)
set(LLAMA_TOOLS_INSTALL OFF CACHE BOOL "" FORCE)
set(MTMD_VIDEO OFF CACHE BOOL "" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
set(GGML_STATIC OFF CACHE BOOL "" FORCE)
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_LLAMAFILE OFF CACHE BOOL "" FORCE)
set(GGML_CPU ON CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA ${HPL2_LLAMA_CUDA} CACHE BOOL "" FORCE)
set(GGML_HIP OFF CACHE BOOL "" FORCE)
set(GGML_METAL OFF CACHE BOOL "" FORCE)
set(GGML_VULKAN OFF CACHE BOOL "" FORCE)
set(GGML_BLAS OFF CACHE BOOL "" FORCE)
set(GGML_ACCELERATE OFF CACHE BOOL "" FORCE)
set(GGML_RPC OFF CACHE BOOL "" FORCE)
set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
foreach(backend MUSA CANN SYCL OPENVINO ET OPENCL HEXAGON ZENDNN WEBGPU ZDNN VIRTGPU VIRTGPU_BACKEND)
    set(GGML_${backend} OFF CACHE BOOL "" FORCE)
endforeach()

# GGML_NATIVE=OFF alone still enables AVX2 in upstream defaults.
foreach(instruction SSE42 AVX AVX2 AVX_VNNI BMI2 FMA F16C AVX512 AVX512_VBMI AVX512_VNNI AVX512_BF16)
    set(GGML_${instruction} OFF CACHE BOOL "" FORCE)
endforeach()

set(LLAMA_BUILD_COMMIT "50569eb87df530daff11afda229ceb9ab8e6cae8")
set(LLAMA_BUILD_NUMBER 0)
# Prevent GGML from reporting the enclosing game's Git commit as its own.
set(GIT_EXE "")
if(HPL2_LLAMA_CUDA)
    find_package(CUDAToolkit REQUIRED)
    if(MSVC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "19.50"
            AND CUDAToolkit_VERSION VERSION_LESS "13.2")
        message(FATAL_ERROR "Visual Studio 2026/v145 requires CUDA Toolkit 13.2 or newer")
    endif()
endif()
add_subdirectory(../sources/llama.cpp upstream)

add_library(hpl2_llama_backend INTERFACE)
target_link_libraries(hpl2_llama_backend INTERFACE mtmd llama)
target_compile_definitions(hpl2_llama_backend INTERFACE
    HPL2_WITH_LLAMA=1 HPL2_LLAMA_CUDA=$<BOOL:${HPL2_LLAMA_CUDA}>)

# Windows CUDA links NVIDIA runtime DLLs even though llama/ggml are static.
# Make their deployment explicit; the driver DLL belongs to the NVIDIA driver.
set(HPL2_LLAMA_RUNTIME_FILES "" CACHE INTERNAL "Embedded CUDA runtime DLLs" FORCE)
if(HPL2_LLAMA_CUDA AND WIN32)
    foreach(runtime cudart cublas cublasLt)
        file(GLOB runtime_files
            "${CUDAToolkit_BIN_DIR}/${runtime}64_*.dll"
            "${CUDAToolkit_BIN_DIR}/x64/${runtime}64_*.dll")
        if(NOT runtime_files)
            message(FATAL_ERROR "CUDA runtime ${runtime} DLL was not found in ${CUDAToolkit_BIN_DIR}")
        endif()
        list(APPEND HPL2_LLAMA_RUNTIME_FILES ${runtime_files})
    endforeach()
    set(HPL2_LLAMA_RUNTIME_FILES "${HPL2_LLAMA_RUNTIME_FILES}" CACHE INTERNAL "Embedded CUDA runtime DLLs" FORCE)
endif()

function(hpl2_llama_copy_runtime target)
    if(HPL2_LLAMA_RUNTIME_FILES)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${HPL2_LLAMA_RUNTIME_FILES} "$<TARGET_FILE_DIR:${target}>"
            VERBATIM)
    endif()
endfunction()

# MSBuild consumes one static archive through HPL2's librarian. CMake users
# link the interface target above and retain upstream's transitive libraries.
if(MSVC)
    set(HPL2_LLAMA_ARCHIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/lib" CACHE PATH "Merged MSBuild archive directory")
    set(archive_dependencies mtmd llama ggml ggml-base ggml-cpu vendor-hash)
    set(archive_inputs "")
    foreach(dependency IN LISTS archive_dependencies)
        list(APPEND archive_inputs "$<TARGET_FILE:${dependency}>")
    endforeach()
    if(HPL2_LLAMA_CUDA)
        list(APPEND archive_dependencies ggml-cuda)
        list(APPEND archive_inputs "$<TARGET_FILE:ggml-cuda>")
        # Import libraries must travel with HPL2.lib too. TARGET_LINKER_FILE
        # resolves the library used by the linker for each imported target.
        foreach(dependency CUDA::cudart CUDA::cublas CUDA::cublasLt)
            list(APPEND archive_inputs "$<TARGET_LINKER_FILE:${dependency}>")
        endforeach()
        if(NOT GGML_CUDA_NO_VMM)
            list(APPEND archive_inputs "$<TARGET_LINKER_FILE:CUDA::cuda_driver>")
        endif()
    endif()
    set(runtime_commands "")
    if(HPL2_LLAMA_RUNTIME_FILES)
        list(APPEND runtime_commands COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${HPL2_LLAMA_RUNTIME_FILES} "${HPL2_LLAMA_ARCHIVE_DIR}/$<CONFIG>")
    endif()
    add_custom_target(HPL2LlamaArchive
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HPL2_LLAMA_ARCHIVE_DIR}/$<CONFIG>"
        COMMAND "${CMAKE_AR}" /NOLOGO
            "/OUT:${HPL2_LLAMA_ARCHIVE_DIR}/$<CONFIG>/HPL2Llama.lib"
            ${archive_inputs}
        ${runtime_commands}
        DEPENDS ${archive_dependencies}
        VERBATIM)
endif()
