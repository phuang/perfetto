/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include "src/trace_processor/importers/ftrace/ftrace_parser.h"

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "perfetto/base/compiler.h"
#include "perfetto/base/logging.h"
#include "perfetto/base/status.h"
#include "perfetto/ext/base/string_utils.h"
#include "perfetto/ext/base/string_view.h"
#include "perfetto/protozero/field.h"
#include "perfetto/protozero/proto_decoder.h"
#include "perfetto/protozero/proto_utils.h"
#include "perfetto/public/compiler.h"
#include "perfetto/trace_processor/basic_types.h"
#include "perfetto/trace_processor/trace_blob_view.h"
#include "src/trace_processor/importers/common/args_tracker.h"
#include "src/trace_processor/importers/common/async_track_set_tracker.h"
#include "src/trace_processor/importers/common/cpu_tracker.h"
#include "src/trace_processor/importers/common/metadata_tracker.h"
#include "src/trace_processor/importers/common/parser_types.h"
#include "src/trace_processor/importers/common/process_tracker.h"
#include "src/trace_processor/importers/common/system_info_tracker.h"
#include "src/trace_processor/importers/common/thread_state_tracker.h"
#include "src/trace_processor/importers/common/track_tracker.h"
#include "src/trace_processor/importers/common/tracks.h"
#include "src/trace_processor/importers/ftrace/binder_tracker.h"
#include "src/trace_processor/importers/ftrace/ftrace_descriptors.h"
#include "src/trace_processor/importers/ftrace/ftrace_sched_event_tracker.h"
#include "src/trace_processor/importers/ftrace/pkvm_hyp_cpu_tracker.h"
#include "src/trace_processor/importers/ftrace/v4l2_tracker.h"
#include "src/trace_processor/importers/ftrace/virtio_video_tracker.h"
#include "src/trace_processor/importers/i2c/i2c_tracker.h"
#include "src/trace_processor/importers/proto/packet_sequence_state_generation.h"
#include "src/trace_processor/importers/syscalls/syscall_tracker.h"
#include "src/trace_processor/importers/systrace/systrace_parser.h"
#include "src/trace_processor/storage/metadata.h"
#include "src/trace_processor/storage/stats.h"
#include "src/trace_processor/storage/trace_storage.h"
#include "src/trace_processor/types/softirq_action.h"
#include "src/trace_processor/types/tcp_state.h"
#include "src/trace_processor/types/variadic.h"
#include "src/trace_processor/types/version_number.h"

#include "protos/perfetto/common/gpu_counter_descriptor.pbzero.h"
#include "protos/perfetto/trace/ftrace/android_fs.pbzero.h"
#include "protos/perfetto/trace/ftrace/bcl_exynos.pbzero.h"
#include "protos/perfetto/trace/ftrace/binder.pbzero.h"
#include "protos/perfetto/trace/ftrace/cma.pbzero.h"
#include "protos/perfetto/trace/ftrace/cpuhp.pbzero.h"
#include "protos/perfetto/trace/ftrace/cros_ec.pbzero.h"
#include "protos/perfetto/trace/ftrace/dcvsh.pbzero.h"
#include "protos/perfetto/trace/ftrace/devfreq.pbzero.h"
#include "protos/perfetto/trace/ftrace/dmabuf_heap.pbzero.h"
#include "protos/perfetto/trace/ftrace/dpu.pbzero.h"
#include "protos/perfetto/trace/ftrace/fastrpc.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/ftrace_stats.pbzero.h"
#include "protos/perfetto/trace/ftrace/g2d.pbzero.h"
#include "protos/perfetto/trace/ftrace/generic.pbzero.h"
#include "protos/perfetto/trace/ftrace/google_icc_trace.pbzero.h"
#include "protos/perfetto/trace/ftrace/google_irm_trace.pbzero.h"
#include "protos/perfetto/trace/ftrace/gpu_mem.pbzero.h"
#include "protos/perfetto/trace/ftrace/i2c.pbzero.h"
#include "protos/perfetto/trace/ftrace/ion.pbzero.h"
#include "protos/perfetto/trace/ftrace/irq.pbzero.h"
#include "protos/perfetto/trace/ftrace/kgsl.pbzero.h"
#include "protos/perfetto/trace/ftrace/kmem.pbzero.h"
#include "protos/perfetto/trace/ftrace/lwis.pbzero.h"
#include "protos/perfetto/trace/ftrace/mali.pbzero.h"
#include "protos/perfetto/trace/ftrace/mdss.pbzero.h"
#include "protos/perfetto/trace/ftrace/mm_event.pbzero.h"
#include "protos/perfetto/trace/ftrace/net.pbzero.h"
#include "protos/perfetto/trace/ftrace/oom.pbzero.h"
#include "protos/perfetto/trace/ftrace/panel.pbzero.h"
#include "protos/perfetto/trace/ftrace/power.pbzero.h"
#include "protos/perfetto/trace/ftrace/raw_syscalls.pbzero.h"
#include "protos/perfetto/trace/ftrace/rpm.pbzero.h"
#include "protos/perfetto/trace/ftrace/samsung.pbzero.h"
#include "protos/perfetto/trace/ftrace/sched.pbzero.h"
#include "protos/perfetto/trace/ftrace/scm.pbzero.h"
#include "protos/perfetto/trace/ftrace/sde.pbzero.h"
#include "protos/perfetto/trace/ftrace/signal.pbzero.h"
#include "protos/perfetto/trace/ftrace/skb.pbzero.h"
#include "protos/perfetto/trace/ftrace/sock.pbzero.h"
#include "protos/perfetto/trace/ftrace/synthetic.pbzero.h"
#include "protos/perfetto/trace/ftrace/systrace.pbzero.h"
#include "protos/perfetto/trace/ftrace/task.pbzero.h"
#include "protos/perfetto/trace/ftrace/tcp.pbzero.h"
#include "protos/perfetto/trace/ftrace/trusty.pbzero.h"
#include "protos/perfetto/trace/ftrace/ufs.pbzero.h"
#include "protos/perfetto/trace/ftrace/vmscan.pbzero.h"
#include "protos/perfetto/trace/ftrace/workqueue.pbzero.h"
#include "protos/perfetto/trace/interned_data/interned_data.pbzero.h"
#include "protos/perfetto/trace/profiling/profile_common.pbzero.h"

namespace perfetto::trace_processor {

namespace {

using protos::pbzero::perfetto_pbzero_enum_KprobeEvent::KprobeType;
using protozero::ConstBytes;
using protozero::ProtoDecoder;

constexpr char kInternconnectTrackName[] = "Interconnect Events";

struct FtraceEventAndFieldId {
  uint32_t event_id;
  uint32_t field_id;
};

// Contains a list of all the proto fields in ftrace events which represent
// kernel functions. This list is used to convert the iids in these fields to
// proper kernel symbols.
// TODO(lalitm): going through this array is O(n) on a hot-path (see
// ParseTypedFtraceToRaw). Consider changing this if we end up adding a lot of
// events here.
constexpr auto kKernelFunctionFields = std::array<FtraceEventAndFieldId, 6>{
    FtraceEventAndFieldId{
        protos::pbzero::FtraceEvent::kSchedBlockedReasonFieldNumber,
        protos::pbzero::SchedBlockedReasonFtraceEvent::kCallerFieldNumber},
    FtraceEventAndFieldId{
        protos::pbzero::FtraceEvent::kWorkqueueExecuteStartFieldNumber,
        protos::pbzero::WorkqueueExecuteStartFtraceEvent::kFunctionFieldNumber},
    FtraceEventAndFieldId{
        protos::pbzero::FtraceEvent::kWorkqueueQueueWorkFieldNumber,
        protos::pbzero::WorkqueueQueueWorkFtraceEvent::kFunctionFieldNumber},
    FtraceEventAndFieldId{
        protos::pbzero::FtraceEvent::kFuncgraphEntryFieldNumber,
        protos::pbzero::FuncgraphEntryFtraceEvent::kFuncFieldNumber},
    FtraceEventAndFieldId{
        protos::pbzero::FtraceEvent::kFuncgraphExitFieldNumber,
        protos::pbzero::FuncgraphExitFtraceEvent::kFuncFieldNumber},
    FtraceEventAndFieldId{
        protos::pbzero::FtraceEvent::kMmShrinkSlabStartFieldNumber,
        protos::pbzero::MmShrinkSlabStartFtraceEvent::kShrinkFieldNumber}};

std::string GetUfsCmdString(uint32_t ufsopcode, uint32_t gid) {
  std::string buffer;
  switch (ufsopcode) {
    case 4:
      buffer = "FORMAT UNIT";
      break;
    case 18:
      buffer = "INQUIRY";
      break;
    case 85:
      buffer = "MODE SELECT (10)";
      break;
    case 90:
      buffer = "MODE SENSE (10)";
      break;
    case 52:
      buffer = "PRE-FETCH (10)";
      break;
    case 144:
      buffer = "PRE-FETCH (16)";
      break;
    case 8:
      buffer = "READ (6)";
      break;
    case 40:
      buffer = "READ (10)";
      break;
    case 136:
      buffer = "READ (16)";
      break;
    case 60:
      buffer = "READ BUFFER";
      break;
    case 37:
      buffer = "READ CAPACITY (10)";
      break;
    case 158:
      buffer = "READ CAPACITY (16)";
      break;
    case 160:
      buffer = "REPORT LUNS";
      break;
    case 3:
      buffer = "REQUEST SENSE";
      break;
    case 162:
      buffer = "SECURITY PROTOCOL IN";
      break;
    case 181:
      buffer = "SECURITY PROTOCOL OUT";
      break;
    case 29:
      buffer = "SEND DIAGNOSTIC";
      break;
    case 27:
      buffer = "START STOP UNIT";
      break;
    case 53:
      buffer = "SYNCHRONIZE CACHE (10)";
      break;
    case 145:
      buffer = "SYNCHRONIZE CACHE (16)";
      break;
    case 0:
      buffer = "TEST UNIT READY";
      break;
    case 66:
      buffer = "UNMAP";
      break;
    case 47:
      buffer = "VERIFY";
      break;
    case 10:
      buffer = "WRITE (6)";
      break;
    case 42:
      buffer = "WRITE (10)";
      break;
    case 138:
      buffer = "WRITE (16)";
      break;
    case 59:
      buffer = "WRITE BUFFER";
      break;
    default:
      buffer = "UNDEFINED";
      break;
  }
  if (gid > 0) {
    base::StackString<32> gid_str(" (GID=0x%x)", gid);
    buffer = buffer + gid_str.c_str();
  }
  return buffer;
}

enum RpmStatus {
  RPM_INVALID = -1,
  RPM_ACTIVE = 0,
  RPM_RESUMING,
  RPM_SUSPENDED,
  RPM_SUSPENDING,
};

// Obtain the string corresponding to the event code (`event` field) in the
// `device_pm_callback_start` tracepoint.
std::string GetDpmCallbackEventString(int64_t event) {
  // This mapping order is obtained directly from the Linux kernel code.
  switch (event) {
    case 0x2:
      return "suspend";
    case 0x10:
      return "resume";
    case 0x1:
      return "freeze";
    case 0x8:
      return "quiesce";
    case 0x4:
      return "hibernate";
    case 0x20:
      return "thaw";
    case 0x40:
      return "restore";
    case 0x80:
      return "recover";
    default:
      return "(unknown PM event)";
  }
}

bool StrStartsWith(const std::string& str, const std::string& prefix) {
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

// Constructs the callback phase name for device PM callback slices.
//
// Format: "<event type>[:<callback phase>]"
// Examples: suspend, suspend:late, resume:noirq etc.
std::string ConstructCallbackPhaseName(const std::string& pm_ops,
                                       const std::string& event_type) {
  std::string callback_phase = event_type;

  // The Linux kernel has a limitation where the `pm_ops` field in the
  // tracepoint is left empty if the phase is either prepare/complete.
  if (pm_ops == "") {
    if (event_type == "suspend")
      return callback_phase + ":prepare";
    else if (event_type == "resume")
      return callback_phase + ":complete";
  }

  // Extract phase (if present) for slice details.
  //
  // The `pm_ops` string may contain both callback phase and callback type, but
  // only phase is needed. A prefix match is used due to potential absence of
  // either/both phase or type in `pm_ops`.
  const std::vector<std::string> valid_phases = {"early", "late", "noirq"};
  for (const std::string& valid_phase : valid_phases) {
    if (StrStartsWith(pm_ops, valid_phase)) {
      return callback_phase + ":" + valid_phase;
    }
  }

  return callback_phase;
}

}  // namespace

FtraceParser::FtraceParser(TraceProcessorContext* context)
    : context_(context),
      rss_stat_tracker_(context),
      drm_tracker_(context),
      iostat_tracker_(context),
      virtio_gpu_tracker_(context),
      mali_gpu_event_tracker_(context),
      pkvm_hyp_cpu_tracker_(context),
      gpu_work_period_tracker_(context),
      thermal_tracker_(context),
      pixel_mm_kswapd_event_tracker_(context),
      sched_wakeup_name_id_(context->storage->InternString("sched_wakeup")),
      sched_waking_name_id_(context->storage->InternString("sched_waking")),
      cpu_id_(context->storage->InternString("cpu")),
      ucpu_id_(context->storage->InternString("ucpu")),
      linux_device_name_id_(
          context->storage->InternString("linux_device_name")),
      suspend_resume_name_id_(
          context->storage->InternString("Suspend/Resume Latency")),
      suspend_resume_minimal_name_id_(
          context->storage->InternString("Suspend/Resume Minimal")),
      suspend_resume_minimal_slice_name_id_(
          context->storage->InternString("Suspended")),
      kfree_skb_name_id_(context->storage->InternString("Kfree Skb IP Prot")),
      ion_total_id_(context->storage->InternString("mem.ion")),
      ion_change_id_(context->storage->InternString("mem.ion_change")),
      ion_buffer_id_(context->storage->InternString("mem.ion_buffer")),
      dma_heap_total_id_(context->storage->InternString("mem.dma_heap")),
      dma_heap_change_id_(
          context->storage->InternString("mem.dma_heap_change")),
      dma_buffer_id_(context->storage->InternString("mem.dma_buffer")),
      inode_arg_id_(context->storage->InternString("inode")),
      ion_total_unknown_id_(context->storage->InternString("mem.ion.unknown")),
      ion_change_unknown_id_(
          context->storage->InternString("mem.ion_change.unknown")),
      bcl_irq_id_(context_->storage->InternString("bcl_irq_id")),
      bcl_irq_throttle_(context_->storage->InternString("bcl_irq_throttle")),
      bcl_irq_cpu0_(context_->storage->InternString("bcl_irq_cpu0")),
      bcl_irq_cpu1_(context_->storage->InternString("bcl_irq_cpu1")),
      bcl_irq_cpu2_(context_->storage->InternString("bcl_irq_cpu2")),
      bcl_irq_tpu_(context_->storage->InternString("bcl_irq_tpu")),
      bcl_irq_gpu_(context_->storage->InternString("bcl_irq_gpu")),
      bcl_irq_voltage_(context_->storage->InternString("bcl_irq_voltage")),
      bcl_irq_capacity_(context_->storage->InternString("bcl_irq_capacity")),
      signal_generate_id_(context->storage->InternString("signal_generate")),
      signal_deliver_id_(context->storage->InternString("signal_deliver")),
      oom_score_adj_id_(context->storage->InternString("oom_score_adj")),
      lmk_id_(context->storage->InternString("mem.lmk")),
      comm_name_id_(context->storage->InternString("comm")),
      signal_name_id_(context_->storage->InternString("signal.sig")),
      oom_kill_id_(context_->storage->InternString("mem.oom_kill")),
      workqueue_id_(context_->storage->InternString("workqueue")),
      irq_id_(context_->storage->InternString("irq")),
      tcp_state_id_(context_->storage->InternString("tcp_state")),
      tcp_event_id_(context_->storage->InternString("tcp_event")),
      protocol_arg_id_(context_->storage->InternString("protocol")),
      napi_gro_id_(context_->storage->InternString("napi_gro")),
      tcp_retransmited_name_id_(
          context_->storage->InternString("TCP Retransmit Skb")),
      ret_arg_id_(context_->storage->InternString("ret")),
      len_arg_id_(context->storage->InternString("len")),
      direct_reclaim_nr_reclaimed_id_(
          context->storage->InternString("direct_reclaim_nr_reclaimed")),
      direct_reclaim_order_id_(
          context->storage->InternString("direct_reclaim_order")),
      direct_reclaim_may_writepage_id_(
          context->storage->InternString("direct_reclaim_may_writepage")),
      direct_reclaim_gfp_flags_id_(
          context->storage->InternString("direct_reclaim_gfp_flags")),
      vec_arg_id_(context->storage->InternString("vec")),
      gpu_mem_total_name_id_(context->storage->InternString("GPU Memory")),
      gpu_mem_total_unit_id_(context->storage->InternString(
          std::to_string(protos::pbzero::GpuCounterDescriptor::BYTE).c_str())),
      gpu_mem_total_global_desc_id_(context->storage->InternString(
          "Total GPU memory used by the entire system")),
      gpu_mem_total_proc_desc_id_(context->storage->InternString(
          "Total GPU memory used by this process")),
      io_wait_id_(context->storage->InternString("io_wait")),
      function_id_(context->storage->InternString("function")),
      waker_utid_id_(context->storage->InternString("waker_utid")),
      cros_ec_arg_num_id_(context->storage->InternString("ec_num")),
      cros_ec_arg_ec_id_(context->storage->InternString("ec_delta")),
      cros_ec_arg_sample_ts_id_(context->storage->InternString("sample_ts")),
      ufs_clkgating_id_(context->storage->InternString(
          "io.ufs.clkgating (OFF:0/REQ_OFF/REQ_ON/ON:3)")),
      ufs_command_count_id_(
          context->storage->InternString("io.ufs.command.count")),
      shrink_slab_id_(context_->storage->InternString("mm_vmscan_shrink_slab")),
      shrink_name_id_(context->storage->InternString("shrink_name")),
      shrink_total_scan_id_(context->storage->InternString("total_scan")),
      shrink_freed_id_(context->storage->InternString("freed")),
      shrink_priority_id_(context->storage->InternString("priority")),
      trusty_category_id_(context_->storage->InternString("tipc")),
      trusty_name_trusty_std_id_(context_->storage->InternString("trusty_std")),
      trusty_name_tipc_rx_id_(context_->storage->InternString("tipc_rx")),
      cma_alloc_id_(context_->storage->InternString("mm_cma_alloc")),
      cma_name_id_(context_->storage->InternString("cma_name")),
      cma_pfn_id_(context_->storage->InternString("cma_pfn")),
      cma_req_pages_id_(context_->storage->InternString("cma_req_pages")),
      cma_nr_migrated_id_(context_->storage->InternString("cma_nr_migrated")),
      cma_nr_reclaimed_id_(context_->storage->InternString("cma_nr_reclaimed")),
      cma_nr_mapped_id_(context_->storage->InternString("cma_nr_mapped")),
      cma_nr_isolate_fail_id_(
          context_->storage->InternString("cma_nr_isolate_fail")),
      cma_nr_migrate_fail_id_(
          context_->storage->InternString("cma_nr_migrate_fail")),
      cma_nr_test_fail_id_(context_->storage->InternString("cma_nr_test_fail")),
      syscall_ret_id_(context->storage->InternString("ret")),
      syscall_args_id_(context->storage->InternString("args")),
      replica_slice_id_(context->storage->InternString("replica_slice")),
      file_path_id_(context_->storage->InternString("file_path")),
      offset_id_start_(context_->storage->InternString("offset_start")),
      offset_id_end_(context_->storage->InternString("offset_end")),
      bytes_read_id_start_(context_->storage->InternString("bytes_read_start")),
      bytes_read_id_end_(context_->storage->InternString("bytes_read_end")),
      android_fs_category_id_(context_->storage->InternString("android_fs")),
      android_fs_data_read_id_(
          context_->storage->InternString("android_fs_data_read")),
      google_icc_event_id_(context->storage->InternString("google_icc_event")),
      google_irm_event_id_(context->storage->InternString("google_irm_event")),
      runtime_status_invalid_id_(
          context->storage->InternString("Invalid State")),
      runtime_status_active_id_(context->storage->InternString("Active")),
      runtime_status_suspending_id_(
          context->storage->InternString("Suspending")),
      runtime_status_resuming_id_(context->storage->InternString("Resuming")),
      suspend_resume_main_event_id_(
          context->storage->InternString("Main Kernel Suspend Event")),
      suspend_resume_device_pm_event_id_(
          context->storage->InternString("Device PM Suspend Event")),
      suspend_resume_utid_arg_name_(context->storage->InternString("utid")),
      suspend_resume_device_arg_name_(
          context->storage->InternString("device_name")),
      suspend_resume_driver_arg_name_(
          context->storage->InternString("driver_name")),
      suspend_resume_callback_phase_arg_name_(
          context->storage->InternString("callback_phase")),
      suspend_resume_event_type_arg_name_(
          context->storage->InternString("event_type")),
      device_name_id_(context->storage->InternString("device_name")) {
  // Build the lookup table for the strings inside ftrace events (e.g. the
  // name of ftrace event fields and the names of their args).
  for (size_t i = 0; i < GetDescriptorsSize(); i++) {
    auto* descriptor = GetMessageDescriptorForId(i);
    if (!descriptor->name) {
      ftrace_message_strings_.emplace_back();
      continue;
    }

    FtraceMessageStrings ftrace_strings;
    ftrace_strings.message_name_id =
        context->storage->InternString(descriptor->name);

    for (size_t fid = 0; fid <= descriptor->max_field_id; fid++) {
      const auto& field = descriptor->fields[fid];
      if (!field.name)
        continue;
      ftrace_strings.field_name_ids[fid] =
          context->storage->InternString(field.name);
    }
    ftrace_message_strings_.emplace_back(ftrace_strings);
  }

  // Array initialization causes a spurious warning due to llvm bug.
  // See https://bugs.llvm.org/show_bug.cgi?id=21629
  fast_rpc_delta_names_[0] =
      context->storage->InternString("mem.fastrpc_change[ASDP]");
  fast_rpc_delta_names_[1] =
      context->storage->InternString("mem.fastrpc_change[MDSP]");
  fast_rpc_delta_names_[2] =
      context->storage->InternString("mem.fastrpc_change[SDSP]");
  fast_rpc_delta_names_[3] =
      context->storage->InternString("mem.fastrpc_change[CDSP]");
  fast_rpc_total_names_[0] =
      context->storage->InternString("mem.fastrpc[ASDP]");
  fast_rpc_total_names_[1] =
      context->storage->InternString("mem.fastrpc[MDSP]");
  fast_rpc_total_names_[2] =
      context->storage->InternString("mem.fastrpc[SDSP]");
  fast_rpc_total_names_[3] =
      context->storage->InternString("mem.fastrpc[CDSP]");

  mm_event_counter_names_ = {
      {MmEventCounterNames(
           context->storage->InternString("mem.mm.min_flt.count"),
           context->storage->InternString("mem.mm.min_flt.max_lat"),
           context->storage->InternString("mem.mm.min_flt.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.maj_flt.count"),
           context->storage->InternString("mem.mm.maj_flt.max_lat"),
           context->storage->InternString("mem.mm.maj_flt.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.read_io.count"),
           context->storage->InternString("mem.mm.read_io.max_lat"),
           context->storage->InternString("mem.mm.read_io.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.compaction.count"),
           context->storage->InternString("mem.mm.compaction.max_lat"),
           context->storage->InternString("mem.mm.compaction.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.reclaim.count"),
           context->storage->InternString("mem.mm.reclaim.max_lat"),
           context->storage->InternString("mem.mm.reclaim.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.swp_flt.count"),
           context->storage->InternString("mem.mm.swp_flt.max_lat"),
           context->storage->InternString("mem.mm.swp_flt.avg_lat")),
       MmEventCounterNames(
           context->storage->InternString("mem.mm.kern_alloc.count"),
           context->storage->InternString("mem.mm.kern_alloc.max_lat"),
           context->storage->InternString("mem.mm.kern_alloc.avg_lat"))}};
}

base::Status FtraceParser::ParseFtraceStats(ConstBytes blob,
                                            uint32_t packet_sequence_id) {
  protos::pbzero::FtraceStats::Decoder evt(blob.data, blob.size);
  bool is_start =
      evt.phase() == protos::pbzero::FtraceStats::Phase::START_OF_TRACE;
  bool is_end = evt.phase() == protos::pbzero::FtraceStats::Phase::END_OF_TRACE;
  if (!is_start && !is_end) {
    return base::ErrStatus("Ignoring unknown ftrace stats phase %d",
                           evt.phase());
  }
  size_t phase = is_end ? 1 : 0;

  // This code relies on the fact that each ftrace_cpu_XXX_end event is
  // just after the corresponding ftrace_cpu_XXX_begin event.
  static_assert(
      stats::ftrace_cpu_read_events_end - stats::ftrace_cpu_read_events_begin ==
              1 &&
          stats::ftrace_cpu_entries_end - stats::ftrace_cpu_entries_begin == 1,
      "ftrace_cpu_XXX stats definition are messed up");

  auto* storage = context_->storage.get();
  for (auto it = evt.cpu_stats(); it; ++it) {
    protos::pbzero::FtraceCpuStats::Decoder cpu_stats(*it);
    int cpu = static_cast<int>(cpu_stats.cpu());

    int64_t entries = static_cast<int64_t>(cpu_stats.entries());
    int64_t overrun = static_cast<int64_t>(cpu_stats.overrun());
    int64_t commit_overrun = static_cast<int64_t>(cpu_stats.commit_overrun());
    int64_t bytes = static_cast<int64_t>(cpu_stats.bytes_read());
    int64_t dropped_events = static_cast<int64_t>(cpu_stats.dropped_events());
    int64_t read_events = static_cast<int64_t>(cpu_stats.read_events());
    int64_t now_ts = static_cast<int64_t>(cpu_stats.now_ts() * 1e9);

    storage->SetIndexedStats(stats::ftrace_cpu_entries_begin + phase, cpu,
                             entries);
    storage->SetIndexedStats(stats::ftrace_cpu_overrun_begin + phase, cpu,
                             overrun);
    storage->SetIndexedStats(stats::ftrace_cpu_commit_overrun_begin + phase,
                             cpu, commit_overrun);
    storage->SetIndexedStats(stats::ftrace_cpu_bytes_begin + phase, cpu, bytes);
    storage->SetIndexedStats(stats::ftrace_cpu_dropped_events_begin + phase,
                             cpu, dropped_events);
    storage->SetIndexedStats(stats::ftrace_cpu_read_events_begin + phase, cpu,
                             read_events);
    storage->SetIndexedStats(stats::ftrace_cpu_now_ts_begin + phase, cpu,
                             now_ts);

    if (is_end) {
      auto opt_entries_begin =
          storage->GetIndexedStats(stats::ftrace_cpu_entries_begin, cpu);
      if (opt_entries_begin) {
        int64_t delta_entries = entries - opt_entries_begin.value();
        storage->SetIndexedStats(stats::ftrace_cpu_entries_delta, cpu,
                                 delta_entries);
      }

      auto opt_overrun_begin =
          storage->GetIndexedStats(stats::ftrace_cpu_overrun_begin, cpu);
      if (opt_overrun_begin) {
        int64_t delta_overrun = overrun - opt_overrun_begin.value();
        storage->SetIndexedStats(stats::ftrace_cpu_overrun_delta, cpu,
                                 delta_overrun);
      }

      auto opt_commit_overrun_begin =
          storage->GetIndexedStats(stats::ftrace_cpu_commit_overrun_begin, cpu);
      if (opt_commit_overrun_begin) {
        int64_t delta_commit_overrun =
            commit_overrun - opt_commit_overrun_begin.value();
        storage->SetIndexedStats(stats::ftrace_cpu_commit_overrun_delta, cpu,
                                 delta_commit_overrun);
      }

      auto opt_bytes_begin =
          storage->GetIndexedStats(stats::ftrace_cpu_bytes_begin, cpu);
      if (opt_bytes_begin) {
        int64_t delta_bytes = bytes - opt_bytes_begin.value();
        storage->SetIndexedStats(stats::ftrace_cpu_bytes_delta, cpu,
                                 delta_bytes);
      }

      auto opt_dropped_events_begin =
          storage->GetIndexedStats(stats::ftrace_cpu_dropped_events_begin, cpu);
      if (opt_dropped_events_begin) {
        int64_t delta_dropped_events =
            dropped_events - opt_dropped_events_begin.value();
        storage->SetIndexedStats(stats::ftrace_cpu_dropped_events_delta, cpu,
                                 delta_dropped_events);
      }

      auto opt_read_events_begin =
          storage->GetIndexedStats(stats::ftrace_cpu_read_events_begin, cpu);
      if (opt_read_events_begin) {
        int64_t delta_read_events = read_events - opt_read_events_begin.value();
        storage->SetIndexedStats(stats::ftrace_cpu_read_events_delta, cpu,
                                 delta_read_events);
      }
    }

    // oldest_event_ts can often be set to very high values, possibly because
    // of wrapping. Ensure that we are not overflowing to avoid ubsan
    // complaining.
    double oldest_event_ts = cpu_stats.oldest_event_ts() * 1e9;
    // NB: This comparison is correct only because of the >=, it would be
    // incorrect with >. std::numeric_limits<int64_t>::max() converted to
    // a double is the next value representable as a double that is *larger*
    // than std::numeric_limits<int64_t>::max(). All values that are
    // representable as doubles and < than that value are thus representable
    // as int64_t.
    if (oldest_event_ts >=
        static_cast<double>(std::numeric_limits<int64_t>::max())) {
      storage->SetIndexedStats(stats::ftrace_cpu_oldest_event_ts_begin + phase,
                               cpu, std::numeric_limits<int64_t>::max());
    } else {
      storage->SetIndexedStats(stats::ftrace_cpu_oldest_event_ts_begin + phase,
                               cpu, static_cast<int64_t>(oldest_event_ts));
    }
  }

  // Compute atrace + ftrace setup errors. We do two things here:
  // 1. We add up all the errors and put the counter in the stats table (which
  //    can hold only numerals).
  // 2. We concatenate together all the errors in a string and put that in the
  //    medatata table.
  // Both will be reported in the 'Info & stats' page in the UI.
  if (is_start) {
    if (seen_errors_for_sequence_id_.count(packet_sequence_id) == 0) {
      std::string error_str;
      for (auto it = evt.failed_ftrace_events(); it; ++it) {
        storage->IncrementStats(stats::ftrace_setup_errors, 1);
        error_str += "Ftrace event failed: " + it->as_std_string() + "\n";
      }
      for (auto it = evt.unknown_ftrace_events(); it; ++it) {
        storage->IncrementStats(stats::ftrace_setup_errors, 1);
        error_str += "Ftrace event unknown: " + it->as_std_string() + "\n";
      }
      if (evt.atrace_errors().size > 0) {
        storage->IncrementStats(stats::ftrace_setup_errors, 1);
        error_str += "Atrace failures: " + evt.atrace_errors().ToStdString();
      }
      if (!error_str.empty()) {
        auto error_str_id = storage->InternString(base::StringView(error_str));
        context_->metadata_tracker->AppendMetadata(
            metadata::ftrace_setup_errors, Variadic::String(error_str_id));
        seen_errors_for_sequence_id_.insert(packet_sequence_id);
      }
    }
    if (evt.preserve_ftrace_buffer()) {
      preserve_ftrace_buffer_ = true;
    }
  }

  // Check for parsing errors such as our understanding of the ftrace ring
  // buffer ABI not matching the data read out of the kernel (while the trace
  // was being recorded). Reject such traces altogether as we need to make such
  // errors hard to ignore (most likely it's a bug in perfetto or the kernel).
  using protos::pbzero::FtraceParseStatus;
  auto error_it = evt.ftrace_parse_errors();
  if (error_it) {
    auto dev_flag =
        context_->config.dev_flags.find("ignore-ftrace-parse-errors");
    bool dev_skip_errors = dev_flag != context_->config.dev_flags.end() &&
                           dev_flag->second == "true";
    if (!dev_skip_errors) {
      std::string msg =
          "Trace was recorded with critical ftrace parsing errors, indicating "
          "a bug in Perfetto or the kernel. Please report "
          "the trace to Perfetto. If you really need to load this trace, use a "
          "native trace_processor_shell as an accelerator with these flags: "
          "\"trace_processor_shell --httpd --dev --dev-flag "
          "ignore-ftrace-parse-errors=true <trace_file.pb>\". Errors: ";
      size_t error_count = 0;
      for (; error_it; ++error_it) {
        auto error_code = static_cast<FtraceParseStatus>(*error_it);
        // Relax the strictness of zero-padded page errors, they're prevalent
        // but also do not affect the actual ftrace payload.
        // See b/329396486#comment6, b/204564312#comment20.
        if (error_code ==
            FtraceParseStatus::FTRACE_STATUS_ABI_ZERO_DATA_LENGTH) {
          context_->storage->IncrementStats(
              stats::ftrace_abi_errors_skipped_zero_data_length);
          continue;
        }
        error_count += 1;
        msg += protos::pbzero::FtraceParseStatus_Name(error_code);
        msg += ", ";
      }
      msg += "(ERR:ftrace_parse)";  // special marker for UI
      if (error_count > 0) {
        return base::Status(msg);
      }
    }
  }

  return base::OkStatus();
}

base::Status FtraceParser::ParseFtraceEvent(uint32_t cpu,
                                            int64_t ts,
                                            const TracePacketData& data) {
  MaybeOnFirstFtraceEvent();
  if (PERFETTO_UNLIKELY(ts < drop_ftrace_data_before_ts_)) {
    context_->storage->IncrementStats(
        stats::ftrace_packet_before_tracing_start);
    return base::OkStatus();
  }
  using protos::pbzero::FtraceEvent;
  const TraceBlobView& event = data.packet;
  PacketSequenceStateGeneration* seq_state = data.sequence_state.get();
  ProtoDecoder decoder(event.data(), event.length());
  uint64_t raw_pid = 0;
  bool no_pid = false;
  if (auto pid_field = decoder.FindField(FtraceEvent::kPidFieldNumber)) {
    raw_pid = pid_field.as_uint64();
  } else {
    no_pid = true;
  }
  uint32_t pid = static_cast<uint32_t>(raw_pid);

  for (auto fld = decoder.ReadField(); fld.valid(); fld = decoder.ReadField()) {
    bool is_metadata_field = fld.id() == FtraceEvent::kPidFieldNumber ||
                             fld.id() == FtraceEvent::kTimestampFieldNumber;
    if (is_metadata_field)
      continue;

    // pKVM hypervisor events are recorded as ftrace events, however they are
    // not associated with any pid. The rest of trace parsing logic for
    // hypervisor events will use the pid 0.
    if (no_pid && !PkvmHypervisorCpuTracker::IsPkvmHypervisorEvent(fld.id())) {
      return base::ErrStatus("Pid field not found in ftrace packet");
    }

    ConstBytes fld_bytes = fld.as_bytes();
    if (fld.id() == FtraceEvent::kGenericFieldNumber) {
      ParseGenericFtrace(ts, cpu, pid, fld_bytes);
    } else if (fld.id() != FtraceEvent::kSchedSwitchFieldNumber) {
      // sched_switch parsing populates the raw table by itself
      ParseTypedFtraceToRaw(fld.id(), ts, cpu, pid, fld_bytes, seq_state);
    }

    // Skip everything besides the |raw| write if we're at the start of the
    // trace and not all per-cpu buffers cover this region yet. Otherwise if
    // this event signifies a beginning of an operation that can end on a
    // different cpu, we could conclude that the operation never ends.
    // See b/192586066.
    if (PERFETTO_UNLIKELY(ts < soft_drop_ftrace_data_before_ts_)) {
      return base::OkStatus();
    }

    if (PkvmHypervisorCpuTracker::IsPkvmHypervisorEvent(fld.id())) {
      pkvm_hyp_cpu_tracker_.ParseHypEvent(cpu, ts, fld.id(), fld_bytes);
    }

    switch (fld.id()) {
      case FtraceEvent::kSchedSwitchFieldNumber: {
        ParseSchedSwitch(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSchedWakingFieldNumber: {
        ParseSchedWaking(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSchedProcessFreeFieldNumber: {
        ParseSchedProcessFree(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kCpuFrequencyFieldNumber: {
        ParseCpuFreq(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kDcvshFreqFieldNumber: {
        ParseCpuFreqThrottle(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kGpuFrequencyFieldNumber: {
        ParseGpuFreq(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kKgslGpuFrequencyFieldNumber: {
        ParseKgslGpuFreq(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kCpuIdleFieldNumber: {
        ParseCpuIdle(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kPrintFieldNumber: {
        ParsePrint(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kZeroFieldNumber: {
        ParseZero(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kRssStatThrottledFieldNumber:
      case FtraceEvent::kRssStatFieldNumber: {
        rss_stat_tracker_.ParseRssStat(ts, fld.id(), pid, fld_bytes);
        break;
      }
      case FtraceEvent::kIonHeapGrowFieldNumber: {
        ParseIonHeapGrowOrShrink(ts, pid, fld_bytes, true);
        break;
      }
      case FtraceEvent::kIonHeapShrinkFieldNumber: {
        ParseIonHeapGrowOrShrink(ts, pid, fld_bytes, false);
        break;
      }
      case FtraceEvent::kIonStatFieldNumber: {
        ParseIonStat(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kDmaHeapStatFieldNumber: {
        ParseDmaHeapStat(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSignalGenerateFieldNumber: {
        ParseSignalGenerate(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSignalDeliverFieldNumber: {
        ParseSignalDeliver(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kOomScoreAdjUpdateFieldNumber: {
        ParseOOMScoreAdjUpdate(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kMarkVictimFieldNumber: {
        ParseOOMKill(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kMmEventRecordFieldNumber: {
        ParseMmEventRecord(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSysEnterFieldNumber: {
        ParseSysEnterEvent(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSysExitFieldNumber: {
        ParseSysExitEvent(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kTaskNewtaskFieldNumber: {
        ParseTaskNewTask(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kTaskRenameFieldNumber: {
        ParseTaskRename(fld_bytes);
        break;
      }
      case FtraceEvent::kBinderTransactionFieldNumber: {
        ParseBinderTransaction(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderTransactionReceivedFieldNumber: {
        ParseBinderTransactionReceived(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderCommandFieldNumber: {
        ParseBinderCommand(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderReturnFieldNumber: {
        ParseBinderReturn(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderTransactionAllocBufFieldNumber: {
        ParseBinderTransactionAllocBuf(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderLockFieldNumber: {
        ParseBinderLock(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderUnlockFieldNumber: {
        ParseBinderUnlock(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kBinderLockedFieldNumber: {
        ParseBinderLocked(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSdeTracingMarkWriteFieldNumber: {
        ParseSdeTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kClockSetRateFieldNumber: {
        ParseClockSetRate(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kClockEnableFieldNumber: {
        ParseClockEnable(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kClockDisableFieldNumber: {
        ParseClockDisable(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kScmCallStartFieldNumber: {
        ParseScmCallStart(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kScmCallEndFieldNumber: {
        ParseScmCallEnd(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kCmaAllocStartFieldNumber: {
        ParseCmaAllocStart(ts, pid);
        break;
      }
      case FtraceEvent::kCmaAllocInfoFieldNumber: {
        ParseCmaAllocInfo(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kMmVmscanDirectReclaimBeginFieldNumber: {
        ParseDirectReclaimBegin(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kMmVmscanDirectReclaimEndFieldNumber: {
        ParseDirectReclaimEnd(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kMmShrinkSlabStartFieldNumber: {
        ParseShrinkSlabStart(ts, pid, fld_bytes, seq_state);
        break;
      }
      case FtraceEvent::kMmShrinkSlabEndFieldNumber: {
        ParseShrinkSlabEnd(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kWorkqueueExecuteStartFieldNumber: {
        ParseWorkqueueExecuteStart(cpu, ts, pid, fld_bytes, seq_state);
        break;
      }
      case FtraceEvent::kWorkqueueExecuteEndFieldNumber: {
        ParseWorkqueueExecuteEnd(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kIrqHandlerEntryFieldNumber: {
        ParseIrqHandlerEntry(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kIrqHandlerExitFieldNumber: {
        ParseIrqHandlerExit(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSoftirqEntryFieldNumber: {
        ParseSoftIrqEntry(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSoftirqExitFieldNumber: {
        ParseSoftIrqExit(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kGpuMemTotalFieldNumber: {
        ParseGpuMemTotal(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kThermalTemperatureFieldNumber: {
        thermal_tracker_.ParseThermalTemperature(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kThermalExynosAcpmBulkFieldNumber: {
        thermal_tracker_.ParseThermalExynosAcpmBulk(fld_bytes);
        break;
      }
      case FtraceEvent::kThermalExynosAcpmHighOverheadFieldNumber: {
        thermal_tracker_.ParseThermalExynosAcpmHighOverhead(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kCdevUpdateFieldNumber: {
        thermal_tracker_.ParseCdevUpdate(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSchedBlockedReasonFieldNumber: {
        ParseSchedBlockedReason(fld_bytes, seq_state);
        break;
      }
      case FtraceEvent::kFastrpcDmaStatFieldNumber: {
        ParseFastRpcDmaStat(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kG2dTracingMarkWriteFieldNumber: {
        ParseG2dTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSamsungTracingMarkWriteFieldNumber: {
        ParseSamsungTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kDpuTracingMarkWriteFieldNumber: {
        ParseDpuTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kMaliTracingMarkWriteFieldNumber: {
        ParseMaliTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kLwisTracingMarkWriteFieldNumber: {
        ParseLwisTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kVirtioGpuCmdQueueFieldNumber:
      case FtraceEvent::kVirtioGpuCmdResponseFieldNumber: {
        virtio_gpu_tracker_.ParseVirtioGpu(ts, fld.id(), pid, fld_bytes);
        break;
      }
      case FtraceEvent::kCpuhpPauseFieldNumber: {
        ParseCpuhpPause(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kNetifReceiveSkbFieldNumber: {
        ParseNetifReceiveSkb(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kNetDevXmitFieldNumber: {
        ParseNetDevXmit(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kInetSockSetStateFieldNumber: {
        ParseInetSockSetState(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kTcpRetransmitSkbFieldNumber: {
        ParseTcpRetransmitSkb(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kNapiGroReceiveEntryFieldNumber: {
        ParseNapiGroReceiveEntry(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kNapiGroReceiveExitFieldNumber: {
        ParseNapiGroReceiveExit(cpu, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kCpuFrequencyLimitsFieldNumber: {
        ParseCpuFrequencyLimits(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kKfreeSkbFieldNumber: {
        ParseKfreeSkb(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kCrosEcSensorhubDataFieldNumber: {
        ParseCrosEcSensorhubData(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kUfshcdCommandFieldNumber: {
        ParseUfshcdCommand(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kWakeupSourceActivateFieldNumber: {
        ParseWakeSourceActivate(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kWakeupSourceDeactivateFieldNumber: {
        ParseWakeSourceDeactivate(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kUfshcdClkGatingFieldNumber: {
        ParseUfshcdClkGating(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSuspendResumeFieldNumber: {
        ParseSuspendResume(ts, cpu, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kSuspendResumeMinimalFieldNumber: {
        ParseSuspendResumeMinimal(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kDrmVblankEventFieldNumber:
      case FtraceEvent::kDrmVblankEventDeliveredFieldNumber:
      case FtraceEvent::kDrmSchedJobFieldNumber:
      case FtraceEvent::kDrmRunJobFieldNumber:
      case FtraceEvent::kDrmSchedProcessJobFieldNumber:
      case FtraceEvent::kDmaFenceInitFieldNumber:
      case FtraceEvent::kDmaFenceEmitFieldNumber:
      case FtraceEvent::kDmaFenceSignaledFieldNumber:
      case FtraceEvent::kDmaFenceWaitStartFieldNumber:
      case FtraceEvent::kDmaFenceWaitEndFieldNumber: {
        drm_tracker_.ParseDrm(ts, fld.id(), pid, fld_bytes);
        break;
      }
      case FtraceEvent::kF2fsIostatFieldNumber: {
        iostat_tracker_.ParseF2fsIostat(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kF2fsIostatLatencyFieldNumber: {
        iostat_tracker_.ParseF2fsIostatLatency(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kSchedCpuUtilCfsFieldNumber: {
        ParseSchedCpuUtilCfs(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kI2cReadFieldNumber: {
        ParseI2cReadEvent(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kI2cWriteFieldNumber: {
        ParseI2cWriteEvent(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kI2cResultFieldNumber: {
        ParseI2cResultEvent(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kFuncgraphEntryFieldNumber: {
        ParseFuncgraphEntry(ts, cpu, pid, fld_bytes, seq_state);
        break;
      }
      case FtraceEvent::kFuncgraphExitFieldNumber: {
        ParseFuncgraphExit(ts, cpu, pid, fld_bytes, seq_state);
        break;
      }
      case FtraceEvent::kV4l2QbufFieldNumber:
      case FtraceEvent::kV4l2DqbufFieldNumber:
      case FtraceEvent::kVb2V4l2BufQueueFieldNumber:
      case FtraceEvent::kVb2V4l2BufDoneFieldNumber:
      case FtraceEvent::kVb2V4l2QbufFieldNumber:
      case FtraceEvent::kVb2V4l2DqbufFieldNumber: {
        V4l2Tracker::GetOrCreate(context_)->ParseV4l2Event(fld.id(), ts, pid,
                                                           fld_bytes);
        break;
      }
      case FtraceEvent::kVirtioVideoCmdFieldNumber:
      case FtraceEvent::kVirtioVideoCmdDoneFieldNumber:
      case FtraceEvent::kVirtioVideoResourceQueueFieldNumber:
      case FtraceEvent::kVirtioVideoResourceQueueDoneFieldNumber: {
        VirtioVideoTracker::GetOrCreate(context_)->ParseVirtioVideoEvent(
            fld.id(), ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustySmcFieldNumber: {
        ParseTrustySmc(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustySmcDoneFieldNumber: {
        ParseTrustySmcDone(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyStdCall32FieldNumber: {
        ParseTrustyStdCall32(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyStdCall32DoneFieldNumber: {
        ParseTrustyStdCall32Done(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyShareMemoryFieldNumber: {
        ParseTrustyShareMemory(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyShareMemoryDoneFieldNumber: {
        ParseTrustyShareMemoryDone(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyReclaimMemoryFieldNumber: {
        ParseTrustyReclaimMemory(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyReclaimMemoryDoneFieldNumber: {
        ParseTrustyReclaimMemoryDone(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIrqFieldNumber: {
        ParseTrustyIrq(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcHandleEventFieldNumber: {
        ParseTrustyIpcHandleEvent(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcConnectFieldNumber: {
        ParseTrustyIpcConnect(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcConnectEndFieldNumber: {
        ParseTrustyIpcConnectEnd(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcWriteFieldNumber: {
        ParseTrustyIpcWrite(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcReadFieldNumber: {
        ParseTrustyIpcRead(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcReadEndFieldNumber: {
        ParseTrustyIpcReadEnd(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcPollFieldNumber: {
        ParseTrustyIpcPoll(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyIpcRxFieldNumber: {
        ParseTrustyIpcRx(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kTrustyEnqueueNopFieldNumber: {
        ParseTrustyEnqueueNop(pid, ts, fld_bytes);
        break;
      }
      case FtraceEvent::kDevfreqFrequencyFieldNumber: {
        ParseDeviceFrequency(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kMaliMaliKCPUCQSSETFieldNumber:
      case FtraceEvent::kMaliMaliKCPUCQSWAITSTARTFieldNumber:
      case FtraceEvent::kMaliMaliKCPUCQSWAITENDFieldNumber:
      case FtraceEvent::kMaliMaliKCPUFENCESIGNALFieldNumber:
      case FtraceEvent::kMaliMaliKCPUFENCEWAITSTARTFieldNumber:
      case FtraceEvent::kMaliMaliKCPUFENCEWAITENDFieldNumber: {
        mali_gpu_event_tracker_.ParseMaliGpuEvent(ts, fld.id(), pid);
        break;
      }
      case FtraceEvent::kMaliMaliCSFINTERRUPTSTARTFieldNumber:
      case FtraceEvent::kMaliMaliCSFINTERRUPTENDFieldNumber: {
        mali_gpu_event_tracker_.ParseMaliGpuIrqEvent(ts, fld.id(), cpu,
                                                     fld_bytes);
        break;
      }
      case FtraceEvent::kMaliMaliPMMCUHCTLCORESDOWNSCALENOTIFYPENDFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLCORESNOTIFYPENDFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLCOREINACTIVEPENDFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLMCUONRECHECKFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLSHADERSCOREOFFPENDFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLSHADERSPENDOFFFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLSHADERSPENDONFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUHCTLSHADERSREADYOFFFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUINSLEEPFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUOFFFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONCOREATTRUPDATEPENDFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONGLBREINITPENDFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONHALTFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONHWCNTDISABLEFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONHWCNTENABLEFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONPENDHALTFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONPENDSLEEPFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUONSLEEPINITIATEFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUPENDOFFFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUPENDONRELOADFieldNumber:
      case FtraceEvent::kMaliMaliPMMCUPOWERDOWNFieldNumber:
      case FtraceEvent::kMaliMaliPMMCURESETWAITFieldNumber: {
        mali_gpu_event_tracker_.ParseMaliGpuMcuStateEvent(ts, fld.id());
        break;
      }
      case FtraceEvent::kTracingMarkWriteFieldNumber: {
        ParseMdssTracingMarkWrite(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kAndroidFsDatareadEndFieldNumber: {
        ParseAndroidFsDatareadEnd(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kAndroidFsDatareadStartFieldNumber: {
        ParseAndroidFsDatareadStart(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kGpuWorkPeriodFieldNumber: {
        gpu_work_period_tracker_.ParseGpuWorkPeriodEvent(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kRpmStatusFieldNumber: {
        ParseRpmStatus(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kPanelWriteGenericFieldNumber: {
        ParsePanelWriteGeneric(ts, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kGoogleIccEventFieldNumber: {
        ParseGoogleIccEvent(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kGoogleIrmEventFieldNumber: {
        ParseGoogleIrmEvent(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kDevicePmCallbackStartFieldNumber: {
        ParseDevicePmCallbackStart(ts, cpu, pid, fld_bytes);
        break;
      }
      case FtraceEvent::kDevicePmCallbackEndFieldNumber: {
        ParseDevicePmCallbackEnd(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kBclIrqTriggerFieldNumber: {
        ParseBclIrq(ts, fld_bytes);
        break;
      }
      case FtraceEvent::kPixelMmKswapdWakeFieldNumber: {
        pixel_mm_kswapd_event_tracker_.ParsePixelMmKswapdWake(ts, pid);
        break;
      }
      case FtraceEvent::kPixelMmKswapdDoneFieldNumber: {
        pixel_mm_kswapd_event_tracker_.ParsePixelMmKswapdDone(ts, pid,
                                                              fld_bytes);
        break;
      }
      case FtraceEvent::kKprobeEventFieldNumber: {
        ParseKprobe(ts, pid, fld_bytes);
        break;
      }
      default:
        break;
    }
  }

  PERFETTO_DCHECK(!decoder.bytes_left());
  return base::OkStatus();
}

base::Status FtraceParser::ParseInlineSchedSwitch(
    uint32_t cpu,
    int64_t ts,
    const InlineSchedSwitch& data) {
  MaybeOnFirstFtraceEvent();
  bool parse_only_into_raw = false;
  if (PERFETTO_UNLIKELY(ts < soft_drop_ftrace_data_before_ts_)) {
    parse_only_into_raw = true;
    if (ts < drop_ftrace_data_before_ts_) {
      context_->storage->IncrementStats(
          stats::ftrace_packet_before_tracing_start);
      return base::OkStatus();
    }
  }

  using protos::pbzero::FtraceEvent;
  FtraceSchedEventTracker* ftrace_sched_tracker =
      FtraceSchedEventTracker::GetOrCreate(context_);
  ftrace_sched_tracker->PushSchedSwitchCompact(
      cpu, ts, data.prev_state, static_cast<uint32_t>(data.next_pid),
      data.next_prio, data.next_comm, parse_only_into_raw);
  return base::OkStatus();
}

base::Status FtraceParser::ParseInlineSchedWaking(
    uint32_t cpu,
    int64_t ts,
    const InlineSchedWaking& data) {
  MaybeOnFirstFtraceEvent();
  bool parse_only_into_raw = false;
  if (PERFETTO_UNLIKELY(ts < soft_drop_ftrace_data_before_ts_)) {
    parse_only_into_raw = true;
    if (ts < drop_ftrace_data_before_ts_) {
      context_->storage->IncrementStats(
          stats::ftrace_packet_before_tracing_start);
      return base::OkStatus();
    }
  }

  using protos::pbzero::FtraceEvent;
  FtraceSchedEventTracker* ftrace_sched_tracker =
      FtraceSchedEventTracker::GetOrCreate(context_);
  ftrace_sched_tracker->PushSchedWakingCompact(
      cpu, ts, static_cast<uint32_t>(data.pid), data.target_cpu, data.prio,
      data.comm, data.common_flags, parse_only_into_raw);
  return base::OkStatus();
}

void FtraceParser::MaybeOnFirstFtraceEvent() {
  if (PERFETTO_LIKELY(has_seen_first_ftrace_packet_)) {
    return;
  }

  // Calculate the timestamp used to skip events that predate the time when
  // tracing started.
  DropFtraceDataBefore drop_before =
      preserve_ftrace_buffer_ ? DropFtraceDataBefore::kNoDrop
                              : context_->config.drop_ftrace_data_before;
  switch (drop_before) {
    case DropFtraceDataBefore::kNoDrop: {
      drop_ftrace_data_before_ts_ = 0;
      break;
    }
    case DropFtraceDataBefore::kAllDataSourcesStarted:
    case DropFtraceDataBefore::kTracingStarted: {
      metadata::KeyId event_key =
          drop_before == DropFtraceDataBefore::kAllDataSourcesStarted
              ? metadata::all_data_source_started_ns
              : metadata::tracing_started_ns;

      drop_ftrace_data_before_ts_ =
          context_->metadata_tracker->GetMetadata(event_key)
              .value_or(SqlValue::Long(0))
              .AsLong();
      break;
    }
  }

  // Calculate the timestamp used to skip early events, while still populating
  // the |ftrace_events| table.
  SoftDropFtraceDataBefore soft_drop_before =
      context_->config.soft_drop_ftrace_data_before;

  // TODO(b/344969928): Workaround, can be removed when perfetto v47+ traces are
  // the norm in Android.
  base::StringView unique_session_name =
      context_->metadata_tracker->GetMetadata(metadata::unique_session_name)
          .value_or(SqlValue::String(""))
          .AsString();
  if (unique_session_name ==
      base::StringView("session_with_lightweight_battery_tracing")) {
    soft_drop_before = SoftDropFtraceDataBefore::kNoDrop;
  }

  switch (soft_drop_before) {
    case SoftDropFtraceDataBefore::kNoDrop: {
      soft_drop_ftrace_data_before_ts_ = 0;
      break;
    }
    case SoftDropFtraceDataBefore::kAllPerCpuBuffersValid: {
      soft_drop_ftrace_data_before_ts_ =
          context_->metadata_tracker
              ->GetMetadata(metadata::ftrace_latest_data_start_ns)
              .value_or(SqlValue::Long(0))
              .AsLong();
      break;
    }
  }
  soft_drop_ftrace_data_before_ts_ =
      std::max(soft_drop_ftrace_data_before_ts_, drop_ftrace_data_before_ts_);

  has_seen_first_ftrace_packet_ = true;
}

void FtraceParser::ParseGenericFtrace(int64_t ts,
                                      uint32_t cpu,
                                      uint32_t tid,
                                      ConstBytes blob) {
  protos::pbzero::GenericFtraceEvent::Decoder evt(blob.data, blob.size);
  StringId event_id = context_->storage->InternString(evt.event_name());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  auto ucpu = context_->cpu_tracker->GetOrCreateCpu(cpu);
  RawId id = context_->storage->mutable_ftrace_event_table()
                 ->Insert({ts, event_id, utid, {}, {}, ucpu})
                 .id;
  auto inserter = context_->args_tracker->AddArgsTo(id);

  for (auto it = evt.field(); it; ++it) {
    protos::pbzero::GenericFtraceEvent::Field::Decoder fld(*it);
    auto field_name_id = context_->storage->InternString(fld.name());
    if (fld.has_int_value()) {
      inserter.AddArg(field_name_id, Variadic::Integer(fld.int_value()));
    } else if (fld.has_uint_value()) {
      inserter.AddArg(
          field_name_id,
          Variadic::Integer(static_cast<int64_t>(fld.uint_value())));
    } else if (fld.has_str_value()) {
      StringId str_value = context_->storage->InternString(fld.str_value());
      inserter.AddArg(field_name_id, Variadic::String(str_value));
    }
  }
}

void FtraceParser::ParseTypedFtraceToRaw(
    uint32_t ftrace_id,
    int64_t timestamp,
    uint32_t cpu,
    uint32_t tid,
    ConstBytes blob,
    PacketSequenceStateGeneration* seq_state) {
  if (PERFETTO_UNLIKELY(!context_->config.ingest_ftrace_in_raw_table))
    return;

  ProtoDecoder decoder(blob.data, blob.size);
  if (ftrace_id >= GetDescriptorsSize()) {
    PERFETTO_DLOG("Event with id: %d does not exist and cannot be parsed.",
                  ftrace_id);
    return;
  }

  FtraceMessageDescriptor* m = GetMessageDescriptorForId(ftrace_id);
  const auto& message_strings = ftrace_message_strings_[ftrace_id];
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  auto ucpu = context_->cpu_tracker->GetOrCreateCpu(cpu);
  RawId id =
      context_->storage->mutable_ftrace_event_table()
          ->Insert(
              {timestamp, message_strings.message_name_id, utid, {}, {}, ucpu})
          .id;
  auto inserter = context_->args_tracker->AddArgsTo(id);

  for (auto fld = decoder.ReadField(); fld.valid(); fld = decoder.ReadField()) {
    uint32_t field_id = fld.id();
    if (PERFETTO_UNLIKELY(field_id >= kMaxFtraceEventFields)) {
      PERFETTO_DLOG(
          "Skipping ftrace arg - proto field id is too large (%" PRIu32 ")",
          field_id);
      continue;
    }

    ProtoSchemaType type = m->fields[field_id].type;
    StringId name_id = message_strings.field_name_ids[field_id];

    // Check if this field represents a kernel function.
    const auto it = std::find_if(
        kKernelFunctionFields.begin(), kKernelFunctionFields.end(),
        [ftrace_id, field_id](const FtraceEventAndFieldId& ev) {
          return ev.event_id == ftrace_id && ev.field_id == field_id;
        });
    if (it != kKernelFunctionFields.end()) {
      PERFETTO_CHECK(type == ProtoSchemaType::kUint64);

      auto* interned_string = seq_state->LookupInternedMessage<
          protos::pbzero::InternedData::kKernelSymbolsFieldNumber,
          protos::pbzero::InternedString>(fld.as_uint64());

      // If we don't have the string for this field (can happen if
      // symbolization wasn't enabled, if reading the symbols errored out or
      // on legacy traces) then just add the field as a normal arg.
      if (interned_string) {
        protozero::ConstBytes str = interned_string->str();
        StringId str_id = context_->storage->InternString(base::StringView(
            reinterpret_cast<const char*>(str.data), str.size));
        inserter.AddArg(name_id, Variadic::String(str_id));
        continue;
      }
    }

    switch (type) {
      case ProtoSchemaType::kInt32:
      case ProtoSchemaType::kInt64:
      case ProtoSchemaType::kSfixed32:
      case ProtoSchemaType::kSfixed64:
      case ProtoSchemaType::kBool:
      case ProtoSchemaType::kEnum: {
        inserter.AddArg(name_id, Variadic::Integer(fld.as_int64()));
        break;
      }
      case ProtoSchemaType::kUint32:
      case ProtoSchemaType::kUint64:
      case ProtoSchemaType::kFixed32:
      case ProtoSchemaType::kFixed64: {
        // Note that SQLite functions will still treat unsigned values
        // as a signed 64 bit integers (but the translation back to ftrace
        // refers to this storage directly).
        inserter.AddArg(name_id, Variadic::UnsignedInteger(fld.as_uint64()));
        break;
      }
      case ProtoSchemaType::kSint32:
      case ProtoSchemaType::kSint64: {
        inserter.AddArg(name_id, Variadic::Integer(fld.as_sint64()));
        break;
      }
      case ProtoSchemaType::kString:
      case ProtoSchemaType::kBytes: {
        StringId value = context_->storage->InternString(fld.as_string());
        inserter.AddArg(name_id, Variadic::String(value));
        break;
      }
      case ProtoSchemaType::kDouble: {
        inserter.AddArg(name_id, Variadic::Real(fld.as_double()));
        break;
      }
      case ProtoSchemaType::kFloat: {
        inserter.AddArg(name_id,
                        Variadic::Real(static_cast<double>(fld.as_float())));
        break;
      }
      case ProtoSchemaType::kUnknown:
      case ProtoSchemaType::kGroup:
      case ProtoSchemaType::kMessage:
        PERFETTO_DLOG("Could not store %s as a field in args table.",
                      ProtoSchemaToString(type));
        break;
    }
  }
}

PERFETTO_ALWAYS_INLINE
void FtraceParser::ParseSchedSwitch(uint32_t cpu,
                                    int64_t timestamp,
                                    ConstBytes blob) {
  protos::pbzero::SchedSwitchFtraceEvent::Decoder ss(blob.data, blob.size);
  uint32_t prev_pid = static_cast<uint32_t>(ss.prev_pid());
  uint32_t next_pid = static_cast<uint32_t>(ss.next_pid());
  FtraceSchedEventTracker::GetOrCreate(context_)->PushSchedSwitch(
      cpu, timestamp, prev_pid, ss.prev_comm(), ss.prev_prio(), ss.prev_state(),
      next_pid, ss.next_comm(), ss.next_prio());
}

void FtraceParser::ParseKprobe(int64_t timestamp,
                               uint32_t pid,
                               ConstBytes blob) {
  protos::pbzero::KprobeEvent::Decoder kp(blob.data, blob.size);

  auto kprobe_type = static_cast<KprobeType>(kp.type());
  StringId name_id = context_->storage->InternString(kp.name());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  switch (kprobe_type) {
    case KprobeType::KPROBE_TYPE_BEGIN:
      context_->slice_tracker->Begin(timestamp, track_id,
                                     kNullStringId /* cat */, name_id);
      break;
    case KprobeType::KPROBE_TYPE_END:
      context_->slice_tracker->End(timestamp, track_id, kNullStringId /* cat */,
                                   name_id);
      break;
    case KprobeType::KPROBE_TYPE_INSTANT:
      context_->slice_tracker->Scoped(timestamp, track_id, kNullStringId,
                                      name_id, 0);
      break;
    case KprobeType::KPROBE_TYPE_UNKNOWN:
      break;
  }
}

void FtraceParser::ParseSchedWaking(int64_t timestamp,
                                    uint32_t pid,
                                    ConstBytes blob) {
  protos::pbzero::SchedWakingFtraceEvent::Decoder sw(blob.data, blob.size);
  uint32_t wakee_pid = static_cast<uint32_t>(sw.pid());
  StringId name_id = context_->storage->InternString(sw.comm());
  auto wakee_utid = context_->process_tracker->UpdateThreadName(
      wakee_pid, name_id, ThreadNamePriority::kFtrace);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  ThreadStateTracker::GetOrCreate(context_)->PushWakingEvent(timestamp,
                                                             wakee_utid, utid);
}

void FtraceParser::ParseSchedProcessFree(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::SchedProcessFreeFtraceEvent::Decoder ex(blob.data, blob.size);
  uint32_t pid = static_cast<uint32_t>(ex.pid());
  context_->process_tracker->EndThread(timestamp, pid);
}

void FtraceParser::ParseCpuFreq(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::CpuFrequencyFtraceEvent::Decoder freq(blob.data, blob.size);
  uint32_t cpu = freq.cpu_id();
  uint32_t new_freq_khz = freq.state();
  TrackId track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_frequency, cpu, TrackTracker::LegacyCharArrayName{"cpufreq"});
  context_->event_tracker->PushCounter(timestamp, new_freq_khz, track);
}

void FtraceParser::ParseCpuFreqThrottle(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::DcvshFreqFtraceEvent::Decoder freq(blob.data, blob.size);
  uint32_t cpu = static_cast<uint32_t>(freq.cpu());
  double new_freq_khz = static_cast<double>(freq.freq());
  TrackId track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_frequency_throttle, cpu,
      TrackTracker::LegacyCharArrayName{"cpufreq_throttle"});
  context_->event_tracker->PushCounter(timestamp, new_freq_khz, track);
}

void FtraceParser::ParseGpuFreq(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::GpuFrequencyFtraceEvent::Decoder freq(blob.data, blob.size);
  uint32_t gpu = freq.gpu_id();
  uint32_t new_freq = freq.state();
  TrackId track = context_->track_tracker->InternGpuCounterTrack(
      tracks::gpu_frequency, gpu, TrackTracker::LegacyCharArrayName{"gpufreq"});
  context_->event_tracker->PushCounter(timestamp, new_freq, track);
}

void FtraceParser::ParseKgslGpuFreq(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::KgslGpuFrequencyFtraceEvent::Decoder freq(blob.data,
                                                            blob.size);
  uint32_t gpu = freq.gpu_id();
  // Source data is frequency / 1000, so we correct that here:
  double new_freq = static_cast<double>(freq.gpu_freq()) * 1000.0;
  TrackId track = context_->track_tracker->InternGpuCounterTrack(
      tracks::gpu_frequency, gpu, TrackTracker::LegacyCharArrayName{"gpufreq"});
  context_->event_tracker->PushCounter(timestamp, new_freq, track);
}

void FtraceParser::ParseCpuIdle(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::CpuIdleFtraceEvent::Decoder idle(blob.data, blob.size);
  uint32_t cpu = idle.cpu_id();
  uint32_t new_state = idle.state();
  TrackId track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_idle, cpu, TrackTracker::LegacyCharArrayName{"cpuidle"});
  context_->event_tracker->PushCounter(timestamp, new_state, track);
}

void FtraceParser::ParsePrint(int64_t timestamp,
                              uint32_t pid,
                              ConstBytes blob) {
  // Atrace slices are emitted as begin/end events written into the tracefs
  // trace_marker. If we're tracing syscalls, the reconstructed atrace slice
  // would start and end in the middle of different sys_write slices (on the
  // same track). Since trace_processor enforces strict slice nesting, we need
  // to resolve this conflict. The chosen approach is to distort the data, and
  // pretend that the write syscall ended at the atrace slice's boundary.
  //
  // In other words, this true structure:
  // [write...].....[write...]
  // ....[atrace_slice..].....
  //
  // Is turned into:
  // [wr][atrace_slice..]
  // ...............[wri]
  //
  std::optional<UniqueTid> opt_utid =
      context_->process_tracker->GetThreadOrNull(pid);
  if (opt_utid) {
    SyscallTracker::GetOrCreate(context_)->MaybeTruncateOngoingWriteSlice(
        timestamp, *opt_utid);
  }

  protos::pbzero::PrintFtraceEvent::Decoder evt(blob.data, blob.size);
  SystraceParser::GetOrCreate(context_)->ParsePrintEvent(timestamp, pid,
                                                         evt.buf());
}

void FtraceParser::ParseZero(int64_t timestamp, uint32_t pid, ConstBytes blob) {
  protos::pbzero::ZeroFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseZeroEvent(
      timestamp, pid, evt.flag(), evt.name(), tgid, evt.value());
}

void FtraceParser::ParseMdssTracingMarkWrite(int64_t timestamp,
                                             uint32_t pid,
                                             ConstBytes blob) {
  protos::pbzero::TracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                           blob.size);
  if (!evt.has_trace_begin()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, 0, evt.trace_begin(), evt.trace_name(), tgid, 0);
}

void FtraceParser::ParseSdeTracingMarkWrite(int64_t timestamp,
                                            uint32_t pid,
                                            ConstBytes blob) {
  protos::pbzero::SdeTracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);
  if (!evt.has_trace_type() && !evt.has_trace_begin()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.trace_type()), evt.trace_begin(),
      evt.trace_name(), tgid, evt.value());
}

void FtraceParser::ParseSamsungTracingMarkWrite(int64_t timestamp,
                                                uint32_t pid,
                                                ConstBytes blob) {
  protos::pbzero::SamsungTracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                                  blob.size);
  if (!evt.has_trace_type()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.trace_type()),
      false /*trace_begin*/, evt.trace_name(), tgid, evt.value());
}

void FtraceParser::ParseDpuTracingMarkWrite(int64_t timestamp,
                                            uint32_t pid,
                                            ConstBytes blob) {
  protos::pbzero::DpuTracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);
  if (!evt.type()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.type()), false /*trace_begin*/,
      evt.name(), tgid, evt.value());
}

void FtraceParser::ParseG2dTracingMarkWrite(int64_t timestamp,
                                            uint32_t pid,
                                            ConstBytes blob) {
  protos::pbzero::G2dTracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);
  if (!evt.type()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.type()), false /*trace_begin*/,
      evt.name(), tgid, evt.value());
}

void FtraceParser::ParseMaliTracingMarkWrite(int64_t timestamp,
                                             uint32_t pid,
                                             ConstBytes blob) {
  protos::pbzero::MaliTracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                               blob.size);
  if (!evt.type()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.type()), false /*trace_begin*/,
      evt.name(), tgid, evt.value());
}

void FtraceParser::ParseLwisTracingMarkWrite(int64_t timestamp,
                                             uint32_t pid,
                                             ConstBytes blob) {
  protos::pbzero::LwisTracingMarkWriteFtraceEvent::Decoder evt(blob.data,
                                                               blob.size);
  if (!evt.type()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.type()), false /*trace_begin*/,
      evt.func_name(), tgid, evt.value());
}

void FtraceParser::ParseGoogleIccEvent(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::GoogleIccEventFtraceEvent::Decoder evt(blob.data, blob.size);
  TrackId track_id = context_->track_tracker->InternGlobalTrack(
      tracks::interconnect_events,
      TrackTracker::LegacyCharArrayName(kInternconnectTrackName));
  StringId slice_name_id =
      context_->storage->InternString(base::StringView(evt.event()));
  context_->slice_tracker->Scoped(timestamp, track_id, google_icc_event_id_,
                                  slice_name_id, 0);
}

void FtraceParser::ParseGoogleIrmEvent(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::GoogleIrmEventFtraceEvent::Decoder evt(blob.data, blob.size);
  TrackId track_id = context_->track_tracker->InternGlobalTrack(
      tracks::interconnect_events,
      TrackTracker::LegacyCharArrayName{kInternconnectTrackName});
  StringId slice_name_id =
      context_->storage->InternString(base::StringView(evt.event()));
  context_->slice_tracker->Scoped(timestamp, track_id, google_irm_event_id_,
                                  slice_name_id, 0);
}

/** Parses ion heap events present in Pixel kernels. */
void FtraceParser::ParseIonHeapGrowOrShrink(int64_t timestamp,
                                            uint32_t pid,
                                            ConstBytes blob,
                                            bool grow) {
  protos::pbzero::IonHeapGrowFtraceEvent::Decoder ion(blob.data, blob.size);
  int64_t change_bytes = static_cast<int64_t>(ion.len()) * (grow ? 1 : -1);
  // The total_allocated ftrace event reports the value before the
  // atomic_long_add / sub takes place.
  int64_t total_bytes = ion.total_allocated() + change_bytes;
  StringId global_name_id = ion_total_unknown_id_;
  StringId change_name_id = ion_change_unknown_id_;

  if (ion.has_heap_name()) {
    base::StringView heap_name = ion.heap_name();
    base::StackString<255> ion_name("mem.ion.%.*s", int(heap_name.size()),
                                    heap_name.data());
    global_name_id = context_->storage->InternString(ion_name.string_view());

    base::StackString<255> change_name("mem.ion_change.%.*s",
                                       int(heap_name.size()), heap_name.data());
    change_name_id = context_->storage->InternString(change_name.string_view());
  }

  // Push the global counter.
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kMemory, global_name_id);
  context_->event_tracker->PushCounter(timestamp,
                                       static_cast<double>(total_bytes), track);

  // Push the change counter.
  // TODO(b/121331269): these should really be instant events.
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  track = context_->track_tracker->LegacyInternThreadCounterTrack(
      change_name_id, utid);
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(change_bytes), track);

  // We are reusing the same function for ion_heap_grow and ion_heap_shrink.
  // It is fine as the arguments are the same, but we need to be sure that the
  // protobuf field id for both are the same.
  static_assert(
      static_cast<int>(
          protos::pbzero::IonHeapGrowFtraceEvent::kTotalAllocatedFieldNumber) ==
              static_cast<int>(protos::pbzero::IonHeapShrinkFtraceEvent::
                                   kTotalAllocatedFieldNumber) &&
          static_cast<int>(
              protos::pbzero::IonHeapGrowFtraceEvent::kLenFieldNumber) ==
              static_cast<int>(
                  protos::pbzero::IonHeapShrinkFtraceEvent::kLenFieldNumber) &&
          static_cast<int>(
              protos::pbzero::IonHeapGrowFtraceEvent::kHeapNameFieldNumber) ==
              static_cast<int>(protos::pbzero::IonHeapShrinkFtraceEvent::
                                   kHeapNameFieldNumber),
      "ION field mismatch");
}

/** Parses ion heap events (introduced in 4.19 kernels). */
void FtraceParser::ParseIonStat(int64_t timestamp,
                                uint32_t pid,
                                protozero::ConstBytes data) {
  protos::pbzero::IonStatFtraceEvent::Decoder ion(data.data, data.size);
  // Push the global counter.
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kMemory, ion_total_id_);
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(ion.total_allocated()), track);

  // Push the change counter.
  // TODO(b/121331269): these should really be instant events.
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  track = context_->track_tracker->LegacyInternThreadCounterTrack(
      ion_change_id_, utid);
  context_->event_tracker->PushCounter(timestamp,
                                       static_cast<double>(ion.len()), track);

  // Global track for individual buffer tracking
  auto async_track =
      context_->async_track_set_tracker->InternGlobalTrackSet(ion_buffer_id_);
  if (ion.len() > 0) {
    TrackId start_id =
        context_->async_track_set_tracker->Begin(async_track, ion.buffer_id());
    std::string buf = std::to_string(ion.len() / 1024) + " kB";
    context_->slice_tracker->Begin(
        timestamp, start_id, kNullStringId,
        context_->storage->InternString(base::StringView(buf)));
  } else {
    TrackId end_id =
        context_->async_track_set_tracker->End(async_track, ion.buffer_id());
    context_->slice_tracker->End(timestamp, end_id);
  }
}

void FtraceParser::ParseBclIrq(int64_t ts, protozero::ConstBytes data) {
  protos::pbzero::BclIrqTriggerFtraceEvent::Decoder bcl(data.data, data.size);
  int throttle = bcl.throttle();
  // id
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_id_);
  context_->event_tracker->PushCounter(ts, throttle ? bcl.id() : -1, track);
  // throttle
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_throttle_);
  context_->event_tracker->PushCounter(ts, throttle, track);
  // cpu0_limit
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_cpu0_);
  context_->event_tracker->PushCounter(ts, throttle ? bcl.cpu0_limit() : 0,
                                       track);
  // cpu1_limit
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_cpu1_);
  context_->event_tracker->PushCounter(ts, throttle ? bcl.cpu1_limit() : 0,
                                       track);
  // cpu2_limit
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_cpu2_);
  context_->event_tracker->PushCounter(ts, throttle ? bcl.cpu2_limit() : 0,
                                       track);
  // tpu_limit
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_tpu_);
  context_->event_tracker->PushCounter(ts, throttle ? bcl.tpu_limit() : 0,
                                       track);
  // gpu_limit
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_gpu_);
  context_->event_tracker->PushCounter(ts, throttle ? bcl.gpu_limit() : 0,
                                       track);
  // voltage
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_voltage_);
  context_->event_tracker->PushCounter(ts, bcl.voltage(), track);
  // capacity
  track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kBatteryMitigation, bcl_irq_capacity_);
  context_->event_tracker->PushCounter(ts, bcl.capacity(), track);
}

void FtraceParser::ParseDmaHeapStat(int64_t timestamp,
                                    uint32_t pid,
                                    protozero::ConstBytes data) {
  protos::pbzero::DmaHeapStatFtraceEvent::Decoder dma_heap(data.data,
                                                           data.size);
  // Push the global counter.
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kMemory, dma_heap_total_id_);
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(dma_heap.total_allocated()), track);

  // Push the change counter.
  // TODO(b/121331269): these should really be instant events.
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  track = context_->track_tracker->LegacyInternThreadCounterTrack(
      dma_heap_change_id_, utid);

  auto opt_counter_id = context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(dma_heap.len()), track);
  if (opt_counter_id) {
    context_->args_tracker->AddArgsTo(*opt_counter_id)
        .AddArg(inode_arg_id_, Variadic::UnsignedInteger(dma_heap.inode()));
  }

  // Global track for individual buffer tracking
  auto async_track =
      context_->async_track_set_tracker->InternGlobalTrackSet(dma_buffer_id_);
  if (dma_heap.len() > 0) {
    TrackId start_id = context_->async_track_set_tracker->Begin(
        async_track, static_cast<int64_t>(dma_heap.inode()));
    std::string buf = std::to_string(dma_heap.len() / 1024) + " kB";
    context_->slice_tracker->Begin(
        timestamp, start_id, kNullStringId,
        context_->storage->InternString(base::StringView(buf)));
  } else {
    TrackId end_id = context_->async_track_set_tracker->End(
        async_track, static_cast<int64_t>(dma_heap.inode()));
    context_->slice_tracker->End(timestamp, end_id);
  }
}

// This event has both the pid of the thread that sent the signal and the
// destination of the signal. Currently storing the pid of the destination.
void FtraceParser::ParseSignalGenerate(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::SignalGenerateFtraceEvent::Decoder sig(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(
      static_cast<uint32_t>(sig.pid()));
  int signal = sig.sig();
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->Scoped(
      timestamp, track, kNullStringId, signal_generate_id_, 0,
      [this, signal](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(signal_name_id_, Variadic::Integer(signal));
      });
}

void FtraceParser::ParseSignalDeliver(int64_t timestamp,
                                      uint32_t pid,
                                      ConstBytes blob) {
  protos::pbzero::SignalDeliverFtraceEvent::Decoder sig(blob.data, blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  int signal = sig.sig();
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->Scoped(
      timestamp, track, kNullStringId, signal_deliver_id_, 0,
      [this, signal](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(signal_name_id_, Variadic::Integer(signal));
      });
}

void FtraceParser::ParseOOMScoreAdjUpdate(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::OomScoreAdjUpdateFtraceEvent::Decoder evt(blob.data,
                                                            blob.size);
  // The int16_t static cast is because older version of the on-device tracer
  // had a bug on negative varint encoding (b/120618641).
  int16_t oom_adj = static_cast<int16_t>(evt.oom_score_adj());
  uint32_t tid = static_cast<uint32_t>(evt.pid());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(tid);
  context_->event_tracker->PushProcessCounterForThread(timestamp, oom_adj,
                                                       oom_score_adj_id_, utid);
}

void FtraceParser::ParseOOMKill(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::MarkVictimFtraceEvent::Decoder evt(blob.data, blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(
      static_cast<uint32_t>(evt.pid()));
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->Scoped(timestamp, track, kNullStringId, oom_kill_id_,
                                  0);
}

void FtraceParser::ParseMmEventRecord(int64_t timestamp,
                                      uint32_t pid,
                                      ConstBytes blob) {
  protos::pbzero::MmEventRecordFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t type = evt.type();
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  if (type >= mm_event_counter_names_.size()) {
    context_->storage->IncrementStats(stats::mm_unknown_type);
    return;
  }

  const auto& counter_names = mm_event_counter_names_[type];
  context_->event_tracker->PushProcessCounterForThread(
      timestamp, evt.count(), counter_names.count, utid);
  context_->event_tracker->PushProcessCounterForThread(
      timestamp, evt.max_lat(), counter_names.max_lat, utid);
  context_->event_tracker->PushProcessCounterForThread(
      timestamp, evt.avg_lat(), counter_names.avg_lat, utid);
}

void FtraceParser::ParseSysEnterEvent(int64_t timestamp,
                                      uint32_t pid,
                                      ConstBytes blob) {
  protos::pbzero::SysEnterFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t syscall_num = static_cast<uint32_t>(evt.id());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  SyscallTracker* syscall_tracker = SyscallTracker::GetOrCreate(context_);
  auto args_callback = [this, &evt](ArgsTracker::BoundInserter* inserter) {
    // process all syscall arguments
    uint32_t count = 0;
    for (auto it = evt.args(); it; ++it) {
      if (syscall_arg_name_ids_.size() == count) {
        base::StackString<32> string_arg("args[%" PRId32 "]", count);
        auto string_id =
            context_->storage->InternString(string_arg.string_view());
        syscall_arg_name_ids_.emplace_back(string_id);
      }
      inserter->AddArg(syscall_args_id_, syscall_arg_name_ids_[count],
                       Variadic::UnsignedInteger(*it));
      ++count;
    }
  };
  syscall_tracker->Enter(timestamp, utid, syscall_num, args_callback);
}

void FtraceParser::ParseSysExitEvent(int64_t timestamp,
                                     uint32_t pid,
                                     ConstBytes blob) {
  // Note: Although this seems duplicated to ParseSysEnterEvent, it is
  //       not. We decode SysExitFtraceEvent here to handle the return
  //       value of a syscall whereas SysEnterFtraceEvent is decoded
  //       above to handle the syscall arguments.
  protos::pbzero::SysExitFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t syscall_num = static_cast<uint32_t>(evt.id());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  SyscallTracker* syscall_tracker = SyscallTracker::GetOrCreate(context_);
  auto args_callback = [this, &evt](ArgsTracker::BoundInserter* inserter) {
    if (evt.has_ret()) {
      const auto ret = evt.ret();
      inserter->AddArg(syscall_ret_id_, Variadic::Integer(ret));
    }
  };
  syscall_tracker->Exit(timestamp, utid, syscall_num, args_callback);
}

void FtraceParser::ParseI2cReadEvent(int64_t timestamp,
                                     uint32_t pid,
                                     protozero::ConstBytes blob) {
  protos::pbzero::I2cReadFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t adapter_nr = static_cast<uint32_t>(evt.adapter_nr());
  uint32_t msg_nr = static_cast<uint32_t>(evt.msg_nr());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  I2cTracker* i2c_tracker = I2cTracker::GetOrCreate(context_);
  i2c_tracker->Enter(timestamp, utid, adapter_nr, msg_nr);
}

void FtraceParser::ParseI2cWriteEvent(int64_t timestamp,
                                      uint32_t pid,
                                      protozero::ConstBytes blob) {
  protos::pbzero::I2cWriteFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t adapter_nr = static_cast<uint32_t>(evt.adapter_nr());
  uint32_t msg_nr = static_cast<uint32_t>(evt.msg_nr());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  I2cTracker* i2c_tracker = I2cTracker::GetOrCreate(context_);
  i2c_tracker->Enter(timestamp, utid, adapter_nr, msg_nr);
}

void FtraceParser::ParseI2cResultEvent(int64_t timestamp,
                                       uint32_t pid,
                                       protozero::ConstBytes blob) {
  protos::pbzero::I2cResultFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t adapter_nr = static_cast<uint32_t>(evt.adapter_nr());
  uint32_t nr_msgs = static_cast<uint32_t>(evt.nr_msgs());
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);

  I2cTracker* i2c_tracker = I2cTracker::GetOrCreate(context_);
  i2c_tracker->Exit(timestamp, utid, adapter_nr, nr_msgs);
}

void FtraceParser::ParseTaskNewTask(int64_t timestamp,
                                    uint32_t source_tid,
                                    ConstBytes blob) {
  protos::pbzero::TaskNewtaskFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t clone_flags = static_cast<uint32_t>(evt.clone_flags());
  uint32_t new_tid = static_cast<uint32_t>(evt.pid());
  StringId new_comm = context_->storage->InternString(evt.comm());
  auto* proc_tracker = context_->process_tracker.get();

  // task_newtask is raised both in the case of a new process creation (fork()
  // family) and thread creation (clone(CLONE_THREAD, ...)).
  static const uint32_t kCloneThread = 0x00010000;  // From kernel's sched.h.

  // If the process is a fork, start a new process.
  if ((clone_flags & kCloneThread) == 0) {
    // This is a plain-old fork() or equivalent.
    proc_tracker->StartNewProcess(timestamp, source_tid, new_tid, new_comm,
                                  ThreadNamePriority::kFtrace);

    auto source_utid = proc_tracker->GetOrCreateThread(source_tid);
    auto new_utid = proc_tracker->GetOrCreateThread(new_tid);

    ThreadStateTracker::GetOrCreate(context_)->PushNewTaskEvent(
        timestamp, new_utid, source_utid);
    return;
  }

  // This is a pthread_create or similar. Bind the two threads together, so
  // they get resolved to the same process.
  auto source_utid = proc_tracker->GetOrCreateThread(source_tid);
  auto new_utid = proc_tracker->StartNewThread(timestamp, new_tid);
  proc_tracker->UpdateThreadNameByUtid(new_utid, new_comm,
                                       ThreadNamePriority::kFtrace);
  proc_tracker->AssociateThreads(source_utid, new_utid);

  ThreadStateTracker::GetOrCreate(context_)->PushNewTaskEvent(
      timestamp, new_utid, source_utid);
}

void FtraceParser::ParseTaskRename(ConstBytes blob) {
  protos::pbzero::TaskRenameFtraceEvent::Decoder evt(blob.data, blob.size);
  uint32_t tid = static_cast<uint32_t>(evt.pid());
  StringId comm = context_->storage->InternString(evt.newcomm());
  context_->process_tracker->UpdateThreadNameAndMaybeProcessName(
      tid, comm, ThreadNamePriority::kFtrace);
}

void FtraceParser::ParseBinderTransaction(int64_t timestamp,
                                          uint32_t pid,
                                          ConstBytes blob) {
  protos::pbzero::BinderTransactionFtraceEvent::Decoder evt(blob.data,
                                                            blob.size);
  int32_t dest_node = static_cast<int32_t>(evt.target_node());
  uint32_t dest_tgid = static_cast<uint32_t>(evt.to_proc());
  uint32_t dest_tid = static_cast<uint32_t>(evt.to_thread());
  int32_t transaction_id = static_cast<int32_t>(evt.debug_id());
  bool is_reply = static_cast<int32_t>(evt.reply()) == 1;
  uint32_t flags = static_cast<uint32_t>(evt.flags());
  auto code_str = base::IntToHexString(evt.code()) + " Java Layer Dependent";
  StringId code = context_->storage->InternString(base::StringView(code_str));
  BinderTracker::GetOrCreate(context_)->Transaction(
      timestamp, pid, transaction_id, dest_node, dest_tgid, dest_tid, is_reply,
      flags, code);
}

void FtraceParser::ParseBinderTransactionReceived(int64_t timestamp,
                                                  uint32_t pid,
                                                  ConstBytes blob) {
  protos::pbzero::BinderTransactionReceivedFtraceEvent::Decoder evt(blob.data,
                                                                    blob.size);
  int32_t transaction_id = static_cast<int32_t>(evt.debug_id());
  BinderTracker::GetOrCreate(context_)->TransactionReceived(timestamp, pid,
                                                            transaction_id);
}

void FtraceParser::ParseBinderCommand(int64_t timestamp,
                                      uint32_t pid,
                                      ConstBytes blob) {
  protos::pbzero::BinderCommandFtraceEvent::Decoder evt(blob.data, blob.size);
  BinderTracker::GetOrCreate(context_)->CommandToKernel(timestamp, pid,
                                                        evt.cmd());
}

void FtraceParser::ParseBinderReturn(int64_t timestamp,
                                     uint32_t pid,
                                     ConstBytes blob) {
  protos::pbzero::BinderReturnFtraceEvent::Decoder evt(blob.data, blob.size);
  BinderTracker::GetOrCreate(context_)->ReturnFromKernel(timestamp, pid,
                                                         evt.cmd());
}

void FtraceParser::ParseBinderTransactionAllocBuf(int64_t timestamp,
                                                  uint32_t pid,
                                                  ConstBytes blob) {
  protos::pbzero::BinderTransactionAllocBufFtraceEvent::Decoder evt(blob.data,
                                                                    blob.size);
  uint64_t data_size = static_cast<uint64_t>(evt.data_size());
  uint64_t offsets_size = static_cast<uint64_t>(evt.offsets_size());

  BinderTracker::GetOrCreate(context_)->TransactionAllocBuf(
      timestamp, pid, data_size, offsets_size);
}

void FtraceParser::ParseBinderLocked(int64_t timestamp,
                                     uint32_t pid,
                                     ConstBytes blob) {
  protos::pbzero::BinderLockedFtraceEvent::Decoder evt(blob.data, blob.size);
  BinderTracker::GetOrCreate(context_)->Locked(timestamp, pid);
}

void FtraceParser::ParseBinderLock(int64_t timestamp,
                                   uint32_t pid,
                                   ConstBytes blob) {
  protos::pbzero::BinderLockFtraceEvent::Decoder evt(blob.data, blob.size);
  BinderTracker::GetOrCreate(context_)->Lock(timestamp, pid);
}

void FtraceParser::ParseBinderUnlock(int64_t timestamp,
                                     uint32_t pid,
                                     ConstBytes blob) {
  protos::pbzero::BinderUnlockFtraceEvent::Decoder evt(blob.data, blob.size);
  BinderTracker::GetOrCreate(context_)->Unlock(timestamp, pid);
}

void FtraceParser::ParseClockSetRate(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::ClockSetRateFtraceEvent::Decoder evt(blob.data, blob.size);
  static const char kSubtitle[] = "Frequency";
  ClockRate(timestamp, evt.name(), kSubtitle, evt.state());
}

void FtraceParser::ParseClockEnable(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::ClockEnableFtraceEvent::Decoder evt(blob.data, blob.size);
  static const char kSubtitle[] = "State";
  ClockRate(timestamp, evt.name(), kSubtitle, evt.state());
}

void FtraceParser::ParseClockDisable(int64_t timestamp, ConstBytes blob) {
  protos::pbzero::ClockDisableFtraceEvent::Decoder evt(blob.data, blob.size);
  static const char kSubtitle[] = "State";
  ClockRate(timestamp, evt.name(), kSubtitle, evt.state());
}

void FtraceParser::ClockRate(int64_t timestamp,
                             base::StringView clock_name,
                             base::StringView subtitle,
                             uint64_t rate) {
  base::StackString<255> counter_name("%.*s %.*s", int(clock_name.size()),
                                      clock_name.data(), int(subtitle.size()),
                                      subtitle.data());
  StringId name = context_->storage->InternString(counter_name.c_str());
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kClockFrequency, name);
  context_->event_tracker->PushCounter(timestamp, static_cast<double>(rate),
                                       track);
}

void FtraceParser::ParseScmCallStart(int64_t timestamp,
                                     uint32_t pid,
                                     ConstBytes blob) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  protos::pbzero::ScmCallStartFtraceEvent::Decoder evt(blob.data, blob.size);

  base::StackString<64> str("scm id=%#" PRIx64, evt.x0());
  StringId name_id = context_->storage->InternString(str.string_view());
  context_->slice_tracker->Begin(timestamp, track_id, kNullStringId, name_id);
}

void FtraceParser::ParseScmCallEnd(int64_t timestamp,
                                   uint32_t pid,
                                   ConstBytes blob) {
  protos::pbzero::ScmCallEndFtraceEvent::Decoder evt(blob.data, blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(timestamp, track_id);
}

void FtraceParser::ParseCmaAllocStart(int64_t timestamp, uint32_t pid) {
  std::optional<VersionNumber> kernel_version =
      SystemInfoTracker::GetOrCreate(context_)->GetKernelVersion();
  // CmaAllocInfo event only exists after 5.10
  if (kernel_version < VersionNumber{5, 10})
    return;

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->Begin(timestamp, track_id, kNullStringId,
                                 cma_alloc_id_);
}

void FtraceParser::ParseCmaAllocInfo(int64_t timestamp,
                                     uint32_t pid,
                                     ConstBytes blob) {
  std::optional<VersionNumber> kernel_version =
      SystemInfoTracker::GetOrCreate(context_)->GetKernelVersion();
  // CmaAllocInfo event only exists after 5.10
  if (kernel_version < VersionNumber{5, 10})
    return;

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  protos::pbzero::CmaAllocInfoFtraceEvent::Decoder cma_alloc_info(blob.data,
                                                                  blob.size);
  auto args_inserter = [this,
                        &cma_alloc_info](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(cma_name_id_,
                     Variadic::String(context_->storage->InternString(
                         cma_alloc_info.name())));
    inserter->AddArg(cma_pfn_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.pfn()));
    inserter->AddArg(cma_req_pages_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.count()));
    inserter->AddArg(cma_nr_migrated_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.nr_migrated()));
    inserter->AddArg(cma_nr_reclaimed_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.nr_reclaimed()));
    inserter->AddArg(cma_nr_mapped_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.nr_mapped()));
    inserter->AddArg(cma_nr_isolate_fail_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.err_iso()));
    inserter->AddArg(cma_nr_migrate_fail_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.err_mig()));
    inserter->AddArg(cma_nr_test_fail_id_,
                     Variadic::UnsignedInteger(cma_alloc_info.err_test()));
  };
  context_->slice_tracker->End(timestamp, track_id, kNullStringId,
                               kNullStringId, args_inserter);
}

void FtraceParser::ParseDirectReclaimBegin(int64_t timestamp,
                                           uint32_t pid,
                                           ConstBytes blob) {
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  protos::pbzero::MmVmscanDirectReclaimBeginFtraceEvent::Decoder
      direct_reclaim_begin(blob.data, blob.size);

  StringId name_id =
      context_->storage->InternString("mm_vmscan_direct_reclaim");

  auto args_inserter = [this, &direct_reclaim_begin](
                           ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(direct_reclaim_order_id_,
                     Variadic::Integer(direct_reclaim_begin.order()));
    inserter->AddArg(direct_reclaim_may_writepage_id_,
                     Variadic::Integer(direct_reclaim_begin.may_writepage()));
    inserter->AddArg(
        direct_reclaim_gfp_flags_id_,
        Variadic::UnsignedInteger(direct_reclaim_begin.gfp_flags()));
  };
  context_->slice_tracker->Begin(timestamp, track_id, kNullStringId, name_id,
                                 args_inserter);
}

void FtraceParser::ParseDirectReclaimEnd(int64_t timestamp,
                                         uint32_t pid,
                                         ConstBytes blob) {
  protos::pbzero::ScmCallEndFtraceEvent::Decoder evt(blob.data, blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track_id = context_->track_tracker->InternThreadTrack(utid);
  protos::pbzero::MmVmscanDirectReclaimEndFtraceEvent::Decoder
      direct_reclaim_end(blob.data, blob.size);

  auto args_inserter =
      [this, &direct_reclaim_end](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(
            direct_reclaim_nr_reclaimed_id_,
            Variadic::UnsignedInteger(direct_reclaim_end.nr_reclaimed()));
      };
  context_->slice_tracker->End(timestamp, track_id, kNullStringId,
                               kNullStringId, args_inserter);
}

void FtraceParser::ParseShrinkSlabStart(
    int64_t timestamp,
    uint32_t pid,
    ConstBytes blob,
    PacketSequenceStateGeneration* seq_state) {
  protos::pbzero::MmShrinkSlabStartFtraceEvent::Decoder shrink_slab_start(
      blob.data, blob.size);

  StringId shrink_name =
      InternedKernelSymbolOrFallback(shrink_slab_start.shrink(), seq_state);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  auto args_inserter = [this, &shrink_slab_start,
                        shrink_name](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(shrink_name_id_, Variadic::String(shrink_name));
    inserter->AddArg(shrink_total_scan_id_,
                     Variadic::UnsignedInteger(shrink_slab_start.total_scan()));
    inserter->AddArg(shrink_priority_id_,
                     Variadic::Integer(shrink_slab_start.priority()));
  };

  context_->slice_tracker->Begin(timestamp, track, kNullStringId,
                                 shrink_slab_id_, args_inserter);
}

void FtraceParser::ParseShrinkSlabEnd(int64_t timestamp,
                                      uint32_t pid,
                                      ConstBytes blob) {
  protos::pbzero::MmShrinkSlabEndFtraceEvent::Decoder shrink_slab_end(
      blob.data, blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  auto args_inserter =
      [this, &shrink_slab_end](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(shrink_freed_id_,
                         Variadic::Integer(shrink_slab_end.retval()));
      };
  context_->slice_tracker->End(timestamp, track, kNullStringId, kNullStringId,
                               args_inserter);
}

void FtraceParser::ParseWorkqueueExecuteStart(
    uint32_t cpu,
    int64_t timestamp,
    uint32_t pid,
    ConstBytes blob,
    PacketSequenceStateGeneration* seq_state) {
  protos::pbzero::WorkqueueExecuteStartFtraceEvent::Decoder evt(blob.data,
                                                                blob.size);
  StringId name_id = InternedKernelSymbolOrFallback(evt.function(), seq_state);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  auto args_inserter = [this, cpu](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(cpu_id_, Variadic::Integer(cpu));
  };
  context_->slice_tracker->Begin(timestamp, track, workqueue_id_, name_id,
                                 args_inserter);
}

void FtraceParser::ParseWorkqueueExecuteEnd(int64_t timestamp,
                                            uint32_t pid,
                                            ConstBytes blob) {
  protos::pbzero::WorkqueueExecuteEndFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(timestamp, track, workqueue_id_);
}

void FtraceParser::ParseIrqHandlerEntry(uint32_t cpu,
                                        int64_t timestamp,
                                        protozero::ConstBytes blob) {
  protos::pbzero::IrqHandlerEntryFtraceEvent::Decoder evt(blob.data, blob.size);

  base::StringView irq_name = evt.name();
  base::StackString<255> slice_name("IRQ (%.*s)", int(irq_name.size()),
                                    irq_name.data());
  StringId slice_name_id =
      context_->storage->InternString(slice_name.string_view());
  TrackId track = context_->track_tracker->InternCpuTrack(
      tracks::cpu_irq, cpu,
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Irq Cpu %u", cpu)});
  context_->slice_tracker->Begin(timestamp, track, irq_id_, slice_name_id);
}

void FtraceParser::ParseIrqHandlerExit(uint32_t cpu,
                                       int64_t timestamp,
                                       protozero::ConstBytes blob) {
  protos::pbzero::IrqHandlerExitFtraceEvent::Decoder evt(blob.data, blob.size);
  TrackId track = context_->track_tracker->InternCpuTrack(
      tracks::cpu_irq, cpu,
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Irq Cpu %u", cpu)});

  base::StackString<255> status("%s", evt.ret() == 1 ? "handled" : "unhandled");
  StringId status_id = context_->storage->InternString(status.string_view());
  auto args_inserter = [this,
                        &status_id](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(ret_arg_id_, Variadic::String(status_id));
  };
  context_->slice_tracker->End(timestamp, track, irq_id_, {}, args_inserter);
}

void FtraceParser::ParseSoftIrqEntry(uint32_t cpu,
                                     int64_t timestamp,
                                     protozero::ConstBytes blob) {
  protos::pbzero::SoftirqEntryFtraceEvent::Decoder evt(blob.data, blob.size);
  auto num_actions = sizeof(kActionNames) / sizeof(*kActionNames);
  if (evt.vec() >= num_actions) {
    PERFETTO_DFATAL("No action name at index %d for softirq event.", evt.vec());
    return;
  }
  base::StringView slice_name = kActionNames[evt.vec()];
  StringId slice_name_id = context_->storage->InternString(slice_name);
  TrackId track = context_->track_tracker->InternCpuTrack(
      tracks::cpu_softirq, cpu,
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("SoftIrq Cpu %u", cpu)});
  context_->slice_tracker->Begin(timestamp, track, irq_id_, slice_name_id);
}

void FtraceParser::ParseSoftIrqExit(uint32_t cpu,
                                    int64_t timestamp,
                                    protozero::ConstBytes blob) {
  protos::pbzero::SoftirqExitFtraceEvent::Decoder evt(blob.data, blob.size);
  TrackId track = context_->track_tracker->InternCpuTrack(
      tracks::cpu_softirq, cpu,
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("SoftIrq Cpu %u", cpu)});
  auto vec = evt.vec();
  auto args_inserter = [this, vec](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(vec_arg_id_, Variadic::Integer(vec));
  };
  context_->slice_tracker->End(timestamp, track, irq_id_, {}, args_inserter);
}

void FtraceParser::ParseGpuMemTotal(int64_t timestamp,
                                    protozero::ConstBytes data) {
  protos::pbzero::GpuMemTotalFtraceEvent::Decoder gpu_mem_total(data.data,
                                                                data.size);

  TrackId track = kInvalidTrackId;
  const uint32_t pid = gpu_mem_total.pid();
  if (pid == 0) {
    // Pid 0 is used to indicate the global total
    track = context_->track_tracker->LegacyInternGlobalCounterTrack(
        TrackTracker::Group::kMemory, gpu_mem_total_name_id_, {},
        gpu_mem_total_unit_id_, gpu_mem_total_global_desc_id_);
  } else {
    // It's possible for GpuMemTotal ftrace events to be emitted by kworker
    // threads *after* process death. In this case, we simply want to discard
    // the event as otherwise we would create fake processes which we
    // definitely want to avoid.
    // See b/192274404 for more info.
    std::optional<UniqueTid> opt_utid =
        context_->process_tracker->GetThreadOrNull(pid);
    if (!opt_utid)
      return;

    // If the thread does exist, the |pid| in gpu_mem_total events is always a
    // true process id (and not a thread id) so ensure there is an association
    // between the tid and pid.
    UniqueTid updated_utid = context_->process_tracker->UpdateThread(pid, pid);
    PERFETTO_DCHECK(updated_utid == *opt_utid);

    // UpdateThread above should ensure this is always set.
    UniquePid upid = *context_->storage->thread_table()[*opt_utid].upid();
    PERFETTO_DCHECK(context_->storage->process_table()[upid].pid() == pid);

    track = context_->track_tracker->LegacyInternProcessCounterTrack(
        gpu_mem_total_name_id_, upid, gpu_mem_total_unit_id_,
        gpu_mem_total_proc_desc_id_);
  }
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(gpu_mem_total.size()), track);
}

void FtraceParser::ParseSchedBlockedReason(
    protozero::ConstBytes blob,
    PacketSequenceStateGeneration* seq_state) {
  protos::pbzero::SchedBlockedReasonFtraceEvent::Decoder event(blob);
  uint32_t pid = static_cast<uint32_t>(event.pid());
  auto utid = context_->process_tracker->GetOrCreateThread(pid);
  uint32_t caller_iid = static_cast<uint32_t>(event.caller());
  auto* interned_string = seq_state->LookupInternedMessage<
      protos::pbzero::InternedData::kKernelSymbolsFieldNumber,
      protos::pbzero::InternedString>(caller_iid);

  std::optional<StringId> blocked_function_str_id = std::nullopt;
  if (interned_string) {
    protozero::ConstBytes str = interned_string->str();
    blocked_function_str_id = context_->storage->InternString(
        base::StringView(reinterpret_cast<const char*>(str.data), str.size));
  }

  ThreadStateTracker::GetOrCreate(context_)->PushBlockedReason(
      utid, event.io_wait(), blocked_function_str_id);
}

void FtraceParser::ParseFastRpcDmaStat(int64_t timestamp,
                                       uint32_t pid,
                                       protozero::ConstBytes blob) {
  protos::pbzero::FastrpcDmaStatFtraceEvent::Decoder event(blob.data,
                                                           blob.size);

  StringId name;
  if (0 <= event.cid() &&
      event.cid() < static_cast<int32_t>(kFastRpcCounterSize)) {
    name = fast_rpc_delta_names_[static_cast<size_t>(event.cid())];
  } else {
    base::StackString<64> str("mem.fastrpc[%" PRId32 "]", event.cid());
    name = context_->storage->InternString(str.string_view());
  }

  StringId total_name;
  if (0 <= event.cid() &&
      event.cid() < static_cast<int32_t>(kFastRpcCounterSize)) {
    total_name = fast_rpc_total_names_[static_cast<size_t>(event.cid())];
  } else {
    base::StackString<64> str("mem.fastrpc[%" PRId32 "]", event.cid());
    total_name = context_->storage->InternString(str.string_view());
  }

  // Push the global counter.
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kMemory, total_name);
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(event.total_allocated()), track);

  // Push the change counter.
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId delta_track =
      context_->track_tracker->LegacyInternThreadCounterTrack(name, utid);
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(event.len()), delta_track);
}

void FtraceParser::ParseCpuhpPause(int64_t,
                                   uint32_t,
                                   protozero::ConstBytes blob) {
  protos::pbzero::CpuhpPauseFtraceEvent::Decoder evt(blob.data, blob.size);
  // TODO(b/183110813): Parse and visualize this event.
}

void FtraceParser::ParseNetifReceiveSkb(uint32_t cpu,
                                        int64_t timestamp,
                                        protozero::ConstBytes blob) {
  protos::pbzero::NetifReceiveSkbFtraceEvent::Decoder event(blob.data,
                                                            blob.size);
  base::StringView net_device = event.name();
  base::StackString<255> counter_name("%.*s Received KB",
                                      static_cast<int>(net_device.size()),
                                      net_device.data());
  StringId name = context_->storage->InternString(counter_name.string_view());

  nic_received_bytes_[name] += event.len();

  uint64_t nic_received_kilobytes = nic_received_bytes_[name] / 1024;
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kNetwork, name);
  std::optional<CounterId> id = context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(nic_received_kilobytes), track);
  if (!id) {
    return;
  }
  // Store cpu & len as args for metrics computation
  StringId cpu_key = context_->storage->InternString("cpu");
  StringId len_key = context_->storage->InternString("len");
  context_->args_tracker->AddArgsTo(*id)
      .AddArg(cpu_key, Variadic::UnsignedInteger(cpu))
      .AddArg(len_key, Variadic::UnsignedInteger(event.len()));
}

void FtraceParser::ParseNetDevXmit(uint32_t cpu,
                                   int64_t timestamp,
                                   protozero::ConstBytes blob) {
  protos::pbzero::NetDevXmitFtraceEvent::Decoder evt(blob.data, blob.size);
  base::StringView net_device = evt.name();
  base::StackString<255> counter_name("%.*s Transmitted KB",
                                      static_cast<int>(net_device.size()),
                                      net_device.data());
  StringId name = context_->storage->InternString(counter_name.string_view());

  // Make sure driver took care of packet.
  if (evt.rc() != 0) {
    return;
  }
  nic_transmitted_bytes_[name] += evt.len();

  uint64_t nic_transmitted_kilobytes = nic_transmitted_bytes_[name] / 1024;
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kNetwork, name);
  std::optional<CounterId> id = context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(nic_transmitted_kilobytes), track);
  if (!id) {
    return;
  }
  // Store cpu & len as args for metrics computation.
  context_->args_tracker->AddArgsTo(*id)
      .AddArg(cpu_id_, Variadic::UnsignedInteger(cpu))
      .AddArg(len_arg_id_, Variadic::UnsignedInteger(evt.len()));
}

void FtraceParser::ParseInetSockSetState(int64_t timestamp,
                                         uint32_t pid,
                                         protozero::ConstBytes blob) {
  protos::pbzero::InetSockSetStateFtraceEvent::Decoder evt(blob.data,
                                                           blob.size);

  // Skip non TCP protocol.
  if (evt.protocol() != kIpprotoTcp) {
    PERFETTO_ELOG("skip non tcp protocol");
    return;
  }

  // Skip non IP protocol.
  if (evt.family() != kAfNet && evt.family() != kAfNet6) {
    PERFETTO_ELOG("skip non IP protocol");
    return;
  }

  // Skip invalid TCP state.
  if (evt.newstate() >= TCP_MAX_STATES || evt.oldstate() >= TCP_MAX_STATES) {
    PERFETTO_ELOG("skip invalid tcp state");
    return;
  }

  auto got = skaddr_to_stream_.find(evt.skaddr());
  if (got == skaddr_to_stream_.end()) {
    skaddr_to_stream_[evt.skaddr()] = ++num_of_tcp_stream_;
  }
  uint32_t stream = skaddr_to_stream_[evt.skaddr()];
  base::StackString<64> stream_str("TCP stream#%" PRIu32 "", stream);
  StringId stream_id =
      context_->storage->InternString(stream_str.string_view());

  StringId slice_name_id;
  if (evt.newstate() == TCP_SYN_SENT) {
    base::StackString<32> str("%s(pid=%" PRIu32 ")",
                              kTcpStateNames[evt.newstate()], pid);
    slice_name_id = context_->storage->InternString(str.string_view());
  } else if (evt.newstate() == TCP_ESTABLISHED) {
    base::StackString<64> str("%s(sport=%" PRIu32 ",dport=%" PRIu32 ")",
                              kTcpStateNames[evt.newstate()], evt.sport(),
                              evt.dport());
    slice_name_id = context_->storage->InternString(str.string_view());
  } else {
    base::StringView slice_name = kTcpStateNames[evt.newstate()];
    slice_name_id = context_->storage->InternString(slice_name);
  }

  // Push to async task set tracker.
  auto async_track =
      context_->async_track_set_tracker->InternGlobalTrackSet(stream_id);
  TrackId end_id = context_->async_track_set_tracker->End(
      async_track, static_cast<int64_t>(evt.skaddr()));
  context_->slice_tracker->End(timestamp, end_id);
  TrackId start_id = context_->async_track_set_tracker->Begin(
      async_track, static_cast<int64_t>(evt.skaddr()));
  context_->slice_tracker->Begin(timestamp, start_id, tcp_state_id_,
                                 slice_name_id);
}

void FtraceParser::ParseTcpRetransmitSkb(int64_t timestamp,
                                         protozero::ConstBytes blob) {
  protos::pbzero::TcpRetransmitSkbFtraceEvent::Decoder evt(blob.data,
                                                           blob.size);

  // Push event as instant to async task set tracker.
  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      tcp_retransmited_name_id_);
  base::StackString<64> str("sport=%" PRIu32 ",dport=%" PRIu32 "", evt.sport(),
                            evt.dport());
  StringId slice_name_id = context_->storage->InternString(str.string_view());
  TrackId track_id =
      context_->async_track_set_tracker->Scoped(async_track, timestamp, 0);
  context_->slice_tracker->Scoped(timestamp, track_id, tcp_event_id_,
                                  slice_name_id, 0);
}

void FtraceParser::ParseNapiGroReceiveEntry(uint32_t cpu,
                                            int64_t timestamp,
                                            protozero::ConstBytes blob) {
  protos::pbzero::NapiGroReceiveEntryFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);
  base::StringView net_device = evt.name();
  StringId slice_name_id = context_->storage->InternString(net_device);
  TrackId track = context_->track_tracker->InternCpuTrack(
      tracks::cpu_napi_gro, cpu,
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Napi Gro Cpu %u", cpu)});
  auto len = evt.len();
  auto args_inserter = [this, len](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(len_arg_id_, Variadic::Integer(len));
  };
  context_->slice_tracker->Begin(timestamp, track, napi_gro_id_, slice_name_id,
                                 args_inserter);
}

void FtraceParser::ParseNapiGroReceiveExit(uint32_t cpu,
                                           int64_t timestamp,
                                           protozero::ConstBytes blob) {
  protos::pbzero::NapiGroReceiveExitFtraceEvent::Decoder evt(blob.data,
                                                             blob.size);
  TrackId track = context_->track_tracker->InternCpuTrack(
      tracks::cpu_napi_gro, cpu,
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Napi Gro Cpu %u", cpu)});
  auto ret = evt.ret();
  auto args_inserter = [this, ret](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(ret_arg_id_, Variadic::Integer(ret));
  };
  context_->slice_tracker->End(timestamp, track, napi_gro_id_, {},
                               args_inserter);
}

void FtraceParser::ParseCpuFrequencyLimits(int64_t timestamp,
                                           protozero::ConstBytes blob) {
  protos::pbzero::CpuFrequencyLimitsFtraceEvent::Decoder evt(blob.data,
                                                             blob.size);

  TrackId max_track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_max_frequency_limit, evt.cpu_id(),
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Cpu %u Max Freq Limit", evt.cpu_id())});
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(evt.max_freq()), max_track);

  TrackId min_track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_min_frequency_limit, evt.cpu_id(),
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Cpu %u Min Freq Limit", evt.cpu_id())});
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(evt.min_freq()), min_track);
}

void FtraceParser::ParseKfreeSkb(int64_t timestamp,
                                 protozero::ConstBytes blob) {
  protos::pbzero::KfreeSkbFtraceEvent::Decoder evt(blob.data, blob.size);

  // Skip non IP & IPV6 protocol.
  if (evt.protocol() != kEthPIp && evt.protocol() != kEthPIp6) {
    return;
  }
  num_of_kfree_skb_ip_prot += 1;

  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kNetwork, kfree_skb_name_id_);
  std::optional<CounterId> id = context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(num_of_kfree_skb_ip_prot), track);
  if (!id) {
    return;
  }
  base::StackString<255> prot("%s", evt.protocol() == kEthPIp ? "IP" : "IPV6");
  StringId prot_id = context_->storage->InternString(prot.string_view());
  // Store protocol as args for metrics computation.
  context_->args_tracker->AddArgsTo(*id).AddArg(protocol_arg_id_,
                                                Variadic::String(prot_id));
}

void FtraceParser::ParseCrosEcSensorhubData(int64_t timestamp,
                                            protozero::ConstBytes blob) {
  protos::pbzero::CrosEcSensorhubDataFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);

  // Push the global counter.
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kDeviceState,
      context_->storage->InternString(
          base::StringView("cros_ec.cros_ec_sensorhub_data." +
                           std::to_string(evt.ec_sensor_num()))));

  auto args_inserter = [this, &evt](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(cros_ec_arg_num_id_,
                     Variadic::Integer(evt.ec_sensor_num()));
    inserter->AddArg(
        cros_ec_arg_ec_id_,
        Variadic::Integer(evt.fifo_timestamp() - evt.current_timestamp()));
    inserter->AddArg(cros_ec_arg_sample_ts_id_,
                     Variadic::Integer(evt.current_timestamp()));
  };

  context_->event_tracker->PushCounter(
      timestamp,
      static_cast<double>(evt.current_time() - evt.current_timestamp()), track,
      args_inserter);
}

void FtraceParser::ParseUfshcdClkGating(int64_t timestamp,
                                        protozero::ConstBytes blob) {
  protos::pbzero::UfshcdClkGatingFtraceEvent::Decoder evt(blob.data, blob.size);
  int32_t clk_state = 0;

  switch (evt.state()) {
    case 1:
      // Change ON state to 3
      clk_state = 3;
      break;
    case 2:
      // Change REQ_OFF state to 1
      clk_state = 1;
      break;
    case 3:
      // Change REQ_ON state to 2
      clk_state = 2;
      break;
  }
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kNetwork, ufs_clkgating_id_);
  context_->event_tracker->PushCounter(timestamp,
                                       static_cast<double>(clk_state), track);
}

void FtraceParser::ParseTrustySmc(uint32_t pid,
                                  int64_t timestamp,
                                  protozero::ConstBytes blob) {
  protos::pbzero::TrustySmcFtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<48> name("trusty_smc:r0= %" PRIu64, evt.r0());
  StringId name_generic = context_->storage->InternString(name.string_view());

  context_->slice_tracker->Begin(timestamp, track, trusty_category_id_,
                                 name_generic);
}

void FtraceParser::ParseTrustySmcDone(uint32_t pid,
                                      int64_t timestamp,
                                      protozero::ConstBytes blob) {
  protos::pbzero::TrustySmcDoneFtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->End(timestamp, track, trusty_category_id_);
  base::StackString<256> name("trusty_smc_done:r0= %" PRIu64, evt.ret());
  StringId name_generic = context_->storage->InternString(name.string_view());
  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);
}

void FtraceParser::ParseTrustyStdCall32(uint32_t pid,
                                        int64_t timestamp,
                                        protozero::ConstBytes blob) {
  protos::pbzero::TrustyStdCall32FtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->Begin(timestamp, track, trusty_category_id_,
                                 trusty_name_trusty_std_id_);
}

void FtraceParser::ParseTrustyStdCall32Done(uint32_t pid,
                                            int64_t timestamp,
                                            protozero::ConstBytes blob) {
  protos::pbzero::TrustyStdCall32DoneFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->End(timestamp, track, trusty_category_id_);
  if (evt.ret() < 0) {
    base::StackString<256> name("trusty_err_std: err= %" PRIi64, evt.ret());
    StringId name_generic = context_->storage->InternString(name.string_view());
    context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                    name_generic, 0);
  }
}

void FtraceParser::ParseTrustyShareMemory(uint32_t pid,
                                          int64_t timestamp,
                                          protozero::ConstBytes blob) {
  protos::pbzero::TrustyShareMemoryFtraceEvent::Decoder evt(blob.data,
                                                            blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name(
      "trusty_share_mem: len= %" PRIu64 " nents= %" PRIu32 " lend= %" PRIu32,
      static_cast<uint64_t>(evt.len()), evt.nents(), evt.lend());
  StringId name_generic = context_->storage->InternString(name.string_view());

  context_->slice_tracker->Begin(timestamp, track, trusty_category_id_,
                                 name_generic);
}

void FtraceParser::ParseTrustyShareMemoryDone(uint32_t pid,
                                              int64_t timestamp,
                                              protozero::ConstBytes blob) {
  protos::pbzero::TrustyShareMemoryDoneFtraceEvent::Decoder evt(blob.data,
                                                                blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(timestamp, track, trusty_category_id_);

  base::StackString<256> name("trusty_share_mem: handle= %" PRIu64
                              " ret= %" PRIi32,
                              evt.handle(), evt.ret());
  StringId name_generic = context_->storage->InternString(name.string_view());
  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);
}

void FtraceParser::ParseTrustyReclaimMemory(uint32_t pid,
                                            int64_t timestamp,
                                            protozero::ConstBytes blob) {
  protos::pbzero::TrustyReclaimMemoryFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name("trusty_reclaim_mem: id=%" PRIu64, evt.id());
  StringId name_generic = context_->storage->InternString(name.string_view());

  context_->slice_tracker->Begin(timestamp, track, trusty_category_id_,
                                 name_generic);
}

void FtraceParser::ParseTrustyReclaimMemoryDone(uint32_t pid,
                                                int64_t timestamp,
                                                protozero::ConstBytes blob) {
  protos::pbzero::TrustyReclaimMemoryDoneFtraceEvent::Decoder evt(blob.data,
                                                                  blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(timestamp, track, trusty_category_id_);

  if (evt.ret() < 0) {
    base::StackString<256> name("trusty_reclaim_mem_err: err= %" PRIi32,
                                evt.ret());
    StringId name_generic = context_->storage->InternString(name.string_view());
    context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                    name_generic, 0);
  }
}

void FtraceParser::ParseTrustyIrq(uint32_t pid,
                                  int64_t timestamp,
                                  protozero::ConstBytes blob) {
  protos::pbzero::TrustyIrqFtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name("trusty_irq: irq= %" PRIi32, evt.irq());
  StringId name_generic = context_->storage->InternString(name.string_view());

  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);
}

void FtraceParser::ParseTrustyIpcHandleEvent(uint32_t pid,
                                             int64_t timestamp,
                                             protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcHandleEventFtraceEvent::Decoder evt(blob.data,
                                                               blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name(
      "trusty_ipc_handle_event: chan=%" PRIu32 " srv_name=%s event=%" PRIu32,
      evt.chan(), evt.srv_name().ToStdString().c_str(), evt.event_id());
  StringId name_generic = context_->storage->InternString(name.string_view());

  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);
}

void FtraceParser::ParseTrustyEnqueueNop(uint32_t pid,
                                         int64_t timestamp,
                                         protozero::ConstBytes blob) {
  protos::pbzero::TrustyEnqueueNopFtraceEvent::Decoder evt(blob.data,
                                                           blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name("trusty_enqueue_nop: arg1= %" PRIu32
                              " arg2= %" PRIu32 " arg3=%" PRIu32,
                              evt.arg1(), evt.arg2(), evt.arg3());
  StringId name_generic = context_->storage->InternString(name.string_view());
  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);
}

void FtraceParser::ParseTrustyIpcConnect(uint32_t pid,
                                         int64_t timestamp,
                                         protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcConnectFtraceEvent::Decoder evt(blob.data,
                                                           blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name("tipc_connect: %s",
                              evt.port().ToStdString().c_str());
  StringId name_generic = context_->storage->InternString(name.string_view());

  context_->slice_tracker->Begin(timestamp, track, trusty_category_id_,
                                 name_generic);
}

void FtraceParser::ParseTrustyIpcConnectEnd(uint32_t pid,
                                            int64_t timestamp,
                                            protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcConnectEndFtraceEvent::Decoder evt(blob.data,
                                                              blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->End(timestamp, track, trusty_category_id_);
  if (evt.err()) {
    base::StackString<256> name("tipc_err_connect:err= %" PRIi32, evt.err());
    StringId name_generic = context_->storage->InternString(name.string_view());
    context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                    name_generic, 0);
  }
}

void FtraceParser::ParseTrustyIpcWrite(uint32_t pid,
                                       int64_t timestamp,
                                       protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcWriteFtraceEvent::Decoder evt(blob.data, blob.size);

  StringId name_generic = kNullStringId;
  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  if (evt.shm_cnt() > 0) {
    base::StackString<256> name("tipc_write: %s shm_cnt:[%" PRIu64 "]",
                                evt.srv_name().ToStdString().c_str(),
                                evt.shm_cnt());
    name_generic = context_->storage->InternString(name.string_view());
  } else {
    base::StackString<256> name("tipc_write: %s",
                                evt.srv_name().ToStdString().c_str());
    name_generic = context_->storage->InternString(name.string_view());
  }
  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);

  if (evt.len_or_err() < 0) {
    base::StackString<256> name("tipc_err_write:len_or_err= %" PRIi32,
                                evt.len_or_err());
    name_generic = context_->storage->InternString(name.string_view());
    context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                    name_generic, 0);
  }
}

void FtraceParser::ParseTrustyIpcRead(uint32_t pid,
                                      int64_t timestamp,
                                      protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcReadFtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name("tipc_read: %s",
                              evt.srv_name().ToStdString().c_str());
  StringId name_generic = context_->storage->InternString(name.string_view());
  context_->slice_tracker->Begin(timestamp, track, trusty_category_id_,
                                 name_generic);
}

void FtraceParser::ParseTrustyIpcReadEnd(uint32_t pid,
                                         int64_t timestamp,
                                         protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcReadEndFtraceEvent::Decoder evt(blob.data,
                                                           blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);
  context_->slice_tracker->End(timestamp, track, trusty_category_id_);

  if (evt.len_or_err() <= 0) {
    base::StackString<256> name("tipc_err_read:len_or_err= %" PRIi32,
                                evt.len_or_err());
    StringId name_generic = context_->storage->InternString(name.string_view());
    context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                    name_generic, 0);
  }
}

void FtraceParser::ParseTrustyIpcPoll(uint32_t pid,
                                      int64_t timestamp,
                                      protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcPollFtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  base::StackString<256> name("tipc_poll: %s",
                              evt.srv_name().ToStdString().c_str());
  StringId name_generic = context_->storage->InternString(name.string_view());
  context_->slice_tracker->Scoped(timestamp, track, trusty_category_id_,
                                  name_generic, 0);
}

void FtraceParser::ParseTrustyIpcRx(uint32_t pid,
                                    int64_t ts,
                                    protozero::ConstBytes blob) {
  protos::pbzero::TrustyIpcRxFtraceEvent::Decoder evt(blob.data, blob.size);

  UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
  TrackId track = context_->track_tracker->InternThreadTrack(utid);

  context_->slice_tracker->Scoped(ts, track, trusty_category_id_,
                                  trusty_name_tipc_rx_id_, 0);
}

void FtraceParser::ParseUfshcdCommand(int64_t timestamp,
                                      protozero::ConstBytes blob) {
  protos::pbzero::UfshcdCommandFtraceEvent::Decoder evt(blob.data, blob.size);

  // Parse occupied ufs command queue
  uint32_t num = evt.doorbell() > 0
                     ? static_cast<uint32_t>(PERFETTO_POPCOUNT(evt.doorbell()))
                     : (evt.str_t() == 1 ? 0 : 1);
  TrackId track = context_->track_tracker->LegacyInternGlobalCounterTrack(
      TrackTracker::Group::kIo, ufs_command_count_id_);
  context_->event_tracker->PushCounter(timestamp, static_cast<double>(num),
                                       track);

  // Parse ufs command tag
  base::StackString<32> cmd_track_name("io.ufs.command.tag[%03d]", evt.tag());
  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      context_->storage->InternString(cmd_track_name.string_view()));
  if (evt.str_t() == 0) {
    std::string ufs_op_str = GetUfsCmdString(evt.opcode(), evt.group_id());
    StringId ufs_slice_name =
        context_->storage->InternString(base::StringView(ufs_op_str));
    TrackId start_id = context_->async_track_set_tracker->Begin(async_track, 0);
    context_->slice_tracker->Begin(timestamp, start_id, kNullStringId,
                                   ufs_slice_name);
  } else {
    TrackId end_id = context_->async_track_set_tracker->End(async_track, 0);
    context_->slice_tracker->End(timestamp, end_id);
  }
}

void FtraceParser::ParseWakeSourceActivate(int64_t timestamp,
                                           protozero::ConstBytes blob) {
  protos::pbzero::WakeupSourceActivateFtraceEvent::Decoder evt(blob.data,
                                                               blob.size);
  std::string event_name = evt.name().ToStdString();

  uint32_t count = active_wakelock_to_count_[event_name];

  active_wakelock_to_count_[event_name] += 1;

  // There is already an active track with this name, don't create another.
  if (count > 0) {
    return;
  }

  base::StackString<32> str("Wakelock(%s)", event_name.c_str());
  StringId stream_id = context_->storage->InternString(str.string_view());

  auto async_track =
      context_->async_track_set_tracker->InternGlobalTrackSet(stream_id);

  TrackId start_id = context_->async_track_set_tracker->Begin(async_track, 0);

  context_->slice_tracker->Begin(timestamp, start_id, kNullStringId, stream_id);
}

void FtraceParser::ParseWakeSourceDeactivate(int64_t timestamp,
                                             protozero::ConstBytes blob) {
  protos::pbzero::WakeupSourceDeactivateFtraceEvent::Decoder evt(blob.data,
                                                                 blob.size);

  std::string event_name = evt.name().ToStdString();
  uint32_t count = active_wakelock_to_count_[event_name];
  active_wakelock_to_count_[event_name] = count > 0 ? count - 1 : 0;
  if (count != 1) {
    return;
  }

  base::StackString<32> str("Wakelock(%s)", event_name.c_str());
  StringId stream_id = context_->storage->InternString(str.string_view());
  auto async_track =
      context_->async_track_set_tracker->InternGlobalTrackSet(stream_id);

  TrackId end_id = context_->async_track_set_tracker->End(async_track, 0);
  context_->slice_tracker->End(timestamp, end_id);
}

void FtraceParser::ParseSuspendResume(int64_t timestamp,
                                      uint32_t cpu,
                                      uint32_t tid,
                                      protozero::ConstBytes blob) {
  protos::pbzero::SuspendResumeFtraceEvent::Decoder evt(blob.data, blob.size);

  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      suspend_resume_name_id_);

  std::string action_name = evt.action().ToStdString();

  // Hard code fix the timekeeping_freeze action's value to zero, the value is
  // processor_id and device could enter suspend/resume from different
  // processor.
  auto val = (action_name == "timekeeping_freeze") ? 0 : evt.val();
  std::string cookie_key = std::to_string(val);
  int64_t cookie = 0;
  if (suspend_resume_cookie_map_.Find(cookie_key) == nullptr) {
    cookie = static_cast<int64_t>(suspend_resume_cookie_map_.size());
    suspend_resume_cookie_map_[cookie_key] = cookie;
  } else {
    cookie = suspend_resume_cookie_map_[cookie_key];
  }

  base::StackString<64> str("%s(%" PRIu32 ")", action_name.c_str(), val);
  std::string current_action = str.ToStdString();

  StringId slice_name_id = context_->storage->InternString(str.string_view());

  if (!evt.start()) {
    TrackId end_id =
        context_->async_track_set_tracker->End(async_track, cookie);
    context_->slice_tracker->End(timestamp, end_id);
    ongoing_suspend_resume_actions[current_action] = false;
    return;
  }

  // Complete the previous action before starting a new one.
  if (ongoing_suspend_resume_actions[current_action]) {
    TrackId end_id =
        context_->async_track_set_tracker->End(async_track, cookie);
    auto args_inserter = [this](ArgsTracker::BoundInserter* inserter) {
      inserter->AddArg(replica_slice_id_, Variadic::Boolean(true));
    };
    context_->slice_tracker->End(timestamp, end_id, kNullStringId,
                                 kNullStringId, args_inserter);
  }

  TrackId start_id =
      context_->async_track_set_tracker->Begin(async_track, cookie);

  auto args_inserter = [&](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(suspend_resume_utid_arg_name_,
                     Variadic::UnsignedInteger(
                         context_->process_tracker->GetOrCreateThread(tid)));
    inserter->AddArg(suspend_resume_event_type_arg_name_,
                     Variadic::String(suspend_resume_main_event_id_));
    auto ucpu = context_->cpu_tracker->GetOrCreateCpu(cpu);
    inserter->AddArg(ucpu_id_, Variadic::UnsignedInteger(ucpu.value));

    // These fields are set to null as this is not a device PM callback event.
    inserter->AddArg(suspend_resume_device_arg_name_,
                     Variadic::String(kNullStringId));
    inserter->AddArg(suspend_resume_driver_arg_name_,
                     Variadic::String(kNullStringId));
    inserter->AddArg(suspend_resume_callback_phase_arg_name_,
                     Variadic::String(kNullStringId));
  };
  context_->slice_tracker->Begin(timestamp, start_id, suspend_resume_name_id_,
                                 slice_name_id, args_inserter);
  ongoing_suspend_resume_actions[current_action] = true;
}

void FtraceParser::ParseSuspendResumeMinimal(int64_t timestamp,
                                             protozero::ConstBytes blob) {
  protos::pbzero::SuspendResumeMinimalFtraceEvent::Decoder evt(blob.data,
                                                               blob.size);
  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      suspend_resume_minimal_name_id_);

  if (evt.start()) {
    TrackId start_id = context_->async_track_set_tracker->Begin(
        async_track, static_cast<int64_t>(0));
    context_->slice_tracker->Begin(timestamp, start_id,
                                   suspend_resume_minimal_name_id_,
                                   suspend_resume_minimal_slice_name_id_);
  } else {
    TrackId end_id = context_->async_track_set_tracker->End(
        async_track, static_cast<int64_t>(0));
    context_->slice_tracker->End(timestamp, end_id);
  }
}

void FtraceParser::ParseSchedCpuUtilCfs(int64_t timestamp,
                                        protozero::ConstBytes blob) {
  protos::pbzero::SchedCpuUtilCfsFtraceEvent::Decoder evt(blob.data, blob.size);
  TrackId util_track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_utilization, evt.cpu(),
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Cpu %u Util", evt.cpu())});
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(evt.cpu_util()), util_track);

  TrackId cap_track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_capacity, evt.cpu(),
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Cpu %u Cap", evt.cpu())});
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(evt.capacity()), cap_track);

  TrackId nrr_track = context_->track_tracker->InternCpuCounterTrack(
      tracks::cpu_nr_running, evt.cpu(),
      TrackTracker::LegacyCharArrayName{
          base::StackString<255>("Cpu %u Nr Running", evt.cpu())});
  context_->event_tracker->PushCounter(
      timestamp, static_cast<double>(evt.nr_running()), nrr_track);
}

void FtraceParser::ParseFuncgraphEntry(
    int64_t timestamp,
    uint32_t cpu,
    uint32_t pid,
    protozero::ConstBytes blob,
    PacketSequenceStateGeneration* seq_state) {
  protos::pbzero::FuncgraphEntryFtraceEvent::Decoder evt(blob.data, blob.size);
  StringId name_id = InternedKernelSymbolOrFallback(evt.func(), seq_state);

  TrackId track = {};
  if (pid != 0) {
    // common case: normal thread
    UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
    track = context_->track_tracker->InternThreadTrack(utid);
  } else {
    // Idle threads (swapper) are implicit, and all share the same thread id 0.
    // Therefore we cannot use a thread-scoped track because many instances
    // of swapper might be running concurrently. Fall back onto global tracks
    // (one per cpu).
    track = context_->track_tracker->InternCpuTrack(
        tracks::cpu_funcgraph, cpu,
        TrackTracker::LegacyCharArrayName{
            base::StackString<255>("swapper%u -funcgraph", cpu)});
  }

  context_->slice_tracker->Begin(timestamp, track, kNullStringId, name_id);
}

void FtraceParser::ParseFuncgraphExit(
    int64_t timestamp,
    uint32_t cpu,
    uint32_t pid,
    protozero::ConstBytes blob,
    PacketSequenceStateGeneration* seq_state) {
  protos::pbzero::FuncgraphExitFtraceEvent::Decoder evt(blob.data, blob.size);
  StringId name_id = InternedKernelSymbolOrFallback(evt.func(), seq_state);

  TrackId track = {};
  if (pid != 0) {
    // common case: normal thread
    UniqueTid utid = context_->process_tracker->GetOrCreateThread(pid);
    track = context_->track_tracker->InternThreadTrack(utid);
  } else {
    // special case: see |ParseFuncgraphEntry|
    track = context_->track_tracker->InternCpuTrack(
        tracks::cpu_funcgraph, cpu,
        TrackTracker::LegacyCharArrayName{
            base::StackString<255>("swapper%u -funcgraph", cpu)});
  }

  context_->slice_tracker->End(timestamp, track, kNullStringId, name_id);
}

/** Parses android_fs_dataread_start event.*/
void FtraceParser::ParseAndroidFsDatareadStart(int64_t ts,
                                               uint32_t pid,
                                               ConstBytes data) {
  protos::pbzero::AndroidFsDatareadStartFtraceEvent::Decoder
      android_fs_read_begin(data);
  base::StringView file_path(android_fs_read_begin.pathbuf());
  std::pair<uint64_t, int64_t> key(android_fs_read_begin.ino(),
                                   android_fs_read_begin.offset());
  inode_offset_thread_map_.Insert(key, pid);
  // Create a new Track object for the event.
  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      android_fs_category_id_);
  TrackId track_id = context_->async_track_set_tracker->Begin(async_track, pid);
  StringId string_id = context_->storage->InternString(file_path);
  auto args_inserter = [this, &android_fs_read_begin,
                        &string_id](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(file_path_id_, Variadic::String(string_id));
    inserter->AddArg(offset_id_start_,
                     Variadic::Integer(android_fs_read_begin.offset()));
    inserter->AddArg(bytes_read_id_start_,
                     Variadic::Integer(android_fs_read_begin.bytes()));
  };
  context_->slice_tracker->Begin(ts, track_id, kNullStringId,
                                 android_fs_data_read_id_, args_inserter);
}

/** Parses android_fs_dataread_end event.*/
void FtraceParser::ParseAndroidFsDatareadEnd(int64_t ts, ConstBytes data) {
  protos::pbzero::AndroidFsDatareadEndFtraceEvent::Decoder android_fs_read_end(
      data);
  std::pair<uint64_t, int64_t> key(android_fs_read_end.ino(),
                                   android_fs_read_end.offset());
  // Find the corresponding (inode, offset) pair in the map.
  auto it = inode_offset_thread_map_.Find(key);
  if (!it) {
    return;
  }
  uint32_t start_event_tid = *it;
  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      android_fs_category_id_);
  TrackId track_id =
      context_->async_track_set_tracker->End(async_track, start_event_tid);
  auto args_inserter =
      [this, &android_fs_read_end](ArgsTracker::BoundInserter* inserter) {
        inserter->AddArg(offset_id_end_,
                         Variadic::Integer(android_fs_read_end.offset()));
        inserter->AddArg(bytes_read_id_end_,
                         Variadic::Integer(android_fs_read_end.bytes()));
      };
  context_->slice_tracker->End(ts, track_id, kNullStringId, kNullStringId,
                               args_inserter);
  // Erase the entry from the map.
  inode_offset_thread_map_.Erase(key);
}

StringId FtraceParser::GetRpmStatusStringId(int32_t rpm_status_val) {
  // `RPM_SUSPENDED` is omitted from this list as it would never be used as a
  // slice label.
  switch (rpm_status_val) {
    case RPM_INVALID:
      return runtime_status_invalid_id_;
    case RPM_SUSPENDING:
      return runtime_status_suspending_id_;
    case RPM_RESUMING:
      return runtime_status_resuming_id_;
    case RPM_ACTIVE:
      return runtime_status_active_id_;
  }

  PERFETTO_DLOG(
      "Invalid runtime status value obtained from rpm_status ftrace event");
  return runtime_status_invalid_id_;
}

void FtraceParser::ParseRpmStatus(int64_t ts, protozero::ConstBytes blob) {
  protos::pbzero::RpmStatusFtraceEvent::Decoder rpm_event(blob.data, blob.size);

  // Device here refers to anything managed by a Linux kernel driver.
  std::string device_name = rpm_event.name().ToStdString();
  StringId device_name_string_id = context_->storage->InternString(device_name);
  TrackId track_id = context_->track_tracker->InternSingleDimensionTrack(
      tracks::linux_rpm, linux_device_name_id_,
      Variadic::String(device_name_string_id),
      TrackTracker::LegacyStringIdName{device_name_string_id});

  // A `runtime_status` event implies a potential change in state. Hence, if an
  // active slice exists for this device, end that slice.
  if (devices_with_active_rpm_slice_.find(device_name_string_id) !=
      devices_with_active_rpm_slice_.end()) {
    context_->slice_tracker->End(ts, track_id);
  }

  // To reduce visual clutter, the "SUSPENDED" state will be omitted from the
  // visualization, as devices typically spend the majority of their time in
  // this state.
  int32_t rpm_status = rpm_event.status();
  if (rpm_status == RPM_SUSPENDED) {
    devices_with_active_rpm_slice_.erase(device_name_string_id);
    return;
  }

  context_->slice_tracker->Begin(ts, track_id, /*category=*/kNullStringId,
                                 /*raw_name=*/GetRpmStatusStringId(rpm_status));
  devices_with_active_rpm_slice_.insert(device_name_string_id);
}

// Parses `device_pm_callback_start` events and begins corresponding slices in
// the suspend / resume latency UI track.
void FtraceParser::ParseDevicePmCallbackStart(int64_t ts,
                                              uint32_t cpu,
                                              uint32_t tid,
                                              protozero::ConstBytes blob) {
  protos::pbzero::DevicePmCallbackStartFtraceEvent::Decoder dpm_event(
      blob.data, blob.size);

  // Device here refers to anything managed by a Linux kernel driver.
  std::string device_name = dpm_event.device().ToStdString();
  std::string driver_name = dpm_event.driver().ToStdString();
  int64_t cookie;
  if (suspend_resume_cookie_map_.Find(device_name) == nullptr) {
    cookie = static_cast<int64_t>(suspend_resume_cookie_map_.size());
    suspend_resume_cookie_map_[device_name] = cookie;
  } else {
    cookie = suspend_resume_cookie_map_[device_name];
  }

  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      suspend_resume_name_id_);
  TrackId track_id =
      context_->async_track_set_tracker->Begin(async_track, cookie);

  std::string slice_name = device_name + " " + driver_name;
  StringId slice_name_id = context_->storage->InternString(slice_name.c_str());

  std::string callback_phase = ConstructCallbackPhaseName(
      /*pm_ops=*/dpm_event.pm_ops().ToStdString(),
      /*event_type=*/GetDpmCallbackEventString(dpm_event.event()));

  auto args_inserter = [&](ArgsTracker::BoundInserter* inserter) {
    inserter->AddArg(suspend_resume_utid_arg_name_,
                     Variadic::UnsignedInteger(
                         context_->process_tracker->GetOrCreateThread(tid)));
    inserter->AddArg(suspend_resume_event_type_arg_name_,
                     Variadic::String(suspend_resume_device_pm_event_id_));
    auto ucpu = context_->cpu_tracker->GetOrCreateCpu(cpu);
    inserter->AddArg(ucpu_id_, Variadic::UnsignedInteger(ucpu.value));
    inserter->AddArg(
        suspend_resume_device_arg_name_,
        Variadic::String(context_->storage->InternString(device_name.c_str())));
    inserter->AddArg(
        suspend_resume_driver_arg_name_,
        Variadic::String(context_->storage->InternString(driver_name.c_str())));
    inserter->AddArg(suspend_resume_callback_phase_arg_name_,
                     Variadic::String(context_->storage->InternString(
                         callback_phase.c_str())));
  };

  context_->slice_tracker->Begin(ts, track_id, suspend_resume_name_id_,
                                 slice_name_id, args_inserter);
}

// Parses `device_pm_callback_end` events and ends corresponding slices in the
// suspend / resume latency UI track.
void FtraceParser::ParseDevicePmCallbackEnd(int64_t ts,
                                            protozero::ConstBytes blob) {
  protos::pbzero::DevicePmCallbackEndFtraceEvent::Decoder dpm_event(blob.data,
                                                                    blob.size);

  // Device here refers to anything managed by a Linux kernel driver.
  std::string device_name = dpm_event.device().ToStdString();

  auto async_track = context_->async_track_set_tracker->InternGlobalTrackSet(
      suspend_resume_name_id_);
  TrackId track_id = context_->async_track_set_tracker->End(
      async_track, suspend_resume_cookie_map_[device_name]);
  context_->slice_tracker->End(ts, track_id);
}

void FtraceParser::ParsePanelWriteGeneric(int64_t timestamp,
                                          uint32_t pid,
                                          ConstBytes blob) {
  protos::pbzero::PanelWriteGenericFtraceEvent::Decoder evt(blob.data,
                                                            blob.size);
  if (!evt.type()) {
    context_->storage->IncrementStats(stats::systrace_parse_failure);
    return;
  }

  uint32_t tgid = static_cast<uint32_t>(evt.pid());
  SystraceParser::GetOrCreate(context_)->ParseKernelTracingMarkWrite(
      timestamp, pid, static_cast<char>(evt.type()), false /*trace_begin*/,
      evt.name(), tgid, evt.value());
}

StringId FtraceParser::InternedKernelSymbolOrFallback(
    uint64_t key,
    PacketSequenceStateGeneration* seq_state) {
  auto* interned_string = seq_state->LookupInternedMessage<
      protos::pbzero::InternedData::kKernelSymbolsFieldNumber,
      protos::pbzero::InternedString>(key);
  StringId name_id;
  if (interned_string) {
    protozero::ConstBytes str = interned_string->str();
    name_id = context_->storage->InternString(
        base::StringView(reinterpret_cast<const char*>(str.data), str.size));
  } else {
    base::StackString<255> slice_name("%#" PRIx64, key);
    name_id = context_->storage->InternString(slice_name.string_view());
  }
  return name_id;
}

void FtraceParser::ParseDeviceFrequency(int64_t ts,
                                        protozero::ConstBytes blob) {
  protos::pbzero::DevfreqFrequencyFtraceEvent::Decoder event(blob);
  std::string dev_name = event.dev_name().ToStdString();

  constexpr char kDelimiter[] = "devfreq_";
  auto position = dev_name.find(kDelimiter);
  if (position == std::string::npos) {
    return;
  }

  // Get device name by getting substring after delimiter and keep existing
  // naming convention (e.g. cpufreq, gpufreq) consistent by adding a suffix
  // to the devfreq name (e.g. dsufreq, bcifreq)
  StringId device_name = context_->storage->InternString(
      (dev_name.substr(position + sizeof(kDelimiter) - 1) + "freq").c_str());

  TrackTracker::DimensionsBuilder dims_builder =
      context_->track_tracker->CreateDimensionsBuilder();
  dims_builder.AppendDimension(device_name_id_, Variadic::String(device_name));
  TrackTracker::Dimensions dims_id = std::move(dims_builder).Build();

  TrackId track_id = context_->track_tracker->InternCounterTrack(
      tracks::linux_device_frequency, dims_id);
  context_->event_tracker->PushCounter(ts, static_cast<double>(event.freq()),
                                       track_id);
}

}  // namespace perfetto::trace_processor
