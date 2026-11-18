cmake_minimum_required(VERSION 3.20)

project(UARMusic VERSION 1.0.1 LANGUAGES CXX)

include(CTest)

# 依赖目录不写死在源码中，便于在干净的工作区或 CI 中构建。
set(UAR_ORT_ROOT "" CACHE PATH
    "ONNX Runtime 1.20.1 Windows x64 SDK 根目录")
if(NOT UAR_ORT_ROOT)
    set(UAR_ORT_ROOT
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime")
endif()

set(UAR_MEL_MODEL_FILE "" CACHE FILEPATH
    "内置 Mel 音频处理文件的路径")
if(NOT UAR_MEL_MODEL_FILE)
    set(UAR_MEL_MODEL_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/models/mel_band_roformer_vocals.onnx")
endif()

set(UAR_BS_MODEL_FILE "" CACHE FILEPATH
    "内置 BS 音频处理文件的路径")
if(NOT UAR_BS_MODEL_FILE)
    set(UAR_BS_MODEL_FILE
        "${CMAKE_CURRENT_SOURCE_DIR}/models/bs_roformer_quantized.onnx")
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
if(NOT EXISTS "${UAR_MEL_MODEL_FILE}")
    message(FATAL_ERROR
        "找不到 Mel 音频处理文件，请设置 -DUAR_MEL_MODEL_FILE=<文件路径>")
endif()
if(NOT EXISTS "${UAR_BS_MODEL_FILE}")
    message(FATAL_ERROR
        "找不到 BS 音频处理文件，请设置 -DUAR_BS_MODEL_FILE=<文件路径>")
endif()

file(SIZE "${UAR_MEL_MODEL_FILE}" UAR_MEL_MODEL_SIZE)
file(SHA256 "${UAR_MEL_MODEL_FILE}" UAR_MEL_MODEL_SHA256)
if(NOT UAR_MEL_MODEL_SIZE EQUAL 135441533 OR
   NOT UAR_MEL_MODEL_SHA256 STREQUAL "bd42c4b3339f43b41baceb2a0204c941744f43911a47f006df5b62e560f91cc1")
    message(FATAL_ERROR "Mel 音频处理文件不完整，请执行 git lfs pull")
endif()
file(SIZE "${UAR_BS_MODEL_FILE}" UAR_BS_MODEL_SIZE)
file(SHA256 "${UAR_BS_MODEL_FILE}" UAR_BS_MODEL_SHA256)
if(NOT UAR_BS_MODEL_SIZE EQUAL 165858657 OR
   NOT UAR_BS_MODEL_SHA256 STREQUAL "b86b6224ff34d6d34c2530e0d41495b6a2c12db46acc1a7a5c7f64d2352807e9")
    message(FATAL_ERROR "BS 音频处理文件不完整，请执行 git lfs pull")
endif()

add_executable(UARMusic WIN32
    src/main.cpp
    src/lucide_icons.h
    src/audio_engine.cpp
    src/signal_processing.cpp
    src/separation_engine.cpp
    resources/app.rc
    resources/app.manifest
)

if(BUILD_TESTING)
    add_executable(signal_processing_test
        tests/signal_processing_test.cpp
        src/signal_processing.cpp
    )
    target_compile_features(signal_processing_test PRIVATE cxx_std_17)
    add_test(NAME signal_processing_round_trip COMMAND signal_processing_test)

    if(WIN32)
        add_executable(audio_engine_resampling_test
            tests/audio_engine_resampling_test.cpp
            src/audio_engine.cpp
        )
        target_compile_features(audio_engine_resampling_test PRIVATE cxx_std_17)
        target_compile_definitions(audio_engine_resampling_test PRIVATE
            UNICODE
            _UNICODE
            WIN32_LEAN_AND_MEAN
            NOMINMAX
        )
        target_link_libraries(audio_engine_resampling_test PRIVATE
            bcrypt
            mfplat
            mfreadwrite
            mfuuid
            ole32
            uuid
        )
        if(MINGW)
            target_compile_options(audio_engine_resampling_test PRIVATE
                -Wall -Wextra -Wpedantic
            )
            target_link_options(audio_engine_resampling_test PRIVATE
                -static-libgcc
                -static-libstdc++
            )
        endif()
        add_test(NAME audio_engine_resampling COMMAND audio_engine_resampling_test)
    endif()
endif()

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
    mfplat
    mfreadwrite
    mfuuid
    uuid
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
        "${UAR_MEL_MODEL_FILE}"
        "$<TARGET_FILE_DIR:UARMusic>/models/mel_band_roformer_vocals.onnx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${UAR_BS_MODEL_FILE}"
        "$<TARGET_FILE_DIR:UARMusic>/models/bs_roformer_quantized.onnx"
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
install(FILES "${UAR_MEL_MODEL_FILE}"
    DESTINATION models
    RENAME mel_band_roformer_vocals.onnx
)
install(FILES "${UAR_BS_MODEL_FILE}"
    DESTINATION models
    RENAME bs_roformer_quantized.onnx
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION .
)
install(FILES
    "${UAR_ORT_ROOT}/LICENSE"
    "${UAR_ORT_ROOT}/ThirdPartyNotices.txt"
    DESTINATION licenses/onnxruntime
)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/lucide/LICENSE"
    DESTINATION licenses/lucide
)
