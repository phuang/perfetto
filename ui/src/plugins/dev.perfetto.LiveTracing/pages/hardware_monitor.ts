import * as echarts from 'echarts';

type EChartInstance = echarts.ECharts;

const CORES = { BIG: 1, MID: 4, LITTLE: 2 };
const HISTORY_LIMIT = 60;

const Theme = {
    colors: {
        lit: '#10b981', mid: '#f59e0b', big: '#ef4444',
        temp: '#f97316', power: '#ec4899', fps: '#3b82f6'
    },
    styles: {
        title: "text-sm tracking-wider whitespace-nowrap font-normal opacity-90",
        statLabel: "text-[9px] text-gray-400 uppercase font-medium block"
    }
};

const CONFIG = {
    CLUSTERS: [
        { id: 'little', name: 'Lit Cluster', color: Theme.colors.lit, cores: CORES.LITTLE, maxGHz: 1.8 },
        { id: 'mid',    name: 'Mid Cluster', color: Theme.colors.mid, cores: CORES.MID,    maxGHz: 2.4 },
        { id: 'big',    name: 'Big Cluster', color: Theme.colors.big, cores: CORES.BIG,    maxGHz: 3.2 }
    ],
    STATS: [
        {
            id: 'temp', title: 'Temperatures (°C)', color: Theme.colors.temp,
            readouts: [
                { id: 'temp-pkg', label: 'CPU Pkg', color: Theme.colors.temp, unit: '°C' },
                { id: 'temp-gpu', label: 'GPU Core', color: '#8b5cf6', unit: '°C' }
            ]
        },
        {
            id: 'power', title: 'Power Consumption (mW)', color: Theme.colors.power,
            readouts: [
                { id: 'power-pkg', label: 'Pkg', color: Theme.colors.power, unit: 'mW', area: true },
                { id: 'power-dram', label: 'DRAM', color: '#06b6d4', unit: 'mW', area: true }
            ]
        },
        {
            id: 'fps', title: 'Frame Rate (FPS)', color: Theme.colors.fps,
            readouts: [{ id: 'fps', label: 'FPS', color: Theme.colors.fps, unit: '', area: true }]
        },
        {
            id: 'eff', title: 'Efficiency (FPS/mW)', color: Theme.colors.lit,
            readouts: [{ id: 'eff', label: 'Eff', color: Theme.colors.lit, unit: '', area: true }]
        }
    ]
};

/**
 * Mounts the hardware monitor UI into the provided container element.
 * This function injects HTML/CSS and wires the TypeScript controllers.
 */
export function mountHardwareMonitor(container: HTMLElement) {
    container.innerHTML = `
        <div class="p-4 md:p-8 text-gray-700 max-w-7xl mx-auto">
            <header class="mb-8 flex flex-col xl:flex-row xl:items-center justify-between gap-4">
                <div class="flex-shrink-0"><h1 class="text-3xl font-light text-gray-800">Advanced System Monitor</h1></div>
                <div class="flex flex-wrap items-center gap-3 flex-1">
                    <div class="flex bg-white rounded-lg shadow-sm border border-gray-100 p-1 flex-shrink-0">
                        <button id="btn-grid" class="px-4 py-1.5 rounded-md text-sm transition-all bg-blue-600 text-white shadow-sm">Grid</button>
                        <button id="btn-list" class="px-4 py-1.5 rounded-md text-sm transition-all text-gray-400">List</button>
                    </div>
                    <div class="relative flex-1 min-w-[200px]">
                        <input type="text" id="chart-filter" placeholder="Filter clusters or stats..." class="w-full pl-10 pr-4 py-2 bg-white border border-gray-200 rounded-lg shadow-sm focus:ring-2 focus:ring-blue-500 outline-none text-sm font-light">
                        <svg class="w-4 h-4 text-gray-400 absolute left-3 top-3" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21l-6-6m2-5a7 7 0 11-14 0 7 7 0 0114 0z"></path></svg>
                    </div>
                    <div class="flex items-center gap-2 bg-white p-2 rounded-lg shadow-sm border border-gray-100 flex-shrink-0">
                        <span class="text-sm font-light text-gray-400 ml-2">Rate:</span>
                        <select id="interval-select" class="text-sm border-none focus:ring-0 cursor-pointer font-medium text-blue-600 bg-transparent">
                            <option value="1000">1.0s</option>
                            <option value="500">0.5s</option>
                            <option value="0">Paused</option>
                        </select>
                    </div>
                    <button id="device-status-badge" class="tracing-active inline-flex items-center px-3 py-1.5 rounded-md text-sm bg-gray-800 text-white border border-gray-700 shadow-sm font-mono hover:bg-gray-700 active:scale-95 transition-all flex-shrink-0">
                        <svg class="status-dot w-3.5 h-3.5 mr-2 transition-colors duration-300" fill="currentColor" viewBox="0 0 24 24"><circle cx="12" cy="12" r="8"/></svg>
                        <span id="device-name-display" class="font-normal">Android Pixel 8 Pro</span>
                        <span id="tracing-label" class="ml-2 text-[10px] uppercase opacity-50">(Live)</span>
                    </button>
                    <button id="save-config-btn" class="px-5 py-2 bg-gray-800 text-white text-sm rounded-lg shadow-sm hover:bg-gray-700 transition-colors flex items-center gap-2 flex-shrink-0 font-medium">
                        Save
                    </button>
                </div>
            </header>

            <div class="grid-layout mb-6 grid-cols-1 md:grid-cols-2 lg:grid-cols-3" id="area-cpu"></div>
            <div class="grid-layout grid-cols-1 md:grid-cols-2" id="area-stats"></div>
        </div>
    `;

    injectStyles();
    UI.buildCards();
    Monitor._setupCharts();
    Monitor.initEventHandlers();
    Monitor.updateRate(1000);
    window.addEventListener('resize', () => Object.values(Monitor.instances).forEach(i => i.resize()));
    setTimeout(() => Object.values(Monitor.instances).forEach(i => i.resize()), 50);
    setTimeout(() => Object.values(Monitor.instances).forEach(i => i.resize()), 300);
}

function injectStyles() {
    const css = `
        body { background-color: #f3f4f6; transition: background-color 0.3s; }
        .chart-card { transition: all 0.2s ease-in-out; position: relative; min-width: 400px; }
        .chart-card.hidden { display: none; }
        .chart-container { height: 300px; transition: height 0.3s ease; }
        .chart-card.expanded { position: fixed; top: 0; left: 0; width: 100vw; height: 100vh; z-index: 100; margin: 0; border-radius: 0; padding: 2rem; background: white; min-width: auto; }
        .chart-card.expanded .chart-container { height: calc(100vh - 160px) !important; }
        body.modal-open { overflow: hidden; }
        .toggle-btn { opacity: 0; transition: all 0.2s; }
        .chart-card:hover .toggle-btn { opacity: 1; }
        .font-mono-stat { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
        .grid-layout { display: grid; gap: 1.5rem; transition: all 0.3s ease; }
        #device-status-badge { transition: all 0.2s ease; }
        .tracing-active .status-dot { color: #4ade80; }
        .tracing-inactive .status-dot { color: #f87171; opacity: 0.6; }
        .tracing-inactive { filter: grayscale(0.5); opacity: 0.8; }
        .metric-label { transition: all 0.2s; cursor: pointer; user-select: none; }
        .metric-label:hover { opacity: 0.7; transform: translateY(-1px); }
        .metric-label:active { transform: translateY(0px); }
        .metric-label.inactive { opacity: 0.25 !important; filter: grayscale(1); }
    `;
    const s = document.createElement('style');
    s.textContent = css;
    document.head.appendChild(s);
}

/* MONITOR ENGINE */
const Monitor = {
    instances: {} as Record<string, EChartInstance>,
    active: true,
    timer: 0 as number | undefined,
    timeline: Array(HISTORY_LIMIT).fill(''),

    initEventHandlers() {
        const interval = document.getElementById('interval-select') as HTMLSelectElement | null;
        interval?.addEventListener('change', () => Monitor.updateRate(Number(interval.value)));

        const btnGrid = document.getElementById('btn-grid');
        const btnList = document.getElementById('btn-list');
        btnGrid?.addEventListener('click', () => UI.setLayout('grid'));
        btnList?.addEventListener('click', () => UI.setLayout('list'));

        const filter = document.getElementById('chart-filter') as HTMLInputElement | null;
        filter?.addEventListener('input', () => UI.filterCharts(filter.value));

        const badge = document.getElementById('device-status-badge');
        badge?.addEventListener('click', () => Monitor.toggleTracing());

        const saveBtn = document.getElementById('save-config-btn');
        saveBtn?.addEventListener('click', (e) => Monitor.saveConfig(e as MouseEvent));

        document.addEventListener('keydown', e => {
            if (e.key === 'Escape') {
                const expanded = document.querySelector('.chart-card.expanded') as HTMLElement | null;
                if (expanded) {
                    const btn = expanded.querySelector('.toggle-btn') as HTMLElement | null;
                    if (btn) UI.toggleExpand(btn, expanded.getAttribute('data-key') || '');
                }
            }
        });
    },

    init() {
        // fill initial timeline
        for (let i = 0; i < HISTORY_LIMIT; i++) this.timeline[i] = '';
    },

    toggleTracing() {
        this.active = !this.active;
        const badge = document.getElementById('device-status-badge');
        const label = document.getElementById('tracing-label');
        badge?.classList.toggle('tracing-active', this.active);
        badge?.classList.toggle('tracing-inactive', !this.active);
        if (label) label.innerText = this.active ? "(Live)" : "(Stopped)";
    },

    updateRate(ms: number) {
        if (this.timer) clearInterval(this.timer);
        if (ms > 0) this.timer = window.setInterval(() => this._tick(), ms);
    },

    _tick() {
        if (!this.active) return;
        const now = new Date().toLocaleTimeString([], { hour12: false, minute: '2-digit', second: '2-digit' });
        this.timeline.shift();
        this.timeline.push(now);

        const rand = (min: number, range: number) => min + Math.random() * range;
        const cpuLoad = (base: number) => Math.max(5, Math.min(95, base + (Math.random() - 0.5) * 40));

        const data = {
            cpu: {
                little: Array.from({ length: CORES.LITTLE }, () => ({ load: cpuLoad(10), freq: rand(0.4, 1.0) })),
                mid: Array.from({ length: CORES.MID }, () => ({ load: cpuLoad(30), freq: rand(0.8, 1.2) })),
                big: Array.from({ length: CORES.BIG }, () => ({ load: cpuLoad(60), freq: rand(1.8, 1.2) }))
            },
            temp: { pkg: rand(40, 20), gpu: rand(38, 15) },
            power: { pkg: rand(800, 1500), dram: rand(100, 400) },
            fps: rand(59, 2)
        };

        this._updateUI(data);
    },

    _updateUI(data: any) {
        CONFIG.CLUSTERS.forEach(c => {
            const metrics = data.cpu[c.id];
            const avgL = parseFloat((metrics.reduce((s: number, x: any) => s + x.load, 0) / c.cores).toFixed(2));
            const avgF = parseFloat((metrics.reduce((s: number, x: any) => s + x.freq, 0) / c.cores).toFixed(2));

            const loadEl = document.getElementById(`val-${c.id}-load`);
            const freqEl = document.getElementById(`val-${c.id}-freq`);
            if (loadEl) loadEl.innerText = `${avgL.toFixed(2)}%`;
            if (freqEl) freqEl.innerText = `${avgF.toFixed(2)} GHz`;

            this._pushChartData(c.id, [avgL, avgF]);
        });

        const eff = data.power.pkg > 0 ? (data.fps / data.power.pkg) : 0;
        const setVal = (id: string, val: string | number) => { const el = document.getElementById(id); if (el) el.innerText = String(val); };

        setVal('val-temp-pkg-cur', `${Math.round(data.temp.pkg)}°C`);
        setVal('val-temp-gpu-cur', `${Math.round(data.temp.gpu)}°C`);
        setVal('val-power-pkg-cur', `${Math.round(data.power.pkg)}mW`);
        setVal('val-power-dram-cur', `${Math.round(data.power.dram)}mW`);
        setVal('val-fps-cur', Math.round(data.fps));
        setVal('val-eff-cur', eff.toFixed(2));

        this._pushChartData('temp', [data.temp.pkg, data.temp.gpu]);
        this._pushChartData('power', [data.power.pkg, data.power.dram]);
        this._pushChartData('fps', [data.fps]);
        this._pushChartData('eff', [eff]);
    },

    _pushChartData(key: string, values: number[]) {
        const chart = this.instances[key];
        if (!chart) return;
        const opt = chart.getOption();
        const seriesUpdate = values.map((val, idx) => {
            const series = (opt.series && opt.series[idx]) ? opt.series[idx] : null;
            if (!series) return { data: [] };
            const data = Array.isArray(series.data) ? [...series.data] : [];
            data.shift();
            data.push(val);
            return { data };
        });
        chart.setOption({ xAxis: { data: this.timeline }, series: seriesUpdate });
    },

    _setupCharts() {
        // CPU cluster charts
        CONFIG.CLUSTERS.forEach(c => {
            const el = document.getElementById(`chart-${c.id}`) as HTMLElement | null;
            if (!el) return;
            el.closest('.chart-card')?.setAttribute('data-key', c.id);
            const inst = echarts.init(el);
            const series = [
                {
                    name: `Avg Load`,
                    type: 'line',
                    showSymbol: false,
                    smooth: true,
                    areaStyle: { color: c.color, opacity: 0.2 },
                    lineStyle: { width: 1.2, color: c.color, opacity: 0.2, type: 'solid' },
                    data: Array(HISTORY_LIMIT).fill(0),
                    yAxisIndex: 0
                },
                {
                    name: `Avg Freq`,
                    type: 'line',
                    showSymbol: false,
                    smooth: true,
                    lineStyle: { width: 1.5, color: c.color, type: 'solid' },
                    data: Array(HISTORY_LIMIT).fill(0),
                    yAxisIndex: 1
                }
            ];
            inst.setOption(this._getSharedOptions({
                tooltip: {
                    trigger: 'axis',
                    confine: true,
                    formatter: (params: any) => {
                        let html = `<div class="p-1 min-w-[140px] text-xs font-light">`;
                        html += `<div class="font-medium mb-1 border-b border-gray-100 pb-1">${params[0].axisValue}</div>`;
                        params.forEach((p: any) => {
                            const unit = p.seriesName.includes('Load') ? '%' : ' GHz';
                            html += `<div class="flex justify-between items-center gap-4 mt-1"><span>${p.marker} ${p.seriesName}</span><span class="font-mono font-medium">${p.value.toFixed(2)}${unit}</span></div>`;
                        });
                        html += `</div>`;
                        return html;
                    }
                },
                grid: { top: 30, left: 10, right: 15, bottom: 10, containLabel: true },
                yAxis: [
                    { type: 'value', min: 0, max: 100, axisLabel: { formatter: '{value}%', fontWeight: 300 } },
                    { type: 'value', min: 0, max: c.maxGHz, axisLabel: { formatter: (value: number) => value.toFixed(1), fontWeight: 300 }, splitLine: { show: false } }
                ],
                series
            }));
            this.instances[c.id] = inst;
        });

        // Stats charts
        CONFIG.STATS.forEach(s => {
            const el = document.getElementById(`chart-${s.id}`) as HTMLElement | null;
            if (!el) return;
            el.closest('.chart-card')?.setAttribute('data-key', s.id);
            const inst = echarts.init(el);
            inst.setOption(this._getSharedOptions({
                yAxis: { scale: true, axisLabel: { formatter: `{value}${s.readouts[0].unit}`, fontWeight: 300 } },
                series: s.readouts.map((r: any) => ({
                    name: r.label, type: 'line', showSymbol: false, smooth: true, lineStyle: { width: 1.2 },
                    itemStyle: { color: r.color }, areaStyle: r.area ? { opacity: 0.05 } : null, data: Array(HISTORY_LIMIT).fill(0),
                    tooltip: { valueFormatter: (val: number) => val.toFixed(2) + (r.unit || '') }
                }))
            }));
            this.instances[s.id] = inst;
        });
    },

    _getSharedOptions(ext: any) {
        return {
            animation: false,
            legend: { show: false, selectedMode: 'multiple' },
            tooltip: { trigger: 'axis', confine: true, axisPointer: { animation: false } },
            grid: { top: 30, left: 10, right: 10, bottom: 10, containLabel: true },
            xAxis: { type: 'category', data: this.timeline, boundaryGap: false, axisLabel: { show: false }, axisTick: { show: false }, axisLine: { show: false } },
            yAxis: { splitLine: { lineStyle: { type: 'dashed', color: '#e5e7eb' } }, axisLabel: { fontSize: 10, fontWeight: 300 } },
            ...ext
        };
    },

    saveConfig(e?: MouseEvent) {
        const btn = e?.currentTarget as HTMLElement | null || document.getElementById('save-config-btn');
        btn?.classList.add('bg-green-600');
        setTimeout(() => btn?.classList.remove('bg-green-600'), 2000);
    }
};

/* UI CONTROLLER */
const UI = {
    buildCards() {
        const cpuArea = document.getElementById('area-cpu');
        const statArea = document.getElementById('area-stats');
        if (!cpuArea || !statArea) return;

        CONFIG.CLUSTERS.forEach(c => {
            cpuArea.insertAdjacentHTML('beforeend', `
                <div class="chart-card bg-white p-5 rounded-xl shadow-sm border border-gray-100 min-w-0 overflow-hidden group">
                    <div class="flex justify-between items-start mb-2 flex-nowrap">
                        <h2 class="${Theme.styles.title}" style="color: ${c.color}">${c.name}</h2>
                        <div class="flex items-center gap-3">
                            <div class="flex gap-3 text-right">
                                <div class="metric-label" data-chart="${c.id}" data-series="Avg Load">
                                    <span class="${Theme.styles.statLabel}">Avg Load</span>
                                    <div id="val-${c.id}-load" class="font-mono-stat text-[10px] px-1.5 py-0 mt-0.5 rounded font-medium flex items-center justify-end" style="color: ${c.color}; background-color: ${c.color}15; min-width: 48px; height: 1.2rem">--%</div>
                                </div>
                                <div class="border-l border-gray-100 pl-3 metric-label" data-chart="${c.id}" data-series="Avg Freq">
                                    <span class="${Theme.styles.statLabel}">Avg Freq</span>
                                    <div id="val-${c.id}-freq" class="font-mono-stat text-[10px] px-1.5 py-0 mt-0.5 rounded font-medium flex items-center justify-end" style="color: ${c.color}; min-width: 58px; height: 1.2rem">-- GHz</div>
                                </div>
                            </div>
                            <button class="toggle-btn p-1 rounded-md hover:bg-gray-100 text-gray-400">
                                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 8V4m0 0h4M4 4l5 5m11-1V4m0 0h-4m4 0l-5 5M4 16v4m0 0h4m-4 0l5-5m11 5l-5-5m5 5v-4m0 4h-4"></path></svg>
                            </button>
                        </div>
                    </div>
                    <div id="chart-${c.id}" class="chart-container w-full"></div>
                </div>`);
        });

        CONFIG.STATS.forEach(s => {
            statArea.insertAdjacentHTML('beforeend', `
                <div class="chart-card bg-white p-6 rounded-xl shadow-sm border border-gray-100 min-w-0 overflow-hidden group">
                    <div class="flex justify-between items-start mb-4 flex-nowrap">
                        <h2 class="${Theme.styles.title}" style="color: ${s.color}">${s.title}</h2>
                        <div class="flex items-center gap-3">
                            <div class="flex gap-3 text-right">
                                ${s.readouts.map((r: any, idx: number) => `
                                    <div class="metric-label ${idx > 0 ? 'border-l border-gray-100 pl-3' : ''}" data-chart="${s.id}" data-idx="${idx}">
                                        <span class="${Theme.styles.statLabel}">${r.label}</span>
                                        <div id="val-${r.id}-cur" class="font-mono-stat text-[10px] px-1.5 py-0 mt-0.5 rounded font-medium flex items-center justify-end" 
                                                 style="color: ${r.color}; height: 1.2rem; min-width: 48px; ${r.area ? `background-color: ${r.color}15` : ''}">--${r.unit}</div>
                                    </div>`).join('')}
                            </div>
                            <button class="toggle-btn p-1 rounded-md hover:bg-gray-100 text-gray-400">
                                <svg class="w-4 h-4" fill="none" stroke="currentColor" viewBox="0 0 24 24"><path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 8V4m0 0h4M4 4l5 5m11-1V4m0 0h-4m4 0l-5 5M4 16v4m0 0h4m-4 0l5-5m11 5l-5-5m5 5v-4m0 4h-4"></path></svg>
                            </button>
                        </div>
                    </div>
                    <div id="chart-${s.id}" class="chart-container w-full"></div>
                </div>`);
        });

        // attach metric toggle handlers (delegated)
        document.querySelectorAll('.metric-label').forEach(el => {
            el.addEventListener('click', (ev) => {
                const target = ev.currentTarget as HTMLElement;
                if (target.hasAttribute('data-series')) {
                    const chartId = target.getAttribute('data-chart') || '';
                    const seriesName = target.getAttribute('data-series') || '';
                    UI.toggleClusterMetric(target, chartId, seriesName);
                } else if (target.hasAttribute('data-idx')) {
                    const chartId = target.getAttribute('data-chart') || '';
                    const idx = Number(target.getAttribute('data-idx'));
                    UI.toggleStatMetric(target, chartId, idx);
                }
            });
        });

        // attach expand handlers
        document.querySelectorAll('.chart-card .toggle-btn').forEach(btn => {
            btn.addEventListener('click', (ev) => {
                const b = ev.currentTarget as HTMLElement;
                const card = b.closest('.chart-card') as HTMLElement | null;
                const key = card?.getAttribute('data-key') || '';
                UI.toggleExpand(b, key);
            });
        });
    },

    toggleClusterMetric(el: HTMLElement, chartId: string, seriesName: string) {
        const chart = Monitor.instances[chartId];
        if (!chart) return;
        const isHiding = !el.classList.contains('inactive');
        el.classList.toggle('inactive', isHiding);
        chart.dispatchAction({ type: isHiding ? 'legendUnSelect' : 'legendSelect', name: seriesName });
    },

    toggleStatMetric(el: HTMLElement, chartId: string, seriesIdx: number) {
        const chart = Monitor.instances[chartId];
        if (!chart) return;
        const isHiding = !el.classList.contains('inactive');
        el.classList.toggle('inactive', isHiding);
        const opt: any = chart.getOption();
        const name = opt.series[seriesIdx].name;
        chart.dispatchAction({ type: isHiding ? 'legendUnSelect' : 'legendSelect', name });
    },

    setLayout(mode: 'grid' | 'list') {
        const isGrid = mode === 'grid';
        ['area-cpu', 'area-stats'].forEach(id => {
            const el = document.getElementById(id);
            if (!el) return;
            el.classList.toggle('grid-cols-1', !isGrid);
            if (id === 'area-cpu') {
                el.classList.toggle('md:grid-cols-2', isGrid); el.classList.toggle('lg:grid-cols-3', isGrid);
            } else {
                el.classList.toggle('md:grid-cols-2', isGrid);
            }
        });
        const gridBtn = document.getElementById('btn-grid');
        const listBtn = document.getElementById('btn-list');
        if (gridBtn) gridBtn.className = isGrid ? 'px-4 py-1.5 rounded-md text-sm transition-all bg-blue-600 text-white shadow-sm' : 'px-4 py-1.5 text-sm transition-all text-gray-400';
        if (listBtn) listBtn.className = !isGrid ? 'px-4 py-1.5 rounded-md text-sm transition-all bg-blue-600 text-white shadow-sm' : 'px-4 py-1.5 text-sm transition-all text-gray-400';
        setTimeout(() => Object.values(Monitor.instances).forEach(i => i.resize()), 150);
    },

    toggleExpand(btn: HTMLElement, key: string) {
        const card = btn.closest('.chart-card') as HTMLElement | null;
        if (!card) return;
        const isExpanded = card.classList.toggle('expanded');
        document.body.classList.toggle('modal-open', isExpanded);
        setTimeout(() => Monitor.instances[key]?.resize(), 300);
    },

    filterCharts(val: string) {
        const query = val.toLowerCase().trim();
        document.querySelectorAll('.chart-card').forEach(card => {
            const content = card.innerHTML.toLowerCase();
            card.classList.toggle('hidden', query !== '' && !content.includes(query));
        });
        setTimeout(() => Object.values(Monitor.instances).forEach(i => i.resize()), 150);
    }
};