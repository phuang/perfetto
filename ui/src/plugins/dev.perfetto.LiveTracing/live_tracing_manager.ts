// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import protos from '../../protos';
import {RecordingTarget} from '../dev.perfetto.RecordTraceV2/interfaces/recording_target';
import {TracingSession} from '../dev.perfetto.RecordTraceV2/interfaces/tracing_session';
import {EvtSource} from '../../base/events';
import m from 'mithril';
import {WasmEngineProxy} from '../../trace_processor/wasm_engine_proxy';
import {LONG, NUM, STR, STR_NULL} from '../../trace_processor/query_result';
import protobuf from 'protobufjs/minimal';

export interface LivePoint {
  readonly x: number;
  readonly y: number;
}

export interface LiveSeries {
  readonly name: string;
  readonly points: LivePoint[];
}

export class LiveTracingManager {
  private session?: TracingSession;
  private engine?: WasmEngineProxy;
  private timer?: number;
  readonly series = new Map<string, LiveSeries>();
  readonly onDataUpdate = new EvtSource<void>();
  private firstTs = 0n;
  private lastQueryTs = 0n;

  async start(target: RecordingTarget) {
    if (this.session) {
      await this.stop();
    }

    this.engine = new WasmEngineProxy('live_tracing');
    await this.engine.resetTraceProcessor({
      tokenizeOnly: false,
      cropTrackEvents: false,
      ingestFtraceInRawTable: false,
      analyzeTraceProtoContent: false,
      ftraceDropUntilAllCpusValid: false,
      forceFullSort: false,
    });

    const config = this.createConfig();
    const res = await target.startTracing(config);
    if (!res.ok) {
      throw new Error(res.error);
    }

    this.session = res.value;
    this.series.clear();
    this.firstTs = 0n;
    this.lastQueryTs = 0n;

    // Send TraceConfig packet first so TraceProcessor knows what to expect.
    const traceConfigBytes = protos.TraceConfig.encode(config).finish();
    const packetWriter = protobuf.Writer.create();
    packetWriter.uint32((33 << 3) | 2);
    packetWriter.bytes(traceConfigBytes);
    const packetBytes = packetWriter.finish();

    const traceWriter = protobuf.Writer.create();
    traceWriter.uint32((1 << 3) | 2);
    traceWriter.bytes(packetBytes);
    await this.engine.parse(traceWriter.finish());

    this.session.onTraceData.addListener((packets) => {
      this.parsePackets(packets).catch((e) => {
        console.error('LiveTracingManager: parsePackets failed', e);
      });
    });

    this.timer = window.setInterval(async () => {
      if (this.session) {
        await this.session.flush();
        await this.session.readBuffers();
      }
    }, 1000);
  }

  async stop() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = undefined;
    }
    if (this.session) {
      await this.session.stop();
      this.session = undefined;
    }
    if (this.engine) {
      this.engine[Symbol.dispose]();
      this.engine = undefined;
    }
    this.onDataUpdate.notify();
  }

  get isRecording(): boolean {
    return this.session !== undefined;
  }

  private createConfig(): protos.ITraceConfig {
    return {
      durationMs: 0, // Continuous
      flushPeriodMs: 1000,
      buffers: [
        {
          sizeKb: 64 * 1024,
          fillPolicy: protos.TraceConfig.BufferConfig.FillPolicy.RING_BUFFER,
        },
      ],
      dataSources: [
        {
          config: {
            name: 'linux.sys_stats',
            sysStatsConfig: {
              cpufreqPeriodMs: 1000,
              thermalPeriodMs: 1000,
              devfreqPeriodMs: 1000,
              meminfoPeriodMs: 1000,
            },
          },
        },
        {
          config: {
            name: 'android.power',
            androidPowerConfig: {
              batteryPollMs: 1000,
              collectPowerRails: true,
              batteryCounters: [
                protos.AndroidPowerConfig.BatteryCounters
                  .BATTERY_COUNTER_CAPACITY_PERCENT,
                protos.AndroidPowerConfig.BatteryCounters.BATTERY_COUNTER_CHARGE,
                protos.AndroidPowerConfig.BatteryCounters
                  .BATTERY_COUNTER_CURRENT,
              ],
            },
          },
        },
      ],
    };
  }

  private async parsePackets(data: Uint8Array) {
    if (!this.engine) return;
    try {
      await this.engine.parse(data);

      // Check if intrinsic tables exist.
      const checkTables = await this.engine.tryQuery(
        "SELECT name FROM sqlite_master WHERE type='table' AND name IN ('__intrinsic_track', '__intrinsic_counter', 'cpu_counter_track', 'counter_track')",
      );
      if (!checkTables.ok) return;

      const tables = new Set<string>();
      const tableIt = checkTables.value.iter({name: STR});
      for (; tableIt.valid(); tableIt.next()) {
        tables.add(tableIt.name);
      }
      if (
        !tables.has('__intrinsic_track') ||
        !tables.has('__intrinsic_counter')
      ) {
        return;
      }

      const hasCpuTrack = tables.has('cpu_counter_track');

      // Debug: list all tracks.
      const tracksRes = await this.engine.tryQuery(
        'SELECT id, name, type FROM __intrinsic_track',
      );
      if (tracksRes.ok) {
        const trackList = [];
        const it = tracksRes.value.iter({id: NUM, name: STR_NULL, type: STR});
        for (; it.valid(); it.next()) {
          trackList.push(`${it.id}:${it.name ?? 'null'}(${it.type})`);
        }
        if (trackList.length > 0) {
          console.log(
            'LiveTracingManager: Available tracks:',
            trackList.join(', '),
          );
        }
      }

      // Query for new counter samples with better names.
      // We don't use 'unit' as it's missing from intrinsic tables.
      const query = hasCpuTrack
        ? `
        SELECT
          COALESCE(t.name, cct.name, 'unknown') || 
          (CASE WHEN cct.cpu IS NOT NULL THEN ' (CPU ' || cct.cpu || ')' ELSE '' END) as name,
          ts,
          value
        FROM __intrinsic_counter c
        JOIN __intrinsic_track t ON c.track_id = t.id
        LEFT JOIN cpu_counter_track cct ON c.track_id = cct.id
        WHERE ts > ${this.lastQueryTs}
        ORDER BY ts ASC
      `
        : `
        SELECT
          COALESCE(t.name, 'unknown') as name,
          ts,
          value
        FROM __intrinsic_counter c
        JOIN __intrinsic_track t ON c.track_id = t.id
        WHERE ts > ${this.lastQueryTs}
        ORDER BY ts ASC
      `;

      const res = await this.engine.tryQuery(query);

      if (!res.ok) {
        console.error('LiveTracingManager: Counter query failed', res.error);
        return;
      }

      const iter = res.value.iter({
        name: STR,
        ts: LONG,
        value: NUM,
      });

      let maxTs = this.lastQueryTs;
      for (; iter.valid(); iter.next()) {
        if (this.firstTs === 0n) this.firstTs = iter.ts;
        if (iter.ts > maxTs) maxTs = iter.ts;

        const relTs = Number(iter.ts - this.firstTs) / 1e9;
        let val = iter.value;
        let name = iter.name;

        // Heuristic to detect thermal zones.
        const lowerName = name.toLowerCase();
        const isThermal =
          lowerName.includes('thermal') || lowerName.includes('temp');
        if (isThermal) {
          if (!lowerName.includes('thermal') && !lowerName.includes('temp')) {
            name += ' (Thermal)';
          }
          // Values are often in mC. If it's > 200, it's likely mC.
          if (val > 200) val /= 1000;
        }

        this.addPoint(name, relTs, val);
      }
      this.lastQueryTs = maxTs;

      this.onDataUpdate.notify();
      m.redraw();
    } catch (e) {
      console.error('LiveTracingManager: Unexpected error in parsePackets', e);
    }
  }

  private addPoint(name: string, x: number, y: number) {
    let s = this.series.get(name);
    if (!s) {
      s = {name, points: []};
      this.series.set(name, s);
    }
    s.points.push({x, y});
    // Keep last 60 points (60 seconds)
    if (s.points.length > 60) {
      s.points.shift();
    }
  }
}
