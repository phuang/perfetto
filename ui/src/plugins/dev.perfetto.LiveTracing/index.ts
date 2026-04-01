// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the \"License\");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an \"AS IS\" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

import m from 'mithril';
import {App} from '../../public/app';
import {PerfettoPlugin} from '../../public/plugin';
import {LiveTracingPage} from './pages/live_tracing_page';
import RecordTraceV2 from '../dev.perfetto.RecordTraceV2';

export default class implements PerfettoPlugin {
  static readonly id = 'dev.perfetto.LiveTracing';

  static onActivate(app: App) {
    app.sidebar.addMenuItem({
      section: 'trace_files',
      text: 'Live Data',
      href: '#!/live_tracing',
      icon: 'monitoring',
      sortOrder: 3,
    });

    app.pages.registerPage({
      route: '/live_tracing',
      render: () => {
        const recMgr = RecordTraceV2.getRecordingManager(app);
        return m(LiveTracingPage, {recMgr});
      },
    });
  }
}
