/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012-2015, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (C) 2015 The CyanogenMod Project
 * Copyright (C) 2017 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef COMMON_POWER_HAL_H
#define COMMON_POWER_HAL_H

#include <hardware/hardware.h>
#include <hardware/power.h>

#include "powerhal_utils.h"
#include "timeoutpoker.h"
#include <semaphore.h>

#define MAX_CHARS 32
#define MAX_INPUT_DEV_COUNT 12
#define MAX_USE_CASE_STRING_SIZE 80
#define MAX_POWER_HINT_COUNT POWER_HINT_SET_PROFILE+1

#define DEFAULT_MIN_ONLINE_CPUS     2
#define DEFAULT_MAX_ONLINE_CPUS     0
#define DEFAULT_FREQ                700

#define POWER_CAP_PROP "persist.sys.NV_PBC_PWR_LIMIT"
#define SLEEP_INTERVAL_SECS 1

//sys node control entry
#define SYS_NODE_PRISM_ENABLE           "/sys/devices/platform/host1x/tegradc.0/smartdimmer/enable"
#define SYS_NODE_CPU0_MAX_FREQ          "/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq"

//PMQOS control entry
#define PMQOS_MAX_ONLINE_CPUS           "/dev/max_online_cpus"
#define PMQOS_MAX_CPU_FREQ              "/dev/cpu_freq_max"
#define PMQOS_CONSTRAINT_CPU_FREQ       "/dev/constraint_cpu_freq"
#define PMQOS_CONSTRAINT_GPU_FREQ       "/dev/constraint_gpu_freq"
#define PMQOS_CONSTRAINT_ONLINE_CPUS    "/dev/constraint_online_cpus"

//Default value to align with kernel pm qos
#define PM_QOS_DEFAULT_VALUE		-1

#define PRISM_CONTROL_PROP              "persist.sys.didim.enable"

#define PM_QOS_BOOST_PRIORITY 35
#define PM_QOS_APP_PROFILE_PRIORITY  40

// Lineage power hints
const static power_hint_t POWER_HINT_SET_PROFILE = (power_hint_t)0x00000111;

struct input_dev_map {
    int dev_id;
    const char* dev_name;
};

typedef struct interactive_data {
    const char *hispeed_freq;
    const char *target_loads;
    const char *above_hispeed_delay;
    const char *timer_rate;
    const char *boost_factor;
    const char *min_sample_time;
    const char *go_hispeed_load;
} interactive_data_t;

enum {
    PROFILE_POWER_SAVE,
    PROFILE_BALANCED,
    PROFILE_HIGH_PERFORMANCE,
    PROFILE_BIAS_POWER,
    PROFILE_BIAS_PERFORMANCE,
    PROFILE_MAX
};

typedef int (*sendhints_fn_t)(uint32_t client_tag, ...);
typedef void (*cancelhints_fn_t)(uint32_t usecase, uint32_t client_tag);

struct powerhal_info {
    TimeoutPoker* mTimeoutPoker;

    int *available_frequencies;
    int num_available_frequencies;

    /* Maximum LP CPU frequency */
    int lp_max_frequency;

    int interaction_boost_frequency;
    int animation_boost_frequency;

    /* maximum frequency for the current cpufreq policy */
    int cpu0_max_frequency;

    bool ftrace_enable;

    /* Number of devices requesting Power HAL service */
    int input_cnt;

    /* Holds input devices */
    struct input_dev_map* input_devs;

    /* Time last hint was sent - in usec */
    uint64_t hint_time[MAX_POWER_HINT_COUNT];
    uint64_t hint_interval[MAX_POWER_HINT_COUNT];

    /* File descriptors used for hints and app profiles */
    struct {
        int vsync_min_cpu;
    } fds;

    /* PHS hint function pointers. Loaded in runtime since powerhal can't
     * depend on libphs in link time. */
    void *libphs_handle;
    sendhints_fn_t NvVaSendThroughputHints;
    cancelhints_fn_t NvCancelThroughputHints;

    /* Switching CPU/EMC freq ratio based on display state */
    bool switch_cpu_emc_limit_enabled;
};

/* Opens power hw module */
void common_power_open(struct powerhal_info *pInfo);

/* Power management setup action at startup.
 * Such as to set default cpufreq parameters.
 */
void common_power_init(struct powerhal_info *pInfo);

/* Power management action,
 * upon the system entering interactive state and ready for interaction,
 * often with UI devices
 * OR
 * non-interactive state the system appears asleep, displayi/touch usually turned off.
*/
void common_power_set_interactive(struct powerhal_info *pInfo, int on);

/* PowerHint called to pass hints on power requirements, which
 * may result in adjustment of power/performance parameters of the
 * cpufreq governor and other controls.
*/
void common_power_hint(struct powerhal_info *pInfo,
                            power_hint_t hint, void *data);

void set_power_level_floor(int on);

#endif  //COMMON_POWER_HAL_H

