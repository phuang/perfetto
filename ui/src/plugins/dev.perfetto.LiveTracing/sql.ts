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

import { NUM, NUM_NULL, STR } from "../../trace_processor/query_result";


const kTraceEndSchedSlice = "(SELECT MAX(ts + dur) FROM __intrinsic_sched_slice)";

const kTraceEndCounter = "(SELECT MAX(ts) FROM __intrinsic_counter)";

export const kQueryCpuLoad = `
  SELECT
  ucpu AS cpu,
  SUM(dur) / 1e6 AS busy_ms,
  SUM(dur) * 100.0 / 1e9 AS load_percent
  FROM __intrinsic_sched_slice
  WHERE utid != 0 
  AND ts >= ${kTraceEndSchedSlice} - 1e9
  GROUP BY ucpu
  ORDER BY ucpu;`

export const kCpuLoadDataSchema = {
  cpu: NUM,
  busy_ms: NUM,
  load_percent: NUM
};

export const kQueryCpuFreq = `
    WITH cpu_freq_with_dur AS (
    SELECT
      (DENSE_RANK() OVER (ORDER BY t.id) - 1) AS cpu,
      c.value as freq,
      c.ts,
      LEAD(c.ts, 1, c.ts+1) OVER(PARTITION BY t.id) - c.ts as dur,
      LAST_VALUE(c.value) OVER(PARTITION BY t.id) as last_freq,
      LAST_VALUE(c.ts) OVER() as last_ts,
      COUNT(*) OVER(PARTITION BY t.id) as '_count'
    FROM __intrinsic_counter c
    JOIN __intrinsic_track t ON c.track_id = t.id
    WHERE t.name = 'cpufreq'
      AND c.ts >= ${kTraceEndCounter} - 1e9 -- Last second of data
    ORDER BY t.id)
  SELECT
    cpu,
    SUM(freq * dur) OVER(PARTITION BY cpu) / SUM(dur) OVER(PARTITION BY cpu) as avg_freq,
    last_freq,
    last_ts
  FROM cpu_freq_with_dur;`

export const kCpuFreqDataSchema = {
  cpu: NUM,
  avg_freq: NUM,
  last_freq: NUM,
  last_ts: NUM
};

export const kQueryFps = `
  SELECT 
    COUNT(*) as fps,
    AVG(dur) / 1e6 as avg_frame_dur_ms,
    MAX(ts) - MIN(ts) as captured_interval_ns
  FROM __intrinsic_slice s
  JOIN __intrinsic_track t ON s.track_id = t.id
  WHERE t.name = 'Actual Timeline'
    AND s.ts >= ${kTraceEndCounter} - 1e9 -- Last second of data;
    AND s.ts <= ${kTraceEndCounter};`

export const kFpsDataSchema = {
  fps: NUM,
  avg_frame_dur_ms: NUM_NULL,
  captured_interval_ns: NUM_NULL
};

export const kQueryPower = `
  WITH RailPower AS (
    SELECT
      t.name as rail_name,
      (MAX(c.value) - MIN(c.value)) / (MAX(c.ts) - MIN(c.ts)) * 1e9 AS avg_power_uw
    FROM __intrinsic_counter c
    JOIN __intrinsic_track t ON c.track_id = t.id
    WHERE t.name IN (
      'power.S13M_VDD_CPU0_uws', 
      'power.S3M_VDD_CPU1_uws', 
      'power.S2M_VDD_CPU2_uws',
      'power.S2S_VDD_GPU_uws',
      'power.rails.ddr.c',
      'power.rails.ddr.a'
    )
    AND c.ts >= ${kTraceEndCounter} - 1e9  -- Last second of data
    GROUP BY t.id
  )
  SELECT
    CASE 
      WHEN rail_name LIKE '%CPU%' THEN 'Total CPU Clusters'
      WHEN rail_name LIKE '%GPU%' THEN 'GPU'
      WHEN rail_name LIKE '%ddr%' THEN 'Total DDR'
    END AS component,
    IIF(SUM(avg_power_uw) IS NULL, 0, SUM(avg_power_uw)) AS total_avg_power_uw
  FROM RailPower
  GROUP BY component;`;

export const kPowerDataSchema = {
  component: STR,
  total_avg_power_uw: NUM
};

export const kQueryTemperature = `
  SELECT
    t.name AS sensor,
    MIN(value) / 1000.0 AS min_temp,
    MAX(value) / 1000.0 AS max_temp,
    AVG(value) / 1000.0 AS avg_temp
  FROM __intrinsic_counter c
  JOIN __intrinsic_track t ON c.track_id = t.id
  WHERE
    (
      t.name LIKE '%therm' OR
      t.name LIKE '%therm-cached'
    )
    AND c.ts >= ${kTraceEndCounter} - 1e9  -- Last second of data
  GROUP BY t.name
  ORDER BY max_temp DESC;`;

export const kTemperatureDataSchema = {
  sensor: STR,
  min_temp: NUM,
  max_temp: NUM,
  avg_temp: NUM
};