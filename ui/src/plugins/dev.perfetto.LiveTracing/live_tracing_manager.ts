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
import { RecordingTarget } from '../dev.perfetto.RecordTraceV2/interfaces/recording_target';
import { TracingSession } from '../dev.perfetto.RecordTraceV2/interfaces/tracing_session';
import { EvtSource } from '../../base/events';
import { WasmEngineProxy } from '../../trace_processor/wasm_engine_proxy';
import { QueryResult, STR, NUM } from '../../trace_processor/query_result';
import protobuf from 'protobufjs/minimal';
import * as sql from './sql';

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
  private lastQueryTime = 0;
  private pendingParseResults: Array<Promise<void>> = [];
  private intrinsicTablesExist: boolean = false;
  private hasNewData = false;
  private cpuFreqData = {
    little: [{ load: 0, freq: 0 }, { load: 0, freq: 0 }],
    mid: [{ load: 0, freq: 0 }, { load: 0, freq: 0 }, { load: 0, freq: 0 }, { load: 0, freq: 0 }],
    big: [{ load: 0, freq: 0 }],
  };
  private fpsData = 0;
  private powerData = { cpu: 0, ddr: 0, gpu: 0 };
  private tempData = { soc: 0, gpu: 0 };

  private powerDataHistory = { cpu: 0, ddr: 0, gpu: 0 };

  // a data callback variable.
  private dataCallback?: (data: Record<string, unknown>) => void;

  registerDataCallback(callback: (data: Record<string, unknown>) => void) {
    this.dataCallback = callback;
  }

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
      // 10 seconds window.
      // @ts-ignore
      windowSizeNs: 10 * 1e9,
    });

    const config = this.createConfig();
    const res = await target.startTracing(config);
    if (!res.ok) {
      throw new Error(res.error);
    }

    this.session = res.value;
    this.series.clear();
    this.lastQueryTime = 0;
    this.intrinsicTablesExist = false;

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
      console.log(`LiveTracingManager: Received ${packets.length} bytes of trace data`);
      this.parsePackets(packets).catch((e) => {
        console.error('LiveTracingManager: parsePackets failed', e);
      });
    });

    console.log('LiveTracingManager: Tracing started, waiting for data...');
    this.timer = window.setInterval(() => {
      if (!this.session) {
        return;
      }
      this.session.flush().then(() => {
        this.session?.readBuffers().then(() => {
          console.log('LiveTracingManager: Flushed session and read buffers');
        }).catch((e) => {
          console.error('LiveTracingManager: Failed to read buffers', e);
        });
      }).catch((e) => {
        console.error('LiveTracingManager: Failed to flush session', e);
      });
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
    const kFillPolicy = protos.TraceConfig.BufferConfig.FillPolicy;
    const kBuffers = [
      {
        sizeKb: 64 * 1024,
        fillPolicy: kFillPolicy.RING_BUFFER,
      },
      // {
      //   sizeKb: 16 * 1024,
      //   fillPolicy: kFillPolicy.RING_BUFFER,
      // },
    ];


    // const kBatteryCounters = protos.AndroidPowerConfig.BatteryCounters;
    const kAndroidConfig = {
      name: 'android.power',
      androidPowerConfig: {
        collectPowerRails: true,
        targetBuffer: 0,
        // batteryPollMs: 500,
        // batteryCounters: [
        //   kBatteryCounters.BATTERY_COUNTER_CAPACITY_PERCENT,
        //   kBatteryCounters.BATTERY_COUNTER_CHARGE,
        //   kBatteryCounters.BATTERY_COUNTER_CURRENT,
        // ],
      },
    };

    const kLinuxFTraceConfig = {
      name: 'linux.ftrace',
      ftraceConfig: {
        targetBuffer: 1,
        ftraceEvents: [
          'sched/sched_switch',
          'power/cpu_frequency',
          'power/cpu_idle',
          'power/suspend_resume',
          'ftrace/print',
        ],
        atraceCategories: [
          'freq',
          'power',
          'thermal',
        ],
      },
    };

    const kSurfaceFlingerConfig = {
      name: 'android.surfaceflinger.frametimeline',
      surfaceFlingerConfig: {
        targetBuffer: 2,
      },
    };

    return {
      durationMs: 0, // Continuous
      flushPeriodMs: 500,
      buffers: kBuffers,
      dataSources: [
        {
          config: kAndroidConfig,
        },
        {
          config: kLinuxFTraceConfig,
        },
        {
          config: kSurfaceFlingerConfig,
        }
      ],
    };
  }

  private async parsePackets(data: Uint8Array) {
    if (!this.engine) {
      console.warn('LiveTracingManager: parsePackets called but engine is undefined');
      return;
    }

    const now = Date.now();

    if (!this.lastQueryTime) {
      this.lastQueryTime = now;
    }

    this.pendingParseResults.push(this.engine.parse(data));
    this.hasNewData = true;

    if (now - this.lastQueryTime < 1000) {
      console.log(`LiveTracingManager: Buffering data, next query in ${1000 - (now - this.lastQueryTime)}ms`);
      return;
    }
    this.lastQueryTime = now;

    let parsePromisses = this.pendingParseResults;
    this.pendingParseResults = [];

    if (!this.hasNewData) {
      console.log('LiveTracingManager: No new data, skipping queries');
      return;
    }
    this.hasNewData = false;

    // Wait for all pending parses to complete before querying.
    Promise.all(parsePromisses).then(async () => {
      console.log('LiveTracingManager: All pending parses completed, starting queries');
      if (!this.engine) {
        console.error('LiveTracingManager: Engine not initialized');
        return;
      }

      await this.engine.flush();
      console.log('LiveTracingManager: Engine flushed');

      // Check if intrinsic tables exist.
      if (!this.intrinsicTablesExist) {
        const checkTablesResult = await this.engine.tryQuery(
          "SELECT name FROM sqlite_master WHERE name LIKE '%__intrinsic_%';"
        );

        if (!checkTablesResult.ok) {
          console.error('LiveTracingManager: Failed to check intrinsic tables', checkTablesResult.error);
          return;
        }

        const tables = new Set<string>();
        const tableIt = checkTablesResult.value.iter({ name: STR });

        for (; tableIt.valid(); tableIt.next()) {
          tables.add(tableIt.name);
        }
        console.log(`LiveTracingManager: Detected intrinsic tables: ${Array.from(tables).join(', ')}`);

        this.intrinsicTablesExist = tables.has('__intrinsic_track') && tables.has('__intrinsic_counter');
        if (!this.intrinsicTablesExist) {
          console.warn('LiveTracingManager: Intrinsic tables not found, live tracing data may be incomplete');
          return;
        }
      }


      {
        const kQueryRecordCounts = `
          SELECT
            (SELECT COUNT(*) FROM __intrinsic_slice) as slice_count,
            (SELECT COUNT(*) FROM __intrinsic_counter) as counter_count,
            (SELECT COUNT(*) FROM __intrinsic_sched_slice) as sched_count,
            (SELECT COUNT(*) FROM __intrinsic_android_logs) as log_count,
            (SELECT COUNT(*) FROM __intrinsic_args) as arg_count
        `;
        const result = await this.engine!.tryQuery(kQueryRecordCounts);
        if (result.ok) {
          const iter = result.value.iter({
            slice_count: NUM,
            counter_count: NUM,
            sched_count: NUM,
            log_count: NUM,
            arg_count: NUM,
          });
          if (iter.valid()) {
            console.log(
              `LiveTracingManager Table Counts: ` +
              `slice=${iter.slice_count}, ` +
              `counter=${iter.counter_count}, ` +
              `sched=${iter.sched_count}, ` +
              `logs=${iter.log_count}, ` +
              `args=${iter.arg_count}`
            );
          }
        } else {
          console.warn(`Select table counts failed: ${result.error}`);
        }
      }

      const queries: Array<string> = [
        sql.kQueryCpuLoad,
        sql.kQueryCpuFreq,
        sql.kQueryFps,
        sql.kQueryPower,
        sql.kQueryTemperature,
        sql.kEnableTableDump ? sql.kQueryTableDump : sql.kQueryDummy
      ];

      const queryResults: Array<Promise<QueryResult>> = queries.map(q => this.engine!.query(q));

      Promise.all(queryResults).then((results) => {
        console.log(`LiveTracingManager: Received ${results.length} query results`);
        const [cpuLoadResult, cpuFreqResult, fpsResult, powerResult, thermResult, _tableDumpResult] = results;
        // Process CPU load results.
        {
          // console.log(`LiveTracingManager: queryResults obtaineed ${cpuLoadResult.numRows()} rows`);
          const iter = cpuLoadResult.iter(sql.kCpuLoadDataSchema);

          let totalLoad = 0;
          let cpuCount = 0;
          for (; iter.valid(); iter.next()) {
            const cpu = iter.cpu;
            const loadPercent = iter.load_percent;
            totalLoad += loadPercent;
            cpuCount++;
            if (cpu < 2) {
              this.cpuFreqData.little[cpu].load = loadPercent;
            } else if (cpu < 6) {
              this.cpuFreqData.mid[cpu - 2].load = loadPercent;
            } else {
              this.cpuFreqData.big[cpu - 6].load = loadPercent;
            }
          }
          if (cpuCount > 0) {
            console.log(`LiveTracingManager: Avg CPU Load: ${(totalLoad / cpuCount).toFixed(2)}% across ${cpuCount} CPUs`);
          }
        }

        // Process CPU frequency results.
        {
          // console.log(`LiveTracingManager: queryResults obtaineed ${cpuFreqResult.numRows()} rows`);
          const iter = cpuFreqResult.iter(sql.kCpuFreqDataSchema);

          for (; iter.valid(); iter.next()) {
            const cpu = iter.cpu;
            const avgFreq = iter.avg_freq / 1000000; // Convert to GHz
            if (cpu < 2) {
              this.cpuFreqData.little[cpu].freq = avgFreq;
            } else if (cpu < 6) {
              this.cpuFreqData.mid[cpu - 2].freq = avgFreq;
            } else {
              this.cpuFreqData.big[cpu - 6].freq = avgFreq;
            }
          }
        }

        // Process FPS results.
        {
          const iter = fpsResult.iter(sql.kFpsDataSchema);

          if (iter.valid()) {
            const fps = iter.fps;
            console.log(`LiveTracingManager: FPS: ${fps.toFixed(1)}`);
            this.fpsData = fps;
          }
        }

        // Process power results.
        {
          const iter = powerResult.iter(sql.kPowerDataSchema);

          for (; iter.valid(); iter.next()) {
            const component = iter.component;
            const power_mws = iter.power_uws / 1000; // Convert to mWs
            console.log(`${component}: ${power_mws} mWS`);
            if (component === 'CPU') {
              if (this.powerDataHistory.cpu > 0) {
                const powerChange = power_mws - this.powerDataHistory.cpu;
                console.log(`CPU Power Change: ${powerChange.toFixed(2)} mWS`);
                this.powerData.cpu = powerChange;
              }
              this.powerDataHistory.cpu = power_mws;
            } else if (component === 'DDR') {
              if (this.powerDataHistory.ddr > 0) {
                const powerChange = power_mws - this.powerDataHistory.ddr;
                console.log(`DDR Power Change: ${powerChange.toFixed(2)} mWS`);
                this.powerData.ddr = powerChange;
              }
              this.powerDataHistory.ddr = power_mws;
            } else if (component === 'GPU') {
              if (this.powerDataHistory.gpu > 0) {
                const powerChange = power_mws - this.powerDataHistory.gpu;
                console.log(`GPU Power Change: ${powerChange.toFixed(2)} mWS`);
                this.powerData.gpu = powerChange;
              }
              this.powerDataHistory.gpu = power_mws;
            }
          }
        }

        // Process temperature results.
        {
          const iter = thermResult.iter(sql.kTemperatureDataSchema);

          var temps: Record<string, number> = {
            'soc_therm': 0,
            'soc_therm-cached': 0,
            'gpu_therm': 0,
            'gpu_therm-cached': 0,
          };
          for (; iter.valid(); iter.next()) {
            const sensor = iter.sensor;
            const avgTemp = iter.avg_temp;
            temps[sensor] = avgTemp;
            console.log(`LiveTracingManager: Sensor ${sensor}: Avg ${avgTemp.toFixed(1)} °C`);
            if (sensor === 'soc_therm' || sensor === 'soc_therm-cached') {
              this.tempData.soc = avgTemp;
            } else if (sensor === 'gpu_therm' || sensor === 'gpu_therm-cached') {
              this.tempData.gpu = avgTemp;
            }
          }

          if (sql.kEnableTableDump) {
            const iter = _tableDumpResult.iter(sql.kTableDumpDataSchema);

            for (; iter.valid(); iter.next()) {
              const component = iter.component;
              const ts = iter.ts;
              const power_uws = iter.power_uws;
              console.log(`Table Dump - Component: ${component}, Timestamp: ${ts}, Power: ${power_uws} uWs`);
            }
          }

          this.tempData.soc = temps['soc_therm'] || temps['soc_therm-cached'] || this.tempData.soc;
          this.tempData.gpu = temps['gpu_therm'] || temps['gpu_therm-cached'] || this.tempData.gpu;

        }

        if (this.dataCallback) {
          // const CORES = { LITTLE: 2, MID: 4, BIG: 1 };
          let data: Record<string, unknown> = {
            cpu: this.cpuFreqData,
            temp: this.tempData,
            power: this.powerData,
            fps: this.fpsData,
          };
          this.dataCallback(data);
        }
      }).catch((e) => {
        console.error('LiveTracingManager: Failed to query counters', e);
      });
    }).catch((e) => {
      console.error('LiveTracingManager: Failed to parse data: ', e);
    });
  }
}
