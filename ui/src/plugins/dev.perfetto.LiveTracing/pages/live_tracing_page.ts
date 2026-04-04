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

import m from 'mithril';
import * as echarts from 'echarts/core';
import {RecordingManager} from '../../dev.perfetto.RecordTraceV2/recording_manager';
import {LivePoint, LiveTracingManager} from '../live_tracing_manager';
import {Button} from '../../../widgets/button';
import {LineChart, LineChartData} from '../../../components/widgets/charts/line_chart';
import {Stack} from '../../../widgets/stack';

export interface LiveTracingPageAttrs {
  readonly recMgr: RecordingManager;
}

export class LiveTracingPage implements m.ClassComponent<LiveTracingPageAttrs> {
  private readonly mgr = new LiveTracingManager();

  onremove() {
    this.mgr.stop();
  }

  view({attrs}: m.Vnode<LiveTracingPageAttrs>) {
    const recMgr = attrs.recMgr;
    const mgr = this.mgr;
    const target = recMgr.currentTarget;

    // Group series by prefix (e.g. 'cpufreq' or 'Battery')
    const grouped = new Map<string, {name: string; points: LivePoint[]}[]>();
    for (const s of mgr.series.values()) {
      let groupName = 'Other';
      const n = s.name.toLowerCase();
      if (n.includes('cpufreq') || n.includes('cpu frequency')) {
        groupName = 'CPU Frequencies';
      } else if (n.includes('battery') || n.includes('batt.')) {
        groupName = 'Battery';
      } else if (n.includes('thermal') || n.includes('temperature')) {
        groupName = 'Thermal';
      } else if (n.includes('rail') || n.includes('uws') || n.includes('energy')) {
        groupName = 'Power Rails';
      } else if (n.includes('mem.') || n.includes('memory') || n.includes('ram')) {
        groupName = 'Memory';
      } else if (n.includes('devfreq') || n.includes('frequency')) {
        groupName = 'Frequencies';
      } else {
        groupName = s.name;
      }

      let seriesList = grouped.get(groupName);
      if (!seriesList) {
        seriesList = [];
        grouped.set(groupName, seriesList);
      }
      seriesList.push({name: s.name, points: s.points});
    }

    const charts = Array.from(grouped.entries()).map(([name, seriesList]) => {
      const data: LineChartData = {
        series: seriesList,
      };
      return m(
        '.live-chart-container',
        m('h3', name),
        m(LineChart, {
          data,
          height: 250,
          xAxisLabel: 'Time (s)',
          yAxisLabel: '',
        }),
      );
    });

    return m('.pf-record-page__section.active',
      m('header', 'Live Tracing'),
      m(Stack, {direction: 'row', gap: '10px', alignItems: 'center'},
        m(Button, {
          label: mgr.isRecording ? 'Stop Live Tracing' : 'Start Live Tracing',
          icon: mgr.isRecording ? 'stop' : 'play_arrow',
          disabled: !target && !mgr.isRecording,
          onclick: async () => {
            if (mgr.isRecording) {
              await mgr.stop();
            } else if (target) {
              await mgr.start(target);
            }
          },
        }),
        target && m('span', `Target: ${target.name}`),
      ),
      m('.live-charts',
        charts.length > 0 ? charts : m('p', 'No data collected yet. Press Start to begin live tracing.')
      )
    );
  }
}
