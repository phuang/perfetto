/*
 * Copyright (C) 2024 The Android Open Source Project
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

#ifndef SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACK_CLASSIFICATION_H_
#define SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACK_CLASSIFICATION_H_

#include <cstddef>
#include "perfetto/base/logging.h"

namespace perfetto::trace_processor {

// The classification of a track indicates the "type of data" the track
// contains.
//
// Every track is uniquely identified by the the combination of the
// classification and a set of dimensions: classifications allow identifying a
// set of tracks with the same type of data within the whole universe of tracks
// while dimensions allow distinguishing between different tracks in that set.
enum class TrackClassification : size_t {
  // Global tracks, unique per trace.
  kTrigger,
  kInterconnect,
  kChromeLegacyGlobalInstant,

  // General tracks.
  kThread,
  kChromeProcessInstant,
  kTrackEvent,

  // Gpu tracks.
  kGpuFrequency,

  // Cpu tracks.
  kIrqCpu,
  kSoftirqCpu,
  kNapiGroCpu,
  kMaliIrqCpu,
  kFuncgraphCpu,
  kPkvmHypervisor,

  // Cpu counter tracks.
  kCpuFrequency,
  kCpuFrequencyThrottle,
  kCpuMaxFrequencyLimit,
  kCpuMinFrequencyLimit,

  kCpuIdle,
  kCpuIdleState,
  kCpuUtilization,
  kCpuCapacity,
  kCpuNumberRunning,

  // Time CPU spent in state.
  kUserTime,
  kNiceUserTime,
  kSystemModeTime,
  kIoWaitTime,
  kIrqTime,
  kSoftIrqTime,
  kCpuIdleTime,

  // Android begin.
  // Energy estimation.
  kAndroidEnergyEstimationBreakdown,
  kAndroidEnergyEstimationBreakdownPerUid,
  // GPU Work period from Android kernels.
  kAndroidGpuWorkPeriod,
  // Per-process LMK track.
  kAndroidLmk,
  // Android end.

  // Linux begin.
  kIrqCounter,
  kSoftirqCounter,
  kLinuxRuntimePowerManagement,
  kLinuxDeviceFrequency,
  // Linux end.

  // Not set. Legacy, never use for new tracks.
  // If set the classification can't be used to decide the tracks and
  // dimensions + name should be used instead. Strongly discouraged.
  kUnknown,

  // Keep last and equal to max value.
  kMax = kUnknown,
};

static inline const char* TrackClassificationToString(
    TrackClassification type) {
  switch (type) {
    case TrackClassification::kTrigger:
      return "triggers";
    case TrackClassification::kInterconnect:
      return "interconnect_events";
    case TrackClassification::kChromeLegacyGlobalInstant:
      return "legacy_chrome_global_instants";
    case TrackClassification::kThread:
      return "thread";
    case TrackClassification::kChromeProcessInstant:
      return "chrome_process_instant";
    case TrackClassification::kLinuxRuntimePowerManagement:
      return "linux_rpm";
    case TrackClassification::kLinuxDeviceFrequency:
      return "linux_device_frequency";
    case TrackClassification::kTrackEvent:
      return "track_event";
    case TrackClassification::kIrqCounter:
      return "irq_counter";
    case TrackClassification::kSoftirqCounter:
      return "softirq_counter";
    case TrackClassification::kGpuFrequency:
      return "gpu_frequency";
    case TrackClassification::kFuncgraphCpu:
      return "cpu_funcgraph";
    case TrackClassification::kIrqCpu:
      return "cpu_irq";
    case TrackClassification::kIrqTime:
      return "cpu_irq_time";
    case TrackClassification::kMaliIrqCpu:
      return "cpu_mali_irq";
    case TrackClassification::kNapiGroCpu:
      return "cpu_napi_gro";
    case TrackClassification::kSoftirqCpu:
      return "cpu_softirq";
    case TrackClassification::kSoftIrqTime:
      return "cpu_softirq_time";
    case TrackClassification::kPkvmHypervisor:
      return "pkvm_hypervisor";
    case TrackClassification::kCpuFrequency:
      return "cpu_frequency";
    case TrackClassification::kCpuFrequencyThrottle:
      return "cpu_frequency_throttle";
    case TrackClassification::kCpuMinFrequencyLimit:
      return "cpu_min_frequency_limit";
    case TrackClassification::kCpuMaxFrequencyLimit:
      return "cpu_max_frequency_limit";
    case TrackClassification::kCpuCapacity:
      return "cpu_capacity";
    case TrackClassification::kCpuIdle:
      return "cpu_idle";
    case TrackClassification::kCpuIdleTime:
      return "cpu_idle_time";
    case TrackClassification::kCpuIdleState:
      return "cpu_idle_state";
    case TrackClassification::kIoWaitTime:
      return "cpu_io_wait_time";
    case TrackClassification::kCpuNumberRunning:
      return "cpu_nr_running";
    case TrackClassification::kCpuUtilization:
      return "cpu_utilization";
    case TrackClassification::kSystemModeTime:
      return "cpu_system_mode_time";
    case TrackClassification::kUserTime:
      return "cpu_user_time";
    case TrackClassification::kNiceUserTime:
      return "cpu_nice_user_time";
    case TrackClassification::kAndroidEnergyEstimationBreakdown:
      return "android_energy_estimation_breakdown";
    case TrackClassification::kAndroidEnergyEstimationBreakdownPerUid:
      return "android_energy_estimation_breakdown_per_uid";
    case TrackClassification::kAndroidGpuWorkPeriod:
      return "android_gpu_work_period";
    case TrackClassification::kAndroidLmk:
      return "android_lmk";
    case TrackClassification::kUnknown:
      return "N/A";
  }
  PERFETTO_FATAL("For GCC");
}

}  // namespace perfetto::trace_processor

#endif  // SRC_TRACE_PROCESSOR_IMPORTERS_COMMON_TRACK_CLASSIFICATION_H_
