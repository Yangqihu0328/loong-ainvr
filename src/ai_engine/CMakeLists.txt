# COMP-04: AI Engine Library

set(AI_ENGINE_SOURCES
  backend/opencv_dnn_backend.cc
  backend/backend_factory.cc
  yolo_adapter/yolov5_adapter.cc
  yolo_adapter/yolov8_adapter.cc
  yolo_adapter/yolo_adapter_factory.cc
  inference/inference_engine.cc
  model_manager/model_manager.cc
  scheduler/analysis_scheduler.cc
  cascade/model_cascade.cc
  lpr/lpr_detector_adapter.cc
  lpr/lpr_ocr_adapter.cc
  lpr/plate_store.cc
  face/face_detector_adapter.cc
  face/face_attribute_adapter.cc
  face/face_store.cc
  plugin/plugin_manager.cc
)

# Conditionally add ONNX Runtime backend
if(LOONG_HAS_ONNXRUNTIME)
  list(APPEND AI_ENGINE_SOURCES backend/onnxruntime_backend.cc)
endif()

# Conditionally add TensorRT backend
if(LOONG_HAS_TENSORRT)
  list(APPEND AI_ENGINE_SOURCES backend/tensorrt_backend.cc)
endif()

add_library(loong_ai_engine STATIC ${AI_ENGINE_SOURCES})

target_include_directories(loong_ai_engine
  PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(loong_ai_engine
  PUBLIC
    loong_core
    ${OpenCV_LIBS}
    spdlog::spdlog
    SQLite::SQLite3
    ${CMAKE_DL_LIBS}
)

# ONNX Runtime linkage
if(LOONG_HAS_ONNXRUNTIME)
  target_compile_definitions(loong_ai_engine PUBLIC LOONG_HAS_ONNXRUNTIME)
  if(onnxruntime_FOUND)
    target_link_libraries(loong_ai_engine PUBLIC onnxruntime::onnxruntime)
  else()
    target_include_directories(loong_ai_engine SYSTEM PUBLIC
      ${ONNXRUNTIME_INCLUDE_DIRS})
    target_link_libraries(loong_ai_engine PUBLIC ${ONNXRUNTIME_LIBRARIES})
  endif()

  if(LOONG_ENABLE_ONNXRUNTIME_CUDA)
    target_compile_definitions(loong_ai_engine PUBLIC LOONG_ORT_CUDA)
  endif()
endif()

# TensorRT linkage
if(LOONG_HAS_TENSORRT)
  target_compile_definitions(loong_ai_engine PUBLIC LOONG_HAS_TENSORRT)
  target_include_directories(loong_ai_engine SYSTEM PUBLIC ${TENSORRT_INCLUDE})
  target_link_libraries(loong_ai_engine PUBLIC
    ${NVINFER_LIB}
    ${NVONNXPARSER_LIB}
  )
  find_package(CUDAToolkit QUIET)
  if(CUDAToolkit_FOUND)
    target_link_libraries(loong_ai_engine PUBLIC CUDA::cudart)
  else()
    target_link_libraries(loong_ai_engine PUBLIC cudart)
  endif()
endif()
