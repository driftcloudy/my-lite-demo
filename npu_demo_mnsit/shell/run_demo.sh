#!/bin/bash
cur_dir=$(pwd)

function readlinkf() {
    perl -MCwd -e 'print Cwd::abs_path shift' "$1";
}

#######################################
# Local Settings: please change accrodingly
#######################################
# ANDROID_NDK=/opt/android-ndk-r17c # For paddlelite docker
ANDROID_ABI=armeabi-v7a # arm64-v8a
ANDROID_NATIVE_API_LEVEL=android-23
if [ $ANDROID_ABI == "armeabi-v7a" ]; then
    ANDROID_NATIVE_API_LEVEL=android-24
fi

# paddle lite dir
PADDLE_LITE_DIR=$(readlinkf ../../npu_lite_libs/armeabi-v7a-log)

#######################################
# Model variables, do not change them
#######################################
# build target
TARGET_EXE=mnist_demo
# model name
MODEL_NAME=mnist_model
# model dir
MODEL_DIR=$(readlinkf ../assets/models)
# workspace
WORK_SPACE=/data/local/tmp

#######################################
# Running commands, do not change
#######################################
# push to device work space
adb shell   "rm -r ${WORK_SPACE}/*"
adb push   ${PADDLE_LITE_DIR}/lib/.     ${WORK_SPACE}
adb push   ${MODEL_DIR}/.                   ${WORK_SPACE}
adb push   build/${TARGET_EXE}           ${WORK_SPACE}
adb shell   chmod +x "${WORK_SPACE}/${TARGET_EXE}"
# define exe commands
EXE_SHELL="cd ${WORK_SPACE}; "
EXE_SHELL+="export GLOG_v=5;"
EXE_SHELL+="LD_LIBRARY_PATH=. ./${TARGET_EXE} ./${MODEL_NAME}"
echo ${EXE_SHELL}
# run
adb shell ${EXE_SHELL}
adb shell ls -l ${WORK_SPACE}

# pull optimized model
adb pull ${WORK_SPACE}/${MODEL_NAME}.nb ${MODEL_DIR}

# cp om/cfg file to model_cache_dir
EXE_SHELL="cd ${WORK_SPACE}; "
EXE_SHELL+="mkdir model_cache_dir;"
EXE_SHELL+="cp *.om ./model_cache_dir;"
EXE_SHELL+="cp *.cfg ./model_cache_dir;"
adb shell ${EXE_SHELL}
adb shell ls -l ${WORK_SPACE}/model_cache_dir
# pull from model_cache_dir
adb pull  ${WORK_SPACE}/model_cache_dir/.    ${MODEL_DIR}

# list models files
echo "ls -l ${MODEL_DIR}"
 ls -l ${MODEL_DIR}