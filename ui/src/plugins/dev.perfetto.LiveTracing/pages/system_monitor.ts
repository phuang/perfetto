import m from 'mithril';
import * as echarts from 'echarts';
import { RecordingManager } from 'src/plugins/dev.perfetto.RecordTraceV2/recording_manager';
import { RecordingTarget } from '../../dev.perfetto.RecordTraceV2/interfaces/recording_target';
import { LiveTracingManager } from '../live_tracing_manager';
// import { Live } from 'node_modules/@google/genai/dist/genai';

/**
 * ============================================================================
 * 1. DOMAIN CONFIGURATION & INTERFACES
 * ============================================================================
 */
const CORES = { LITTLE: 2, MID: 4, BIG: 1 };
const HISTORY_LIMIT = 60;

interface ClusterConfig {
  id: string;
  name: string;
  color: string;
  cores: number;
  maxGHz: number;
}

interface StatReadout {
  id: string;
  label: string;
  color: string;
  unit: string;
  area?: boolean;
}

interface StatConfig {
  id: string;
  title: string;
  color: string;
  range?: [number, number];
  readouts: StatReadout[];
}

const Theme = {
  colors: {
    lit: '#10b981', mid: '#f59e0b', big: '#ef4444',
    temp: '#f97316',
    temp_soc: '#f97316',
    temp_gpu: '#f87171',
    power: '#ec4899',
    power_cpu: '#ec4899',
    power_gpu: '#8b5cf6',
    power_ddr: '#06b6d4',
    fps: '#3b82f6'
  },
  styles: {
    title: "text-sm tracking-wider whitespace-nowrap font-light opacity-90",
    statLabel: "text-[9px] text-gray-400 uppercase font-normal block"
  }
};

export const CONFIG = {
  CLUSTERS: [
    { id: 'little', name: 'Lit Cluster', color: Theme.colors.lit, cores: CORES.LITTLE, maxGHz: 2.8 },
    { id: 'mid', name: 'Mid Cluster', color: Theme.colors.mid, cores: CORES.MID, maxGHz: 3.4 },
    { id: 'big', name: 'Big Cluster', color: Theme.colors.big, cores: CORES.BIG, maxGHz: 4.2 }
  ] as ClusterConfig[],
  STATS: [
    {
      id: 'temp', title: 'Temperatures (°C)', color: Theme.colors.temp,
      readouts: [
        { id: 'temp-soc', label: 'SOC', color: Theme.colors.temp_soc, unit: '°C' },
        { id: 'temp-gpu', label: 'GPU', color: Theme.colors.temp_gpu, unit: '°C' }
      ]
    },
    {
      id: 'power', title: 'Power Consumption (mW)', color: Theme.colors.power,
      readouts: [
        { id: 'power-cpu', label: 'CPU', color: Theme.colors.power_cpu, unit: 'mW', area: true },
        { id: 'power-gpu', label: 'GPU', color: Theme.colors.power_gpu, unit: 'mW', area: true },
        { id: 'power-ddr', label: 'DDR', color: Theme.colors.power_ddr, unit: 'mW', area: true }
      ]
    },
    {
      id: 'fps', title: 'Frame Rate (FPS)', color: Theme.colors.fps, range: [0, 130],
      readouts: [{ id: 'fps', label: 'FPS', color: Theme.colors.fps, unit: '', area: true }]
    },
    {
      id: 'eff', title: 'Efficiency (FPS/mW)', color: Theme.colors.lit,
      readouts: [{ id: 'eff', label: 'Eff', color: Theme.colors.lit, unit: '', area: true }]
    }
  ] as StatConfig[]
};

/**
 * ============================================================================
 * 2. SVG ICONS DICTIONARY
 * ============================================================================
 */
const Icons = {
  search: m.trust(`
        <svg class="w-4 h-4 text-gray-400 absolute left-3 top-3" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"></path>
        </svg>
    `),

  pause: m.trust(`
        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 24 24">
            <path d="M6 19h4V5H6v14zm8-14v14h4V5h-4z"></path>
        </svg>
    `),

  resume: m.trust(`
        <svg class="w-3.5 h-3.5" fill="currentColor" viewBox="0 0 24 24">
            <path d="M8 5v14l11-7z"></path>
        </svg>
    `),

  dot: (active: boolean) => m.trust(`
        <svg class="w-3.5 h-3.5 mr-2 transition-colors duration-300 ${active ? 'text-green-400' : 'text-red-400 opacity-60'}" fill="currentColor" viewBox="0 0 24 24">
            <circle cx="12" cy="12" r="8"></circle>
        </svg>
    `),

  save: m.trust(`
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M8 7H5a2 2 0 00-2 2v9a2 2 0 002 2h14a2 2 0 002-2V9a2 2 0 00-2-2h-3m-1 4l-3 3m0 0l-3-3m3 3V4"></path>
        </svg>
    `),

  expand: m.trust(`
        <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 8V4m0 0h4M4 4l5 5m11-1V4m0 0h-4m4 0l-5 5M4 16v4m0 0h4m-4 0l5-5m11 5l-5-5m5 5v-4m0 4h-4"></path>
        </svg>
    `)
};

/**
 * ============================================================================
 * 3. DATA LAYER (Integration Hook)
 * ============================================================================
 */
export class DataSimulator {
  private static rand(min: number, range: number): number {
    return min + Math.random() * range;
  }

  private static cpuLoad(base: number): number {
    return Math.max(5, Math.min(95, base + (Math.random() - 0.5) * 40));
  }

  static fetchMetrics() {
    return {
      cpu: {
        little: Array.from({ length: CORES.LITTLE }, () => ({ load: this.cpuLoad(10), freq: this.rand(0.4, 1.0) })),
        mid: Array.from({ length: CORES.MID }, () => ({ load: this.cpuLoad(30), freq: this.rand(0.8, 1.2) })),
        big: Array.from({ length: CORES.BIG }, () => ({ load: this.cpuLoad(60), freq: this.rand(1.8, 1.2) }))
      },
      temp: { pkg: this.rand(40, 20), gpu: this.rand(38, 15) },
      power: { pkg: this.rand(800, 1500), dram: this.rand(100, 400) },
      fps: this.rand(59, 2)
    };
  }
}

export class PefettoLiveTracingDataProvider {
  private static rand(min: number, range: number): number {
    return min + Math.random() * range;
  }

  private static cpuLoad(base: number): number {
    return Math.max(5, Math.min(95, base + (Math.random() - 0.5) * 40));
  }

  static fetchMetrics() {
    return {
      cpu: {
        little: Array.from({ length: CORES.LITTLE }, () => ({ load: this.cpuLoad(10), freq: this.rand(0.4, 1.0) })),
        mid: Array.from({ length: CORES.MID }, () => ({ load: this.cpuLoad(30), freq: this.rand(0.8, 1.2) })),
        big: Array.from({ length: CORES.BIG }, () => ({ load: this.cpuLoad(60), freq: this.rand(1.8, 1.2) }))
      },
      temp: { pkg: this.rand(40, 20), gpu: this.rand(38, 15) },
      power: { pkg: this.rand(800, 1500), dram: this.rand(100, 400) },
      fps: this.rand(59, 2)
    };
  }
}

/**
 * ============================================================================
 * 4. GLOBAL APPLICATION STATE
 * ============================================================================
 */
export const State = {
  isTracing: false,
  target: null as RecordingTarget | null,
  recordingManager: null as RecordingManager | null,
  liveTracingManager: null as LiveTracingManager | null,
  targetName: 'Android Pixel 8 Pro',
  layoutMode: 'grid' as 'grid' | 'list',
  filterQuery: '',
  expandedCardId: null as string | null,
  timer: null as ReturnType<typeof setInterval> | null,

  timeline: Array.from({ length: HISTORY_LIMIT }, (_, i) => Date.now() - (HISTORY_LIMIT - i) * 1000),
  seriesBuffers: {} as Record<string, number[][]>,
  latestMetrics: {} as Record<string, string | number>,
  hiddenSeries: {} as Record<string, boolean>,
  chartRegistry: {} as Record<string, echarts.EChartsType>,

  init(recordingManager: RecordingManager) {
    this.recordingManager = recordingManager;
    this.liveTracingManager = new LiveTracingManager();
    this.initBuffers();
    // if (this.timer) clearInterval(this.timer);
    // this.timer = setInterval(() => this.tick(), 1000);
    this.liveTracingManager.registerDataCallback((data) => this.processData(data));
  },

  initBuffers() {
    CONFIG.CLUSTERS.forEach(c => this.seriesBuffers[c.id] = [Array(HISTORY_LIMIT).fill(0), Array(HISTORY_LIMIT).fill(0)]);
    CONFIG.STATS.forEach(s => this.seriesBuffers[s.id] = s.readouts.map(() => Array(HISTORY_LIMIT).fill(0)));
  },

  triggerResize() {
    setTimeout(() => Object.values(this.chartRegistry).forEach(c => c.resize()), 150);
    setTimeout(() => Object.values(this.chartRegistry).forEach(c => c.resize()), 300);
  },

  toggleSeries(chartId: string, seriesName: string) {
    const key = `${chartId}_${seriesName}`;
    this.hiddenSeries[key] = !this.hiddenSeries[key];
  },

  toggleExpand(id: string | null) {
    this.expandedCardId = this.expandedCardId === id ? null : id;
    if (this.expandedCardId) document.body.classList.add('overflow-hidden');
    else document.body.classList.remove('overflow-hidden');
    this.triggerResize();
  },

  async togglePause() {
    if (!this.recordingManager) {
      console.warn("RecordingManager is not initialized. Cannot toggle pause.");
      return;
    }

    if (!this.liveTracingManager) {
      console.warn("LiveTracingManager is not initialized. Cannot toggle pause.");
      return;
    }
    
    this.isTracing = !this.isTracing;

    if (this.isTracing) {
      const target = this.recordingManager.currentTarget;
      if (target) {
        await this.liveTracingManager.start(target);
      } else {
        this.isTracing = false;
        console.warn("No recording target available to start live tracing.");
      }
    } else {
      await this.liveTracingManager.stop();
    }
  },

  processData(data: Record<string, unknown>) {
    
    let nowMs = Date.now();
    if (this.timeline.length > 0 && nowMs <= this.timeline[this.timeline.length - 1]) {
      nowMs = this.timeline[this.timeline.length - 1] + 1;
    }
    this.timeline.shift();
    this.timeline.push(nowMs);

    const cpuData = data.cpu as Record<string, Array<{ load: number; freq: number }>>;
    CONFIG.CLUSTERS.forEach(c => {
      const metrics = cpuData[c.id];
      const avgL = parseFloat((metrics.reduce((s, x) => s + x.load, 0) / c.cores).toFixed(2));
      const avgF = parseFloat((metrics.reduce((s, x) => s + x.freq, 0) / c.cores).toFixed(2));

      this.latestMetrics[`${c.id}_load`] = `${avgL.toFixed(2)}%`;
      this.latestMetrics[`${c.id}_freq`] = `${avgF.toFixed(2)} GHz`;

      this.seriesBuffers[c.id][0].shift(); this.seriesBuffers[c.id][0].push(avgL);
      this.seriesBuffers[c.id][1].shift(); this.seriesBuffers[c.id][1].push(avgF);
    });

    const temp = data.temp as Record<string, number>;
    const power = data.power as Record<string, number>;
    const fps = data.fps as number;
    const eff = power.cpu > 0 ? (fps / power.cpu) : 0;

    this.latestMetrics['temp-soc'] = `${Math.round(temp.soc)}°C`;
    this.latestMetrics['temp-gpu'] = `${Math.round(temp.gpu)}°C`;
    this.latestMetrics['power-cpu'] = `${Math.round(power.cpu)}mW`;
    this.latestMetrics['power-gpu'] = `${Math.round(power.gpu)}mW`;
    this.latestMetrics['power-ddr'] = `${Math.round(power.ddr)}mW`;
    this.latestMetrics['fps'] = Math.round(fps);
    this.latestMetrics['eff'] = eff.toFixed(2).substring(0, 4);

    this.seriesBuffers['temp'][0].shift(); this.seriesBuffers['temp'][0].push(temp.soc);
    this.seriesBuffers['temp'][1].shift(); this.seriesBuffers['temp'][1].push(temp.gpu);
    this.seriesBuffers['power'][0].shift(); this.seriesBuffers['power'][0].push(power.cpu);
    this.seriesBuffers['power'][1].shift(); this.seriesBuffers['power'][1].push(power.gpu);
    this.seriesBuffers['power'][2].shift(); this.seriesBuffers['power'][2].push(power.ddr);
    this.seriesBuffers['fps'][0].shift(); this.seriesBuffers['fps'][0].push(fps);
    this.seriesBuffers['eff'][0].shift(); this.seriesBuffers['eff'][0].push(eff);

    m.redraw();
  },

  tick() {
    if (!this.isTracing) return;

    const data = DataSimulator.fetchMetrics();
    this.processData(data);
  }
};

/**
 * ============================================================================
 * 5. ECHARTS COMPONENT & OPTIONS BUILDER
 * ============================================================================
 */

interface EChartAttrs {
  id: string;
  options: echarts.EChartsCoreOption;
  isExpanded?: boolean;
}

interface EChartState {
  resizeObserver?: ResizeObserver;
}

const EChartComponent: m.Component<EChartAttrs, EChartState> = {
  oncreate(vnode: m.VnodeDOM<EChartAttrs, EChartState>) {
    const chart = echarts.init(vnode.dom as HTMLElement);
    State.chartRegistry[vnode.attrs.id] = chart;
    chart.setOption(vnode.attrs.options);

    vnode.state.resizeObserver = new ResizeObserver(() => {
      chart.resize();
    });
    vnode.state.resizeObserver.observe(vnode.dom);
  },

  onupdate(vnode: m.VnodeDOM<EChartAttrs, EChartState>) {
    const chart = State.chartRegistry[vnode.attrs.id];
    if (chart) {
      chart.setOption(vnode.attrs.options, { notMerge: false, lazyUpdate: true });
    }
  },

  onremove(vnode: m.VnodeDOM<EChartAttrs, EChartState>) {
    if (vnode.state.resizeObserver) {
      vnode.state.resizeObserver.disconnect();
    }
    const chart = State.chartRegistry[vnode.attrs.id];
    if (chart) {
      chart.dispose();
      delete State.chartRegistry[vnode.attrs.id];
    }
  },

  view(vnode: m.Vnode<EChartAttrs, EChartState>) {
    const heightClass = vnode.attrs.isExpanded ? "h-[calc(100vh-160px)]" : "h-[300px]";
    return m("div", { class: `w-full transition-[height] duration-300 ease-in-out ${heightClass}` });
  }
};

const OptionsBuilder = {
  getSharedOptions(ext: any): echarts.EChartsCoreOption {
    return {
      animation: false,
      legend: { show: false },
      tooltip: { trigger: 'axis', confine: true, axisPointer: { animation: false }, textStyle: { fontWeight: 'normal', fontSize: 12 }, animation: false },
      grid: { top: 30, left: 10, right: 10, bottom: 10, containLabel: true },
      xAxis: { type: 'category', data: [...State.timeline], boundaryGap: false, axisLabel: { show: false }, axisTick: { show: false }, axisLine: { show: false } },
      yAxis: { splitLine: { lineStyle: { type: 'dashed', color: '#e5e7eb' } }, axisLabel: { fontSize: 10, fontWeight: 300 } },
      ...ext
    };
  },

  formatTooltip(params: any[], readouts: StatReadout[] | null = null) {
    if (!params || !params.length || params[0] === undefined) return '';
    const timestamp = Number(params[0].axisValue);
    const timeStr = isNaN(timestamp) ? '' : new Date(timestamp).toLocaleTimeString([], { hour12: false, minute: '2-digit', second: '2-digit' });

    let html = `<div class="p-1 min-w-[140px] text-xs font-light"><div class="font-normal mb-1 border-b border-gray-100 pb-1 text-gray-500">${timeStr}</div>`;

    params.forEach(p => {
      if (!p || p.value === undefined) return;
      let unit = p.seriesName.includes('Load') ? '%' : ' GHz';
      let bgStyle = p.seriesName.includes('Load') ? `background-color: ${p.color}15; padding: 1px 4px; border-radius: 3px;` : '';

      if (readouts) {
        const readout = readouts.find(r => r.label === p.seriesName);
        if (readout) {
          unit = readout.unit;
          bgStyle = readout.area ? `background-color: ${p.color}15; padding: 1px 4px; border-radius: 3px;` : '';
        }
      }

      const marker = `<span style="display:inline-block;margin-right:5px;border-radius:10px;width:10px;height:10px;background-color:${p.color};"></span>`;
      html += `<div class="flex justify-between items-center gap-4 mt-1">
                <span>${marker} ${p.seriesName}</span>
                <span style="font-family: ui-monospace, SFMono-Regular, monospace; color: ${p.color}; ${bgStyle}">${Number(p.value).toFixed(2)}${unit}</span>
            </div>`;
    });
    return html + `</div>`;
  },

  getClusterOptions(c: ClusterConfig) {
    const buffers = State.seriesBuffers[c.id];
    const selected = {
      'Avg Load': !State.hiddenSeries[`${c.id}_Avg Load`],
      'Avg Freq': !State.hiddenSeries[`${c.id}_Avg Freq`]
    };

    return this.getSharedOptions({
      legend: { show: false, selected },
      tooltip: { trigger: 'axis', confine: true, formatter: (p: any) => this.formatTooltip(p) },
      grid: { top: 30, left: 10, right: 15, bottom: 10, containLabel: true },
      yAxis: [
        { type: 'value', min: 0, max: 100, axisLabel: { formatter: '{value}%', fontWeight: 300 } },
        { type: 'value', min: 0, max: c.maxGHz, axisLabel: { formatter: (v: number) => v.toFixed(1), fontWeight: 300 }, splitLine: { show: false } }
      ],
      series: [
        { id: `${c.id}_s0`, name: `Avg Load`, type: 'line', showSymbol: false, smooth: true, itemStyle: { color: c.color }, areaStyle: { color: c.color, opacity: 0.2 }, lineStyle: { width: 1.2, color: c.color, opacity: 0.2, type: 'solid' }, data: [...buffers[0]], yAxisIndex: 0 },
        { id: `${c.id}_s1`, name: `Avg Freq`, type: 'line', showSymbol: false, smooth: true, itemStyle: { color: c.color }, lineStyle: { width: 1.5, color: c.color, type: 'solid' }, data: [...buffers[1]], yAxisIndex: 1 }
      ]
    });
  },

  getStatOptions(s: StatConfig) {
    const buffers = State.seriesBuffers[s.id];
    const selected: Record<string, boolean> = {};
    s.readouts.forEach(r => selected[r.label] = !State.hiddenSeries[`${s.id}_${r.label}`]);

    return this.getSharedOptions({
      legend: { show: false, selected },
      tooltip: { trigger: 'axis', confine: true, formatter: (p: any) => this.formatTooltip(p, s.readouts) },
      yAxis: { scale: s.range ? true : false, min: s.range ? s.range[0] : undefined, max: s.range ? s.range[1] : undefined  , axisLabel: { formatter: `{value}${s.readouts[0].unit}`, fontWeight: 300 } },
      series: s.readouts.map((r, idx) => ({
        id: `${s.id}_s${idx}`, name: r.label, type: 'line', showSymbol: false, smooth: true, lineStyle: { width: 1.2 },
        itemStyle: { color: r.color }, areaStyle: r.area ? { opacity: 0.05 } : null, data: [...buffers[idx]]
      }))
    });
  }
};

/**
 * ============================================================================
 * 6. MITHRIL VIEW COMPONENTS (UI)
 * ============================================================================
 */
class Toolbar implements m.Component {
  view() {
    const gridBtnClass = State.layoutMode === 'grid' ? "bg-blue-600 text-white shadow-sm font-normal" : "text-gray-400 font-light";
    const listBtnClass = State.layoutMode === 'list' ? "bg-blue-600 text-white shadow-sm font-normal" : "text-gray-400 font-light";
    // const pauseBtnClass = State.isTracing ? "text-blue-600 hover:bg-gray-50" : "text-green-600 hover:bg-gray-50";

    return m("header", { class: "mb-8 flex flex-wrap items-center gap-3" }, [

      // View Toggle
      m("div", { class: "flex bg-white rounded-lg shadow-sm border border-gray-100 p-1 flex-shrink-0" }, [
        m("button", { class: `px-4 py-1.5 rounded-md text-sm transition-all ${gridBtnClass}`, onclick: () => { State.layoutMode = 'grid'; State.triggerResize(); } }, "Grid"),
        m("button", { class: `px-4 py-1.5 rounded-md text-sm transition-all ${listBtnClass}`, onclick: () => { State.layoutMode = 'list'; State.triggerResize(); } }, "List")
      ]),

      // Search Filter
      m("div", { class: "relative flex-1 min-w-[200px]" }, [
        m("input", {
          type: "text",
          placeholder: "Filter clusters or stats...",
          class: "w-full pl-10 pr-4 py-2 bg-white border border-gray-200 rounded-lg shadow-sm focus:ring-2 focus:ring-blue-500 outline-none text-sm font-light text-gray-600",
          oninput: (e: Event) => State.filterQuery = (e.target as HTMLInputElement).value.toLowerCase()
        }),
        Icons.search
      ]),

      // Pause Button
      // m("button", {
      //   class: `flex items-center gap-2 bg-white px-4 py-2 rounded-lg shadow-sm border border-gray-100 text-sm font-normal transition-all flex-shrink-0 min-w-[105px] justify-center ${pauseBtnClass}`,
      //   onclick: () => State.togglePause()
      // }, [
      //   State.isTracing ? Icons.pause : Icons.resume,
      //   m("span", State.isTracing ? "Pause" : "Resume")
      // ]),

      // Status Badge
      m("button", {
        class: `inline-flex items-center px-3 py-1.5 rounded-md text-sm bg-gray-800 text-white border border-gray-700 shadow-sm hover:bg-gray-700 active:scale-95 transition-all flex-shrink-0 ${State.isTracing ? '' : 'opacity-80 grayscale-[0.5]'}`,
        onclick: () => State.togglePause()
      }, [
        Icons.dot(State.isTracing),
        m("span", { class: "font-normal", style: "font-family: ui-monospace, SFMono-Regular, monospace;" }, State.targetName),
        m("span", { class: "ml-2 text-[10px] uppercase opacity-50" }, State.isTracing ? "(Live)" : "(Stopped)")
      ]),

      // Save Button
      m("button", {
        class: "px-5 py-2 bg-gray-800 text-white text-sm rounded-lg shadow-sm hover:bg-gray-700 transition-colors flex items-center gap-2 flex-shrink-0 font-normal",
        onclick: (e: Event) => {
          const target = e.currentTarget as HTMLElement;
          target.classList.add('!bg-green-600');
          setTimeout(() => target.classList.remove('!bg-green-600'), 2000);
        }
      }, [Icons.save, "Save"])
    ]);
  }
}

interface ClusterCardAttrs {
  cluster: ClusterConfig;
}

const ClusterCard: m.Component<ClusterCardAttrs> = {
  view(vnode) {
    const c = vnode.attrs.cluster;
    const isExpanded = State.expandedCardId === c.id;

    // Tailwind Card Styling (handles normal vs expanded modal state)
    const cardClasses = `bg-white p-5 shadow-sm border border-gray-100 overflow-hidden group transition-all duration-200 ease-in-out relative ${isExpanded
      ? '!fixed inset-0 w-screen h-screen z-[100] !m-0 !rounded-none p-8'
      : 'rounded-xl min-w-0'
      }`;

    const baseLabelClass = "transition-all duration-200 cursor-pointer select-none hover:opacity-70 hover:-translate-y-[1px] active:translate-y-0";

    return m("div", { class: cardClasses }, [
      m("div", { class: "flex justify-between items-start mb-2 flex-nowrap text-gray-500" }, [
        m("h2", { class: Theme.styles.title, style: `color: ${c.color}` }, c.name),
        m("div", { class: "flex items-center gap-3" }, [
          m("div", { class: "flex gap-3 text-right" }, [

            // Metric Toggle: Load
            m("div", {
              class: `${baseLabelClass} ${State.hiddenSeries[`${c.id}_Avg Load`] ? '!opacity-25 grayscale' : ''}`,
              onclick: () => State.toggleSeries(c.id, 'Avg Load')
            }, [
              m("span", { class: Theme.styles.statLabel }, "Avg Load"),
              m("div", {
                class: "text-[10px] px-1.5 py-0 mt-0.5 rounded font-normal flex items-center justify-end",
                style: `font-family: ui-monospace, SFMono-Regular, monospace; color: ${c.color}; background-color: ${c.color}15; min-width: 48px; height: 1.2rem;`
              }, State.latestMetrics[`${c.id}_load`] || '--%')
            ]),

            // Metric Toggle: Freq
            m("div", {
              class: `border-l border-gray-100 pl-3 ${baseLabelClass} ${State.hiddenSeries[`${c.id}_Avg Freq`] ? '!opacity-25 grayscale' : ''}`,
              onclick: () => State.toggleSeries(c.id, 'Avg Freq')
            }, [
              m("span", { class: Theme.styles.statLabel }, "Avg Freq"),
              m("div", {
                class: "text-[10px] px-1.5 py-0 mt-0.5 rounded font-normal flex items-center justify-end",
                style: `font-family: ui-monospace, SFMono-Regular, monospace; color: ${c.color}; min-width: 58px; height: 1.2rem;`
              }, State.latestMetrics[`${c.id}_freq`] || '-- GHz')
            ])

          ]),
          m("button", {
            class: "p-1 rounded-md hover:bg-gray-100 text-gray-400 opacity-0 transition-opacity duration-200 group-hover:opacity-100",
            onclick: () => State.toggleExpand(c.id)
          }, Icons.expand)
        ])
      ]),
      m(EChartComponent, { id: c.id, options: OptionsBuilder.getClusterOptions(c), isExpanded })
    ]);
  }
};

interface StatCardAttrs {
  stat: StatConfig;
}

const StatCard: m.Component<StatCardAttrs> = {
  view(vnode) {
    const s = vnode.attrs.stat;
    const isExpanded = State.expandedCardId === s.id;

    const cardClasses = `bg-white p-6 shadow-sm border border-gray-100 overflow-hidden group transition-all duration-200 ease-in-out relative ${isExpanded
      ? '!fixed inset-0 w-screen h-screen z-[100] !m-0 !rounded-none p-8'
      : 'rounded-xl min-w-0'
      }`;

    const baseLabelClass = "transition-all duration-200 cursor-pointer select-none hover:opacity-70 hover:-translate-y-[1px] active:translate-y-0";

    return m("div", { class: cardClasses }, [
      m("div", { class: "flex justify-between items-start mb-4 flex-nowrap text-gray-500" }, [
        m("h2", { class: Theme.styles.title, style: `color: ${s.color}` }, s.title),
        m("div", { class: "flex items-center gap-3" }, [
          m("div", { class: "flex gap-3 text-right" }, s.readouts.map((r, idx) => {
            const bgStyle = r.area ? `${r.color}15` : 'transparent';
            const borderClass = idx > 0 ? "border-l border-gray-100 pl-3" : "";
            const inactiveClass = State.hiddenSeries[`${s.id}_${r.label}`] ? "!opacity-25 grayscale" : "";

            return m("div", {
              class: `${baseLabelClass} ${borderClass} ${inactiveClass}`,
              onclick: () => State.toggleSeries(s.id, r.label)
            }, [
              m("span", { class: Theme.styles.statLabel }, r.label),
              m("div", {
                class: "text-[10px] px-1.5 py-0 mt-0.5 rounded font-normal flex items-center justify-end",
                style: `font-family: ui-monospace, SFMono-Regular, monospace; color: ${r.color}; height: 1.2rem; min-width: 48px; background-color: ${bgStyle};`
              }, State.latestMetrics[r.id] || '--' + r.unit)
            ]);
          })),
          m("button", {
            class: "p-1 rounded-md hover:bg-gray-100 text-gray-400 opacity-0 transition-opacity duration-200 group-hover:opacity-100",
            onclick: () => State.toggleExpand(s.id)
          }, Icons.expand)
        ])
      ]),
      m(EChartComponent, { id: s.id, options: OptionsBuilder.getStatOptions(s), isExpanded })
    ]);
  }
};

export interface LiveTracingPageAttrs {
  readonly recMgr: RecordingManager;
}

export class SystemMonitor implements m.ClassComponent<LiveTracingPageAttrs> {
  constructor() {
    // console.log("SystemMonitor.constructor()");
  }

  oninit(vnode: m.Vnode<LiveTracingPageAttrs, this>): void {
    // console.log("SystemMonitor.oninit()");
    const recMgr = vnode.attrs.recMgr;
    State.init(recMgr);
    window.addEventListener('resize', () => State.triggerResize());
    document.addEventListener('keydown', e => {
      if (e.key === 'Escape' && State.expandedCardId) {
        State.toggleExpand(null);
        m.redraw();
      }
    });
  }

  onupdate(vnode: m.VnodeDOM<LiveTracingPageAttrs, this>) {
    // console.log("SystemMonitor.onupdate()");
    const recMgr = vnode.attrs.recMgr;
    const target = recMgr.currentTarget;
    if (target) {
      State.targetName = target.name || 'Unknown Device';
    }
  }

  oncreate(vnode: m.VnodeDOM<LiveTracingPageAttrs, this>) {
    // console.log("SystemMonitor.oncreate()");
    const recMgr = vnode.attrs.recMgr;
    const target = recMgr.currentTarget;
    if (target) {
      State.targetName = target.name || 'Unknown Device';
    }
  }

  view(_vnode: m.Vnode<LiveTracingPageAttrs, this>) {
    // console.log("SystemMonitor.view()");
    // const recMgr = vnode.attrs.recMgr;
    // const target = recMgr.currentTarget;
    const cpuGridClass = State.layoutMode === 'grid' ? 'md:grid-cols-2 lg:grid-cols-3' : 'grid-cols-1';
    const statGridClass = State.layoutMode === 'grid' ? 'md:grid-cols-2' : 'grid-cols-1';
    return m("div", { class: "p-2 md:p-2 text-gray-700 font-light w-full" }, [

      m(Toolbar),

      // Core Load Grid
      m("div", { class: `grid gap-6 transition-all duration-300 ease-in-out mb-6 ${cpuGridClass}` },
        CONFIG.CLUSTERS
          .filter(c => c.name.toLowerCase().includes(State.filterQuery))
          .map(c => m(ClusterCard, { key: c.id, cluster: c }))
      ),

      // Hardware Stats Grid
      m("div", { class: `grid gap-6 transition-all duration-300 ease-in-out ${statGridClass}` },
        CONFIG.STATS
          .filter(s => s.title.toLowerCase().includes(State.filterQuery))
          .map(s => m(StatCard, { key: s.id, stat: s }))
      )
    ]);
  }
}