/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012-2015, NVIDIA CORPORATION.  All rights reserved.
 * Copyright (C) 2015 The CyanogenMod Project
 * Copyright (C) 2017 The LineageOS Project
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
#define LOG_TAG "powerHAL::common"

#include <hardware/hardware.h>
#include <hardware/power.h>

#include "powerhal_utils.h"
#include "powerhal.h"

//
// NOTE: BEWARE: These enumerations are duplicated from phs.h because phs.h
// isn't available in customer builds.
//
// TODO: Consider implementing a binary shim library that links to libphs.so
// and that encapsulates the phs.h definitions in compile time. Then link to
// the shim from nvpowerhal and use a different API between powerhal and the
// shim to ask it send various predetermined hints.
//
typedef enum
{
    NvUsecase_generic   = 0x00000001,
    NvUsecase_graphics  = 0x00000002,
    NvUsecase_camera    = 0x00000004,
    NvUsecase_video     = 0x00000008,
    NvUsecase_Force32   = 0x7fffffff
} NvUsecase;

typedef enum
{
    NvHintType_ThroughputHint   = 0,
    NvHintType_FramerateTarget  = 1,
    NvHintType_MinCPU           = 2,
    NvHintType_MaxCPU           = 3,
    NvHintType_MinGPU           = 4,
    NvHintType_MaxGPU           = 5,
    NvHintType_LastReserved     = 63,
    NvHintType_Force32          = 0x7FFFFFFF
} NvHintType;

#define NVHINT_DEFAULT_TAG 0x00000000U

#define PHS_DEBUG

#ifdef POWER_MODE_SET_INTERACTIVE
static int get_system_power_mode(void);
static void set_interactive_governor(int mode);

#define NVCPL_HINT_MAX_PERF 0
#define NVCPL_HINT_OPT_PERF 1
#define NVCPL_HINT_BAT_SAVE 4
#define NVCPL_HINT_COUNT 4

static const interactive_data_t interactive_data_array[] =
{
    { "1122000", "65 304000:75 1122000:80", "19000", "20000", "0", "41000", "90" },
    { "1020000", "65 256000:75 1020000:80", "19000", "20000", "0", "30000", "99" },
    { "640000", "65 256000:75 640000:80", "80000", "20000", "2", "30000", "99" },
    { "1020000", "65 256000:75 1020000:80", "19000", "20000", "0", "30000", "99" },
    { "420000", "80",                     "80000", "300000","2", "30000", "99" }
};
#endif

// CPU/EMC ratio table source sysfs
#define CPU_EMC_RATIO_SRC_NODE "/sys/kernel/tegra_cpu_emc/table_src"

static const int VSyncActiveBoostFreq = 300000;

static int get_input_count(void)
{
    int i = 0;
    int ret;
    char path[80];
    char name[32];

    while(1)
    {
        snprintf(path, sizeof(path), "/sys/class/input/input%d/name", i);
        ret = access(path, F_OK);
        if (ret < 0)
            break;
        memset(name, 0, 32);
        sysfs_read(path, name, 32);
        ALOGI("input device id:%d present with name:%s", i++, name);
    }
    return i;
}

static void find_input_device_ids(struct powerhal_info *pInfo)
{
    int i = 0;
    int status;
    int count = 0;
    char path[80];
    char name[MAX_CHARS];

    while (1)
    {
        snprintf(path, sizeof(path), "/sys/class/input/input%d/name", i);
        if (access(path, F_OK) < 0)
            break;
        else {
            memset(name, 0, MAX_CHARS);
            sysfs_read(path, name, MAX_CHARS);
            for (int j = 0; j < pInfo->input_cnt; j++) {
                status = (-1 == pInfo->input_devs[j].dev_id)
                    && (0 == strncmp(name,
                    pInfo->input_devs[j].dev_name, MAX_CHARS));
                if (status) {
                    ++count;
                    pInfo->input_devs[j].dev_id = i;
                    ALOGI("find_input_device_ids: %d %s",
                        pInfo->input_devs[j].dev_id,
                        pInfo->input_devs[j].dev_name);
                }
            }
            ++i;
        }

        if (count == pInfo->input_cnt)
            break;
    }
}

static int check_hint(struct powerhal_info *pInfo, power_hint_t hint, uint64_t *t)
{
    struct timespec ts;
    uint64_t time;

    if (hint >= MAX_POWER_HINT_COUNT) {
        ALOGE("Invalid power hint: 0x%x", hint);
        return -1;
    }

    clock_gettime(CLOCK_MONOTONIC, &ts);
    time = ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

    if (pInfo->hint_time[hint] && pInfo->hint_interval[hint] &&
        (time - pInfo->hint_time[hint] < pInfo->hint_interval[hint]))
        return -1;

    *t = time;

    return 0;
}

static void common_libphs_open (struct powerhal_info *pInfo)
{
    pInfo->libphs_handle = dlopen("libphs.so", RTLD_NOW);

    if (pInfo->libphs_handle) {
        pInfo->NvVaSendThroughputHints = (sendhints_fn_t)dlsym(pInfo->libphs_handle, "NvVaSendThroughputHints");
        pInfo->NvCancelThroughputHints = (cancelhints_fn_t)dlsym(pInfo->libphs_handle, "NvCancelThroughputHints");

        if (pInfo->NvVaSendThroughputHints && pInfo->NvCancelThroughputHints)
            return;
    }

    pInfo->NvVaSendThroughputHints = NULL;
    pInfo->NvCancelThroughputHints = NULL;
}

void common_power_open(struct powerhal_info *pInfo)
{
    int i;
    int size = 256;
    char *pch;

    if (0 == pInfo->input_devs || 0 == pInfo->input_cnt)
        pInfo->input_cnt = get_input_count();
    else
        find_input_device_ids(pInfo);

    // Initialize timeout poker
    Barrier readyToRun;
    pInfo->mTimeoutPoker = new TimeoutPoker(&readyToRun);
    readyToRun.wait();

    // Read available frequencies
    char *buf = (char*)malloc(sizeof(char) * size);
    memset(buf, 0, size);
    sysfs_read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies",
               buf, size);

    // Determine number of available frequencies
    pch = strtok(buf, " ");
    pInfo->num_available_frequencies = -1;
    while(pch != NULL)
    {
        pch = strtok(NULL, " ");
        pInfo->num_available_frequencies++;
    }

    // Store available frequencies in a lookup array
    pInfo->available_frequencies = (int*)malloc(sizeof(int) * pInfo->num_available_frequencies);
    sysfs_read("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies",
               buf, size);
    pch = strtok(buf, " ");
    for(i = 0; i < pInfo->num_available_frequencies; i++)
    {
        pInfo->available_frequencies[i] = atoi(pch);
        pch = strtok(NULL, " ");
    }

    // Store LP cluster max frequency
    sysfs_read("/sys/devices/system/cpu/cpuquiet/tegra_cpuquiet/idle_top_freq",
                buf, size);
    pInfo->lp_max_frequency = atoi(buf);

    pInfo->interaction_boost_frequency = pInfo->lp_max_frequency;
    pInfo->animation_boost_frequency = pInfo->lp_max_frequency;

    for (i = 0; i < pInfo->num_available_frequencies; i++)
    {
        if (pInfo->available_frequencies[i] >= 1326000) {
            pInfo->interaction_boost_frequency = pInfo->available_frequencies[i];
            break;
        }
    }

    for (i = 0; i < pInfo->num_available_frequencies; i++)
    {
        if (pInfo->available_frequencies[i] >= 1024000) {
            pInfo->animation_boost_frequency = pInfo->available_frequencies[i];
            break;
        }
    }

    // Store CPU0 max frequency
    sysfs_read(SYS_NODE_CPU0_MAX_FREQ, buf, size);
    pInfo->cpu0_max_frequency = atoi(buf);

    // Initialize hint intervals in usec
    //
    // Set the interaction timeout to be slightly shorter than the duration of
    // the interaction boost so that we can maintain is constantly during
    // interaction.
    pInfo->hint_interval[POWER_HINT_VSYNC] = 0;
    pInfo->hint_interval[POWER_HINT_INTERACTION] = 90000;
    pInfo->hint_interval[POWER_HINT_SET_PROFILE] = 200000;
    pInfo->hint_interval[POWER_HINT_LAUNCH] = 1500000;
    pInfo->hint_interval[POWER_HINT_VIDEO_DECODE] = 500000;
    pInfo->hint_interval[POWER_HINT_VIDEO_ENCODE] = 500000;
    pInfo->hint_interval[POWER_HINT_LOW_POWER] = 0;

    // Initialize fds
    pInfo->fds.vsync_min_cpu = -1;

    // Initialize libphs
    common_libphs_open(pInfo);

    free(buf);
}

static void set_vsync_min_cpu_freq(struct powerhal_info *pInfo, int enabled)
{
    if (enabled && pInfo->fds.vsync_min_cpu == -1) {
        pInfo->fds.vsync_min_cpu =
        pInfo->mTimeoutPoker->requestPmQos(PMQOS_CONSTRAINT_CPU_FREQ, PM_QOS_BOOST_PRIORITY, PM_QOS_DEFAULT_VALUE, VSyncActiveBoostFreq);
    } else if (!enabled && pInfo->fds.vsync_min_cpu >= 0) {
        close(pInfo->fds.vsync_min_cpu);
        pInfo->fds.vsync_min_cpu = -1;
    }

    ALOGV("%s: set min CPU floor =%i", __func__, VSyncActiveBoostFreq);
}

void common_power_init(struct powerhal_info *pInfo)
{
    common_power_open(pInfo);

    pInfo->ftrace_enable = get_property_bool("nvidia.hwc.ftrace_enable", false);

    // Boost to max frequency on initialization to decrease boot time
    pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_CPU_FREQ,
                                            PM_QOS_BOOST_PRIORITY,
                                            PM_QOS_DEFAULT_VALUE,
                                            pInfo->available_frequencies[pInfo->num_available_frequencies - 1],
                                            s2ns(15));

    pInfo->switch_cpu_emc_limit_enabled = sysfs_exists(CPU_EMC_RATIO_SRC_NODE);
}

void common_power_set_interactive(struct powerhal_info *pInfo, int on)
{
    int i;
    int dev_id;
    char path[80];
    const char* state = (0 == on)?"0":"1";

    sysfs_write("/sys/devices/platform/host1x/nvavp/boost_sclk", state);

    if (0 != pInfo) {
        for (i = 0; i < pInfo->input_cnt; i++) {
            if (0 == pInfo->input_devs)
                dev_id = i;
            else if (-1 == pInfo->input_devs[i].dev_id)
                continue;
            else
                dev_id = pInfo->input_devs[i].dev_id;
            snprintf(path, sizeof(path), "/sys/class/input/input%d/enabled", dev_id);
            if (!access(path, W_OK)) {
                if (0 == on)
                    ALOGI("Disabling input device:%d", dev_id);
                else
                    ALOGI("Enabling input device:%d", dev_id);
                sysfs_write(path, state);
            }
        }

        if(pInfo->switch_cpu_emc_limit_enabled) {
            sysfs_write_int(CPU_EMC_RATIO_SRC_NODE, on);
        }
    }

#ifdef POWER_MODE_SET_INTERACTIVE
    if (on) {
        int power_mode = -1;
        power_mode = get_system_power_mode();
        if (power_mode < NVCPL_HINT_MAX_PERF || power_mode > NVCPL_HINT_COUNT) {
            ALOGV("%s: no system power mode info, take optimized settings", __func__);
            power_mode = NVCPL_HINT_OPT_PERF;
        }
    } else {
        power_mode = NVCPL_HINT_COUNT;
    }
    set_interactive_governor(power_mode);
#elif defined(POWER_MODE_LEGACY)
    sysfs_write("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
        (on == 0)?"conservative":"interactive");
#else
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/hispeed_freq", (on == 0)?"420000":"624000");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/target_loads", (on == 0)?"45 312000:75 564000:85":"65 228000:75 624000:85");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay", (on == 0)?"80000":"19000");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/timer_rate", (on == 0)?"300000":"20000");
    sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/boost_factor", (on == 0)?"2":"0");
#endif
}

#ifdef POWER_MODE_SET_INTERACTIVE
static int get_system_power_mode(void)
{
    char value[PROPERTY_VALUE_MAX] = { 0 };
    int power_mode = -1;

    property_get("persist.sys.NV_POWER_MODE", value, "");
    if (value[0] != '\0')
    {
        power_mode = atoi(value);
    }

    if (get_property_bool("persist.sys.NV_ECO.STATE.ISECO", false))
    {
        power_mode = NVCPL_HINT_BAT_SAVE;
    }

    return power_mode;
}

static void __sysfs_write(const char *file, const char *data)
{
    if (data != NULL)
    {
        sysfs_write(file, data);
    }
}

static void set_interactive_governor(int mode)
{
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/hispeed_freq",
            interactive_data_array[mode].hispeed_freq);
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/target_loads",
            interactive_data_array[mode].target_loads);
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/above_hispeed_delay",
            interactive_data_array[mode].above_hispeed_delay);
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/timer_rate",
            interactive_data_array[mode].timer_rate);
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/boost_factor",
            interactive_data_array[mode].boost_factor);
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/min_sample_time",
            interactive_data_array[mode].min_sample_time);
    __sysfs_write("/sys/devices/system/cpu/cpufreq/interactive/go_hispeed_load",
            interactive_data_array[mode].go_hispeed_load);
}

static void set_power_mode_hint(struct powerhal_info *pInfo, int *data)
{
    int map[] = {NVCPL_HINT_BAT_SAVE, NVCPL_HINT_OPT_PERF, NVCPL_HINT_MAX_PERF, NVCPL_HINT_BIAS_SAVE, NVCPL_HINT_BIAS_PERF};
    int mode = *data;
    int status;
    char value[4] = { 0 };

    if (mode < NVCPL_HINT_MAX_PERF || mode > NVCPL_HINT_COUNT)
    {
        ALOGE("%s: invalid hint mode = %d", __func__, mode);
        return;
    }

    // only set interactive governor parameters when display on
    sysfs_read("/sys/class/backlight/pwm-backlight/brightness", value, sizeof(value));
    status = atoi(value);

    if (status)
    {
        set_interactive_governor(map[mode]);
    }

}
#endif

void common_power_hint(struct powerhal_info *pInfo,
                            power_hint_t hint, void *data)
{
    uint64_t t;

    if (!pInfo)
        return;

    if (check_hint(pInfo, hint, &t) < 0)
        return;

    switch (hint) {
    case POWER_HINT_VSYNC:
        if (data)
            set_vsync_min_cpu_freq(pInfo, *(int *)data);
        break;
    case POWER_HINT_INTERACTION:
        break;
    /*case POWER_HINT_MULTITHREAD_BOOST:
        // Boost to 4 cores
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_ONLINE_CPUS,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                4,
                                                s2ns(2));
        break;*/
    case POWER_HINT_LAUNCH:
        // Boost to 1.2Ghz dual core
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_CPU_FREQ,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                INT_MAX,
                                                ms2ns(1500));
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_ONLINE_CPUS,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                2,
                                                ms2ns(1500));
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_GPU_FREQ,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                180000,
                                                ms2ns(1500));
        pInfo->mTimeoutPoker->requestPmQosTimed("/dev/emc_freq_min",
                                                792000,
                                                ms2ns(1500));
        break;
    case POWER_HINT_VIDEO_ENCODE:
        // set minimum 2 CPU core
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_ONLINE_CPUS,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                2,
                                                s2ns(1));
        // Set minimum CPU freq to 816 MHz
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_CPU_FREQ,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                816000,
                                                s2ns(1));
        break;
    case POWER_HINT_VIDEO_DECODE:
        // set minimum 1 CPU core
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_ONLINE_CPUS,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                1,
                                                s2ns(1));
        // Set minimum CPU freq to 710 MHz
        pInfo->mTimeoutPoker->requestPmQosTimed(PMQOS_CONSTRAINT_CPU_FREQ,
                                                PM_QOS_BOOST_PRIORITY,
                                                PM_QOS_DEFAULT_VALUE,
                                                710000,
                                                s2ns(1));
        break;
    case POWER_HINT_SET_PROFILE:
#ifdef POWER_MODE_SET_INTERACTIVE
        // Set interactive governor parameters according to power mode
        set_power_mode_hint(pInfo, (int32_t *)data);
#endif
        break;
    case POWER_HINT_LOW_POWER:
#ifdef POWER_MODE_SET_INTERACTIVE
        int new_state = (data ? PROFILE_POWER_SAVE : PROFILE_BALANCED);
        set_power_mode_hint(pInfo, &new_state);
#endif
        break;
    default:
        ALOGE("Unknown power hint: 0x%x", hint);
        break;
    }

    pInfo->hint_time[hint] = t;
}
