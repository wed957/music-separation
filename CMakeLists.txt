cmake_minimum_required(VERSION 3.20)

project(UARMusic VERSION 1.0.0 LANGUAGES CXX)

# 依赖目录不写死在源码中，便于在干净的工作区或 CI 中构建。
set(UAR_ORT_ROOT "" CACHE PATH
    "ONNX Runtime 1.20.1 Windows x64 SDK 根目录")
if(NOT UAR_ORT_ROOT)
    set(UAR_ORT_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
endif()

set(UAR_MODEL_FILE "" CACHE FILEPATH
    "内置音频处理模型 mel_band_roformer_vocals.onnx 的路径")
if(NOT UAR_MODEL_FILE)
    set(UAR_MODEL_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/models/mel_band_roformer_vocals.onnx")
endif()

set(UAR_ORT_INCLUDE_DIR "${UAR_ORT_ROOT}/include")
set(UAR_ORT_LIBRARY_DIR "${UAR_ORT_ROOT}/lib")
set(UAR_ORT_LIBRARY "${UAR_ORT_LIBRARY_DIR}/onnxruntime.lib")
set(UAR_ORT_RUNTIME_DIR "${UAR_ORT_ROOT}/bin")
if(NOT EXISTS "${UAR_ORT_RUNTIME_DIR}/onnxruntime.dll")
    # 官方压缩包把 DLL 和导入库都放在 lib；仓库内的裁剪版按 bin/lib 分类。
    set(UAR_ORT_RUNTIME_DIR "${UAR_ORT_LIBRARY_DIR}")
endif()
if(NOT EXISTS "${UAR_ORT_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "找不到 ONNX Runtime 头文件，请设置 -DUAR_ORT_ROOT=<SDK目录>")
endif()
if(NOT EXISTS "${UAR_ORT_LIBRARY}")
    message(FATAL_ERROR
        "找不到 ONNX Runtime 导入库，请设置 -DUAR_ORT_ROOT=<SDK目录>")
endif()
if(NOT EXISTS "${UAR_ORT_RUNTIME_DIR}/onnxruntime.dll")
    message(FATAL_ERROR
        "找不到 onnxruntime.dll，请确认 SDK 目录完整")
endif()
if(NOT EXISTS "${UAR_ORT_RUNTIME_DIR}/onnxruntime_providers_shared.dll")
    message(FATAL_ERROR
        "找不到 onnxruntime_providers_shared.dll，请确认 SDK 目录完整")
endif()
if(NOT EXISTS "${UAR_MODEL_FILE}")
    message(FATAL_ERROR
        "找不到音频处理模型，请设置 -DUAR_MODEL_FILE=<模型文件>")
endif()

add_executable(UARMusic WIN32
    src/main.cpp
    src/audio_engine.cpp
    src/separation_engine.cpp
    resources/app.rc
    resources/app.manifest
)

target_compile_features(UARMusic PRIVATE cxx_std_17)
target_compile_definitions(UARMusic PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

if(MINGW)
    target_compile_options(UARMusic PRIVATE -Wall -Wextra -Wpedantic -municode)
    target_link_options(UARMusic PRIVATE
        -municode
        -static-libgcc
        -static-libstdc++
        $<$<CONFIG:Release>:-s>
    )
endif()

target_include_directories(UARMusic PRIVATE "${UAR_ORT_INCLUDE_DIR}")
target_link_directories(UARMusic PRIVATE "${UAR_ORT_LIBRARY_DIR}")
target_link_libraries(UARMusic PRIVATE
    user32
    gdi32
    comdlg32
    ole32
    shell32
    dwmapi
    uxtheme
    msimg32
    bcrypt
    "${UAR_ORT_LIBRARY}"
)

add_custom_command(TARGET UARMusic POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${UAR_ORT_RUNTIME_DIR}/onnxruntime.dll"
        "$<TARGET_FILE_DIR:UARMusic>/onnxruntime.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${UAR_ORT_RUNTIME_DIR}/onnxruntime_providers_shared.dll"
        "$<TARGET_FILE_DIR:UARMusic>/onnxruntime_providers_shared.dll"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:UARMusic>/models"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${UAR_MODEL_FILE}"
        "$<TARGET_FILE_DIR:UARMusic>/models/mel_band_roformer_vocals.onnx"
)

set_target_properties(UARMusic PROPERTIES
    OUTPUT_NAME "UARMusic"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS UARMusic RUNTIME DESTINATION .)
install(FILES
    "${UAR_ORT_RUNTIME_DIR}/onnxruntime.dll"
    "${UAR_ORT_RUNTIME_DIR}/onnxruntime_providers_shared.dll"
    DESTINATION .
)
install(FILES "${UAR_MODEL_FILE}"
    DESTINATION models
    RENAME mel_band_roformer_vocals.onnx
)
