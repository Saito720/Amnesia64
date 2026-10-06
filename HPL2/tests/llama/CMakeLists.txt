cmake_minimum_required(VERSION 3.20)
project(HPL2LlamaTests LANGUAGES C CXX)

option(HPL2_WITH_LLAMA "Compile and test the embedded llama.cpp backend" OFF)
option(HPL2_LLAMA_CUDA "Build or consume the embedded CUDA backend" OFF)
set(HPL2_PREBUILT_LIBRARY "" CACHE FILEPATH "Optional existing HPL2 static library to test as a consumer")

if(HPL2_LLAMA_CUDA AND NOT HPL2_WITH_LLAMA)
    message(FATAL_ERROR "HPL2_LLAMA_CUDA requires HPL2_WITH_LLAMA=ON")
endif()

get_filename_component(HPL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)

add_executable(hpl2_llama_tests
    LlamaInferenceTests.cpp
)
target_compile_features(hpl2_llama_tests PRIVATE cxx_std_17)
target_include_directories(hpl2_llama_tests PRIVATE "${HPL2_ROOT}/core/include")
target_link_libraries(hpl2_llama_tests PRIVATE Threads::Threads)

if(HPL2_PREBUILT_LIBRARY)
    if(NOT EXISTS "${HPL2_PREBUILT_LIBRARY}")
        message(FATAL_ERROR "HPL2_PREBUILT_LIBRARY does not exist: ${HPL2_PREBUILT_LIBRARY}")
    endif()
    target_link_libraries(hpl2_llama_tests PRIVATE "${HPL2_PREBUILT_LIBRARY}")
    target_compile_definitions(hpl2_llama_tests PRIVATE HPL2_WITH_LLAMA=$<BOOL:${HPL2_WITH_LLAMA}>)
    if(WIN32)
        # The engine archive embeds CUDA import libraries; the corresponding
        # runtime DLLs are deployed next to it by the engine's build.
        get_filename_component(prebuilt_directory "${HPL2_PREBUILT_LIBRARY}" DIRECTORY)
        set(prebuilt_runtime_files "")
        foreach(runtime cudart cublas cublasLt)
            file(GLOB runtime_files "${prebuilt_directory}/${runtime}64_*.dll")
            if(HPL2_LLAMA_CUDA AND NOT runtime_files)
                message(FATAL_ERROR "CUDA runtime ${runtime} DLL was not found next to HPL2_PREBUILT_LIBRARY: ${prebuilt_directory}")
            endif()
            list(APPEND prebuilt_runtime_files ${runtime_files})
        endforeach()
        if(prebuilt_runtime_files)
            add_custom_command(TARGET hpl2_llama_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${prebuilt_runtime_files} "$<TARGET_FILE_DIR:hpl2_llama_tests>"
                VERBATIM)
        endif()
    endif()
elseif(HPL2_WITH_LLAMA)
    target_sources(hpl2_llama_tests PRIVATE "${HPL2_ROOT}/core/sources/ai/LlamaInference.cpp")
    add_subdirectory("${HPL2_ROOT}/dependencies/llama" llama)
    target_link_libraries(hpl2_llama_tests PRIVATE hpl2_llama_backend)
    hpl2_llama_copy_runtime(hpl2_llama_tests)
else()
    target_sources(hpl2_llama_tests PRIVATE "${HPL2_ROOT}/core/sources/ai/LlamaInference.cpp")
    target_compile_definitions(hpl2_llama_tests PRIVATE HPL2_WITH_LLAMA=0)
endif()

enable_testing()
add_test(NAME hpl2_llama_service_contract COMMAND hpl2_llama_tests)
set_tests_properties(hpl2_llama_service_contract PROPERTIES TIMEOUT 60)
