cmake_minimum_required(VERSION 3.20)
project(HPL2ObservationTests LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This consumer test currently uses the Windows prebuilt HPL2 libraries.")
endif()

get_filename_component(HPL2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(WORKSPACE_ROOT "${HPL2_ROOT}/.." ABSOLUTE)
set(HPL2_LIBRARY_DIRECTORY "${WORKSPACE_ROOT}/x64/Release" CACHE PATH "Directory of matching Release HPL2/dependency libraries")

add_executable(hpl2_observation_tests ObservationTests.cpp)
target_compile_features(hpl2_observation_tests PRIVATE cxx_std_17)
set_property(TARGET hpl2_observation_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_definitions(hpl2_observation_tests PRIVATE USE_SDL2 GLEW_STATIC _NEWTON_USE_LIB IL_STATIC_LIB HAVE_LIBC AL_LIBTYPE_STATIC)
target_include_directories(hpl2_observation_tests PRIVATE
    "${HPL2_ROOT}/core/include" "${HPL2_ROOT}/dependencies/include")

foreach(dependency HPL2 Newton AngelScript DevIL freealut GLEW jpeg ogg OpenAL32 png SDL2 theora vorbis zlib)
    if(NOT EXISTS "${HPL2_LIBRARY_DIRECTORY}/${dependency}.lib")
        message(FATAL_ERROR "Missing ${dependency}.lib in HPL2_LIBRARY_DIRECTORY; build the x64 Release engine first.")
    endif()
    target_link_libraries(hpl2_observation_tests PRIVATE "${HPL2_LIBRARY_DIRECTORY}/${dependency}.lib")
endforeach()
target_link_libraries(hpl2_observation_tests PRIVATE
    opengl32 dbghelp winmm setupapi Imm32 Version Avrt user32 gdi32 shell32 ole32 oleaut32 advapi32 uuid)

# Optional inference builds embed CUDA import libraries in HPL2. The tests never
# load a model; copy already deployed runtimes to satisfy loader dependencies.
file(GLOB inference_runtime_files "${HPL2_LIBRARY_DIRECTORY}/cudart64_*.dll"
    "${HPL2_LIBRARY_DIRECTORY}/cublas64_*.dll" "${HPL2_LIBRARY_DIRECTORY}/cublasLt64_*.dll")
if(inference_runtime_files)
    add_custom_command(TARGET hpl2_observation_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${inference_runtime_files}
            "$<TARGET_FILE_DIR:hpl2_observation_tests>" VERBATIM)
endif()

enable_testing()
add_test(NAME hpl2_observation_render_contract COMMAND hpl2_observation_tests)
set_tests_properties(hpl2_observation_render_contract PROPERTIES TIMEOUT 60)
