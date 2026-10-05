cmake_minimum_required(VERSION 3.14)
project(HPL2LlamaBackend LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "HPL2 llama.cpp integration requires a 64-bit build")
endif()

# Keep this embedded build self-contained, with no programs or model downloads.
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
set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(GGML_LLAMAFILE OFF CACHE BOOL "" FORCE)
set(GGML_CPU ON CACHE BOOL "" FORCE)
set(GGML_CPU_ALL_VARIANTS OFF CACHE BOOL "" FORCE)
set(GGML_CUDA OFF CACHE BOOL "" FORCE)
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
add_subdirectory(../sources/llama.cpp upstream)

add_library(hpl2_llama_backend INTERFACE)
target_link_libraries(hpl2_llama_backend INTERFACE mtmd llama)
target_compile_definitions(hpl2_llama_backend INTERFACE HPL2_WITH_LLAMA=1)

# MSBuild consumes one static archive through HPL2's librarian. CMake users
# link the interface target above and retain upstream's transitive libraries.
if(MSVC)
    set(HPL2_LLAMA_ARCHIVE_DIR "${CMAKE_CURRENT_BINARY_DIR}/lib" CACHE PATH "Merged MSBuild archive directory")
    add_custom_target(HPL2LlamaArchive
        COMMAND ${CMAKE_COMMAND} -E make_directory "${HPL2_LLAMA_ARCHIVE_DIR}/$<CONFIG>"
        COMMAND "${CMAKE_AR}" /NOLOGO
            "/OUT:${HPL2_LLAMA_ARCHIVE_DIR}/$<CONFIG>/HPL2Llama.lib"
            "$<TARGET_FILE:mtmd>" "$<TARGET_FILE:llama>"
            "$<TARGET_FILE:ggml>" "$<TARGET_FILE:ggml-base>"
            "$<TARGET_FILE:ggml-cpu>" "$<TARGET_FILE:vendor-hash>"
        DEPENDS mtmd llama ggml ggml-base ggml-cpu vendor-hash
        VERBATIM)
endif()
