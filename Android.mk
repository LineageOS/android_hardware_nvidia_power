# Copyright (C) 2015 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq ($(TARGET_POWERHAL_VARIANT),tegra)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

# This is only for devices that can contain a sata hard drive, currently only foster
ifneq ($(filter foster,$(TARGET_DEVICE)),)
LOCAL_CFLAGS += -DENABLE_SATA_STANDBY_MODE
endif

# Any devices with a old interactive governor
ifeq ($(TARGET_TEGRA_VERSION),t114)
    LOCAL_CFLAGS += -DPOWER_MODE_LEGACY
endif

ifneq ($(filter $(TARGET_DEVICE), t210 t186),)
    LOCAL_CFLAGS += -DPOWER_MODE_SET_INTERACTIVE
endif

LOCAL_SRC_FILES := \
    nvpowerhal.cpp \
    timeoutpoker.cpp \
    powerhal.cpp \
    powerhal_utils.cpp \
    tegra_sata_hal.cpp \
    Power.cpp \
    service.cpp

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libdl \
    libhidlbase \
    libhidltransport \
    libhardware \
    libutils \
    android.hardware.power@1.0

LOCAL_MODULE := android.hardware.power@1.0-service-nvidia
LOCAL_INIT_RC := android.hardware.power@1.0-service-nvidia.rc
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_OWNER := nvidia
include $(BUILD_EXECUTABLE)

endif # TARGET_POWERHAL_VARIANT == tegra
