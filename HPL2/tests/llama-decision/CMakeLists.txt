cmake_minimum_required(VERSION 3.20)
project(HPL2LlamaDecisionTests LANGUAGES CXX)

get_filename_component(WORKSPACE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(HPL2_PREBUILT_LIBRARY "" CACHE FILEPATH "Optional llama-enabled HPL2 archive for native GBNF tests")

add_executable(hpl2_llama_decision_tests LlamaDecisionTests.cpp
    "${WORKSPACE_ROOT}/amnesia/src/game/LuxLlamaDecision.cpp")
target_compile_features(hpl2_llama_decision_tests PRIVATE cxx_std_17)
set_property(TARGET hpl2_llama_decision_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_include_directories(hpl2_llama_decision_tests PRIVATE "${WORKSPACE_ROOT}/amnesia/src/game")

if(HPL2_PREBUILT_LIBRARY)
    if(NOT EXISTS "${HPL2_PREBUILT_LIBRARY}")
        message(FATAL_ERROR "HPL2_PREBUILT_LIBRARY must be an existing matching llama-enabled archive")
    endif()
    set(LLAMA_ROOT "${WORKSPACE_ROOT}/HPL2/dependencies/sources/llama.cpp")
    target_compile_definitions(hpl2_llama_decision_tests PRIVATE LUX_LLAMA_TEST_GRAMMAR=1)
    target_include_directories(hpl2_llama_decision_tests PRIVATE
        "${LLAMA_ROOT}/src" "${LLAMA_ROOT}/include" "${LLAMA_ROOT}/ggml/include")
    target_link_libraries(hpl2_llama_decision_tests PRIVATE "${HPL2_PREBUILT_LIBRARY}")
    if(MSVC)
        # A grammar-only consumer does not pull LlamaInference.obj's CRT
        # correction for NVIDIA's driver-loader library. Match the engine's DLL CRT.
        target_link_options(hpl2_llama_decision_tests PRIVATE /NODEFAULTLIB:LIBCMT)
    endif()
    if(WIN32)
        get_filename_component(prebuilt_directory "${HPL2_PREBUILT_LIBRARY}" DIRECTORY)
        file(GLOB runtime_files "${prebuilt_directory}/cudart64_*.dll"
            "${prebuilt_directory}/cublas64_*.dll" "${prebuilt_directory}/cublasLt64_*.dll")
        if(runtime_files)
            add_custom_command(TARGET hpl2_llama_decision_tests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different ${runtime_files}
                    "$<TARGET_FILE_DIR:hpl2_llama_decision_tests>" VERBATIM)
        endif()
    endif()
endif()

enable_testing()
add_test(NAME hpl2_llama_decision_contract COMMAND hpl2_llama_decision_tests)
set_tests_properties(hpl2_llama_decision_contract PROPERTIES TIMEOUT 60)
