cmake_minimum_required(VERSION 3.16)


###############################################################################
# Application related definitions
###############################################################################

set(TARGET_NAME         vww)

###############################################################################
# CMake pre initialization
###############################################################################

include($ENV{GAP_SDK_HOME}/utils/cmake/setup.cmake)
set(APP_MSG_PREFIX "[${BoldRed}VWW${ColourReset}]")

if (CONFIG_MOBILENET) 
  set(TRAINED_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/models/vww_96_int8.tflite)
  set(NN_WIN        96)
  set(NN_HIN        96)
  set(INPUT_IMAGE   "${CMAKE_CURRENT_SOURCE_DIR}/images/COCO_val2014_000000174838_1_96x96.ppm")
elseif (CONFIG_MIT_PROXYLESSNAS)
  set(TRAINED_MODEL ${CMAKE_CURRENT_SOURCE_DIR}/models/visual_wake_quant.tflite)
  set(INPUT_IMAGE   "${CMAKE_CURRENT_SOURCE_DIR}/images/COCO_val2014_000000174838_1_258x208.ppm")
  set(NN_WIN        258)
  set(NN_HIN        208)
endif()
message(STATUS "${APP_MSG_PREFIX} Using the ${TRAINED_MODEL}")

if(CONFIG_INFERENCE)
  message(STATUS "${APP_MSG_PREFIX} Building App in INFERENCE Mode")

  if (CONFIG_INFERENCE_RESIZER)
    set(INPUT_IMAGE         "${CMAKE_CURRENT_SOURCE_DIR}/images/COCO_val2014_000000174838_1_160x120.ppm")
    set(RESIZER_FLAG        --insert_resizer)
    set(NN_WIN        160)
    set(NN_HIN        120)
  endif()
  set(MAIN                vww.c)

elseif(CONFIG_DEMO)
  message(STATUS "${APP_MSG_PREFIX} Building App in DEMO Mode")
  set(RESIZER_FLAG        --insert_resizer)

  set(NN_WIN        160)
  set(NN_HIN        120)

  set(MAIN                vww_demo.c)

endif()
###############################################################################
# Autotiler Variables
###############################################################################

autotiler_define_gen_names(${TARGET_NAME})
set(AT_TENSORS_DIR BUILD_MODEL/weights_tensors)
autotiler_setup(${TARGET_NAME})
add_custom_command(
  OUTPUT ${AT_MODEL_PATH} ${AT_TENSORS_DIR}
  COMMENT "Generating the autotiler model"
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/nntool_generate_model.py
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/nntool_generate_model.py
           --trained_model=${TRAINED_MODEL}
           ${RESIZER_FLAG}
           --at_model_path=${AT_MODEL_PATH}
           --tensors_dir=${AT_TENSORS_DIR}
           --flash_type=${MODEL_L3_FLASH}
           --ram_type=${MODEL_L3_RAM}
)
add_custom_target(${NNTOOL_TARGET_PREFIX}clean_model
  COMMENT "clean_model: removing ${COMMON_MODEL_BUILDDIR}/${MODEL_GEN}"
  COMMAND rm -rf ${AT_MODEL_PATH}
)

###############################################################################
# Application dependencies
###############################################################################

list(APPEND TARGET_SRCS ${MAIN}
                        ${CNN_LIB}
                        ${CONFIG_MODEL_BUILDDIR}/${KERNEL_C}
                        ${CONFIG_MODEL_BUILDDIR}/Expression_Kernels.c
                        ${MODEL_EXPRESSIONS}
)

list(APPEND TARGET_INCS -I.
                        -I${TILER_INC}
                        -I${TILER_EMU_INC}
                        -I$ENV{GAP_SDK_HOME}/libs/gap_lib/include
                          ${CNN_LIB_INCLUDE}
                        -I${CMAKE_CURRENT_BINARY_DIR}/${CONFIG_MODEL_BUILDDIR}
                        -I${CMAKE_CURRENT_SOURCE_DIR}
                        -I${CMAKE_CURRENT_SOURCE_DIR}/at_model)

list(APPEND TARGET_CFLAGS   -O3)

set(FREQ_CL 370)
set(FREQ_FC 370)
set(FREQ_PE 370)

message(STATUS "Clock speeds - Cluster ${FREQ_CL} FC ${FREQ_FC} Peripheral ${FREQ_PE}")
# math(EXPR FREQ_CL ${CONFIG_FREQUENCY_CLUSTER}/1000000 OUTPUT_FORMAT DECIMAL)
# math(EXPR FREQ_FC ${CONFIG_FREQUENCY_FC}/1000000 OUTPUT_FORMAT DECIMAL)
# math(EXPR FREQ_PE ${CONFIG_FREQUENCY_PERIPH}/1000000 OUTPUT_FORMAT DECIMAL)

list(APPEND TARGET_PREPROCESSOR -DPERF
                                -DAT_MODEL_PREFIX=${MODEL_PREFIX}
                                -DSTACK_SIZE=${CONFIG_CL_MASTER_CORE_STACK_SIZE}
                                -DSLAVE_STACK_SIZE=${CONFIG_CL_SLAVE_CORE_STACK_SIZE}
                                -DFREQ_CL=${FREQ_CL}
                                -DFREQ_FC=${FREQ_FC}
                                -DFREQ_PE=${FREQ_PE}
                                -DINPUT_IMAGE=${INPUT_IMAGE}
                                -DNN_HIN=${NN_HIN}
                                -DNN_WIN=${NN_WIN}
)
if (DEFINED CI_TARGET)
  list(APPEND TARGET_PREPROCESSOR -DCI_TARGET=${CI_TARGET})
endif()

project(${TARGET_NAME} C ASM)

add_executable(${TARGET_NAME} ${TARGET_SRCS})
target_link_libraries(${TARGET_NAME} PUBLIC m)

target_compile_options(${TARGET_NAME} PUBLIC   ${TARGET_CFLAGS}
                                                ${TARGET_INCS}
                                                ${TARGET_PREPROCESSOR})

###############################################################################
# CMake post initialization
###############################################################################
setupos(${TARGET_NAME})
