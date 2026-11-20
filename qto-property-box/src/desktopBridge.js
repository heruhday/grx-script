import { invoke, isTauri } from '@tauri-apps/api/core';

// Service adapter only. All markup, panels, editors, dialogs, layout and styling
// belong to reference/qto_property_box.html, never to the former React UI.
if (isTauri()) startDesktop();

function startDesktop() {
  const ui = window.qtoUIState;
  const byId = (id) => document.getElementById(id);
  const notify = (error) => ui.notify(String(error));
  let project = null;
  let projectBusy = false;
  let restoring = false;
  let timer;
  let saveChain = Promise.resolve();
  let selection = { entities: [], dwg: '', version: 0 };
  let lastRenderedVersion = -1;
  let polling = false;
  let states = [];
  const fields = { color: 'clr', layer: 'lyr', transparency: 'trp', visible: 'visible' };
  const uiKey = (entry) => 'qto-ui-project-v1:' + encodeURIComponent(entry.path);
  const projectStorageKeys = () => [uiKey(project), 'qto-location', 'qto-general-states',
    'qto-wbs-v1:' + project.name, 'qto-wbs-tree-v1:' + project.name,
    'qto-report-levels:' + project.name];

  function save() {
    if (!project || restoring) return saveChain;
    clearTimeout(timer);
    const entry = project;
    localStorage.setItem(uiKey(entry), JSON.stringify(window.qtoProjectUI.capture()));
    const value = { version: 1, measurements: ui.exportData(),
      storage: Object.fromEntries(projectStorageKeys().map((key) => [key, localStorage.getItem(key)])) };
    saveChain = saveChain.catch(() => {}).then(() => invoke('save_project_setting', {
      projectPath: entry.path, key: 'reference_ui_v1', value,
    }));
    return saveChain;
  }

  function scheduleSave() {
    if (!project || restoring || projectBusy) return;
    clearTimeout(timer);
    // Let the reference's own debounced UI snapshot finish before persisting it.
    timer = setTimeout(() => save().catch(notify), 650);
  }

  async function refreshProjects() {
    const entries = await invoke('list_recent_projects');
    window.qtoProjectsState.restore(entries);
    byId('projects-status').textContent = entries.length ? '' : 'Create a project or open an existing QTO project folder.';
  }

  async function loadDrawings() {
    if (!project) return;
    const entry = project;
    const drawings = await invoke('list_project_drawings', { projectPath: entry.path });
    if (project?.path === entry.path) window.qtoDrawingUI.restore(drawings);
  }

  async function refreshStates() {
    if (!project) return;
    states = await invoke('list_general_states', { projectPath: project.path });
    const select = byId('general-state');
    select.replaceChildren(new Option('Current values / New state', ''), ...states.map((entry) => new Option(entry.name, entry.name)));
  }

  function updateCadSelectionUI(result, entities = []) {
    const count = entities.length;
    const dwg = result.dwg || 'Drawing';
    const platform = result.platform || 'CAD';

    // 1. Preview CAD Card Header & Meta
    const titleEl = byId('cad-preview-title');
    if (titleEl) {
      titleEl.textContent = count > 0 ? `${dwg} (${platform})` : `${dwg} · No Selection`;
    }
    const metaEl = byId('cad-preview-meta');
    if (metaEl) {
      if (count > 0) {
        const totalL = entities.reduce((acc, ent) => acc + (Number(ent.l) || 0), 0);
        const totalA = entities.reduce((acc, ent) => acc + (Number(ent.a) || 0), 0);
        const parts = [`${count} object${count > 1 ? 's' : ''}`];
        if (totalL > 0) parts.push(`L: ${totalL.toFixed(3)}m`);
        if (totalA > 0) parts.push(`A: ${totalA.toFixed(3)}m²`);
        metaEl.textContent = parts.join(' · ');
      } else {
        metaEl.textContent = '0 objects selected';
      }
    }

    // 2. Hide fake presets bar since live CAD is active
    const presetsEl = byId('cad-presets');
    if (presetsEl) presetsEl.style.display = 'none';

    // 3. Visual Stage
    const stageEl = byId('cad-visual-stage');
    if (stageEl) {
      if (count === 0) {
        stageEl.innerHTML = '<span class="text-[11px] text-slate-500">No CAD objects selected. Select objects in CAD drawing.</span>';
      } else {
        const rows = entities.slice(0, 10).map((ent) => {
          const hand = ent.hand || ent.handle || '';
          const ty = (ent.ty || 'Entity').replace(/^AcDb/, '');
          const lyr = ent.lyr || '0';
          const lStr = ent.l != null ? `L:${Number(ent.l).toFixed(2)}m` : '';
          const aStr = ent.a != null ? `A:${Number(ent.a).toFixed(2)}m²` : '';
          const metric = [lStr, aStr].filter(Boolean).join(' ');
          return `<div class="flex items-center justify-between text-[10px] font-mono py-0.5 px-2 rounded bg-white/5 border border-white/10 text-slate-200">
            <span class="text-sky-400 font-semibold">${ty}</span>
            <span class="text-slate-400">#${hand}</span>
            <span class="text-emerald-400 truncate max-w-[80px]" title="Layer: ${lyr}">${lyr}</span>
            ${metric ? `<span class="text-amber-300">${metric}</span>` : ''}
          </div>`;
        }).join('');
        const more = count > 10 ? `<div class="text-[9px] text-slate-400 text-center font-mono py-0.5">+${count - 10} more objects</div>` : '';
        stageEl.innerHTML = `<div class="w-full h-full overflow-y-auto no-scrollbar py-1 space-y-1">${rows}${more}</div>`;
      }
    }

    // 4. CAD Event Bar (unhide and update)
    const barEl = byId('cad-event-bar');
    if (barEl) {
      barEl.style.setProperty('display', 'flex', 'important');
      const textEl = byId('cad-event-text');
      if (textEl) {
        textEl.textContent = `${platform}: ${count} object${count !== 1 ? 's' : ''} selected in ${dwg}`;
      }
      const btnEl = byId('btn-send-fake-cad');
      if (btnEl) {
        const btnSpan = btnEl.querySelector('span');
        if (btnSpan) btnSpan.textContent = count > 0 ? `Pick CAD (${count})` : 'Pick CAD';
        btnEl.disabled = count === 0;
      }
    }

    // 5. General Properties Panel
    const getCommon = (fn) => {
      if (!count) return '';
      const vals = entities.map(fn);
      return vals.every((v) => v === vals[0]) ? (vals[0] == null ? '' : String(vals[0])) : '<Varies>';
    };

    const clrInput = byId('general-color');
    if (clrInput && !clrInput.matches(':focus')) {
      clrInput.value = count > 0 ? getCommon((ent) => {
        if (ent.clr === 256) return 'ByLayer';
        if (ent.clr === 0) return 'ByBlock';
        if (ent.clr && typeof ent.clr === 'object') return `rgb(${ent.clr.r},${ent.clr.g},${ent.clr.b})`;
        return ent.clr != null ? ent.clr : '';
      }) : '';
    }

    const lyrInput = byId('general-layer');
    if (lyrInput && !lyrInput.matches(':focus')) {
      lyrInput.value = count > 0 ? getCommon((ent) => ent.lyr != null ? ent.lyr : '') : '';
    }

    const trpInput = byId('general-transparency');
    if (trpInput && !trpInput.matches(':focus')) {
      trpInput.value = count > 0 ? getCommon((ent) => {
        if (ent.trp === -2) return 'ByLayer';
        if (ent.trp === -1) return 'ByBlock';
        return ent.trp != null ? ent.trp : '';
      }) : '';
    }

    const visSelect = byId('general-visible');
    if (visSelect && !visSelect.matches(':focus')) {
      const val = count > 0 ? getCommon((ent) => ent.visible != null ? String(ent.visible) : '') : '';
      visSelect.value = val === '<Varies>' ? '' : val;
    }

    const genStatus = byId('general-status');
    if (genStatus) {
      genStatus.textContent = count > 0 ? `${count} CAD object(s) selected` : '';
    }

    // 6. Location Panel
    const bldgInput = byId('location-building');
    if (bldgInput && !bldgInput.matches(':focus')) {
      bldgInput.value = count > 0 ? getCommon((ent) => ent.building ?? ent.data?.building ?? ent.data?.props?.building ?? '') : '';
    }
    const floorInput = byId('location-floor');
    if (floorInput && !floorInput.matches(':focus')) {
      floorInput.value = count > 0 ? getCommon((ent) => ent.floor ?? ent.data?.floor ?? ent.data?.props?.floor ?? '') : '';
    }
    const areaInput = byId('location-area');
    if (areaInput && !areaInput.matches(':focus')) {
      areaInput.value = count > 0 ? getCommon((ent) => ent.area ?? ent.data?.area ?? ent.data?.props?.area ?? '') : '';
    }
    const customInput = byId('location-custom');
    if (customInput && !customInput.matches(':focus')) {
      customInput.value = count > 0 ? getCommon((ent) => ent.custom ?? ent.data?.custom ?? ent.data?.props?.custom ?? '') : '';
    }

    // 7. Pick CAD buttons in active row panels (.pick-sim-btn)
    const label = count > 0 ? `Pick CAD (${count} ${count === 1 ? 'entity' : 'entities'})` : 'Pick CAD';
    window.qtoCurrentSelectionLabel = label;
    document.querySelectorAll('.pick-sim-btn').forEach((btn) => {
      btn.style.setProperty('display', 'inline-flex', 'important');
      const span = btn.querySelector('span');
      if (span) span.textContent = label;
      else btn.textContent = label;
      btn.disabled = count === 0;
    });

    ensureApplyButton();
  }

  function ensureApplyButton() {
    const addRowBtn = byId('btn-add-row');
    if (!addRowBtn || byId('btn-apply-xdata')) return;
    const btn = document.createElement('button');
    btn.id = 'btn-apply-xdata';
    btn.type = 'button';
    btn.title = 'Apply properties and XData to CAD (F5)';
    btn.className = 'inline-flex h-8 items-center gap-1.5 rounded-full bg-sky-600 hover:bg-sky-500 px-3 text-[11px] font-semibold text-white shadow-sm border border-white/25 backdrop-blur-md hover:brightness-110 active:scale-95 transition';
    btn.innerHTML = '<span>☁ Apply</span><span class="rounded bg-sky-800/80 px-1 py-0.5 text-[9px] font-mono">F5</span>';
    addRowBtn.after(btn);
  }

  function updateCadOfflineUI() {
    const titleEl = byId('cad-preview-title');
    if (titleEl) titleEl.textContent = 'CAD Offline';
    const metaEl = byId('cad-preview-meta');
    if (metaEl) metaEl.textContent = '127.0.0.1:8123 disconnected';
    const stageEl = byId('cad-visual-stage');
    if (stageEl) stageEl.innerHTML = '<span class="text-[11px] text-slate-500">AutoCAD/GstarCAD not connected</span>';
    const textEl = byId('cad-event-text');
    if (textEl) textEl.textContent = 'CAD offline';
  }

  let consecutiveErrors = 0;
  let activeItemTracker = null;
  async function pollSelection() {
    if (polling) return;
    polling = true;
    try {
      const queryVersion = (selection.entities && selection.entities.length > 0) ? selection.version : 0;
      const result = await invoke('cad_poll_selection', { lastVersion: queryVersion, port: 8123 });
      consecutiveErrors = 0;

      const activeInfo = ui.getActiveItem ? ui.getActiveItem() : null;
      const currentItemId = activeInfo?.item?.id || null;
      const itemChanged = Boolean(currentItemId && currentItemId !== activeItemTracker);
      if (currentItemId) activeItemTracker = currentItemId;

      if (result.changed !== false) {
        selection = { ...result, entities: result.entities || [] };
        if (ui.syncCadSelection) {
          ui.syncCadSelection(selection.entities);
        }
        updateCadSelectionUI(result, selection.entities);
        lastRenderedVersion = selection.version;
      } else if (result.dwg !== selection.dwg) {
        selection = { entities: [], dwg: result.dwg, version: 0 };
        if (ui.syncCadSelection) {
          ui.syncCadSelection([]);
        }
        updateCadSelectionUI(result, []);
        lastRenderedVersion = 0;
      } else if (itemChanged && selection.entities && selection.entities.length > 0) {
        if (ui.syncCadSelection) {
          ui.syncCadSelection(selection.entities);
        }
        updateCadSelectionUI(selection, selection.entities);
      }
      ensureApplyButton();
    } catch {
      consecutiveErrors++;
      if (consecutiveErrors >= 3 && (selection.dwg || selection.entities.length)) {
        selection = { entities: [], dwg: '', version: 0 };
        lastRenderedVersion = -1;
        updateCadOfflineUI();
      }
    } finally {
      polling = false;
    }
  }

  async function enter(entry) {
    if (!entry) return;
    project = entry;
    restoring = true;
    selection = { entities: [], dwg: '', version: 0 };
    try {
      const saved = await invoke('get_project_setting', { projectPath: entry.path, key: 'reference_ui_v1' });
      for (const key of projectStorageKeys()) localStorage.removeItem(key);
      if (saved?.version === 1) {
        for (const [key, value] of Object.entries(saved.storage || {})) {
          if (projectStorageKeys().includes(key) && typeof value === 'string') localStorage.setItem(key, value);
        }
      }
      // Browser-only demo quantities must never become a new native project's data.
      ui.clearData();
      document.querySelectorAll('#general-panel input,#location-panel input,#location-panel textarea').forEach((input) => { input.value = ''; input.disabled = false; });
      byId('general-visible').value = '';
      if (saved?.measurements) ui.importData(saved.measurements);
      document.body.classList.remove('projects-home');
      byId('projects-screen').hidden = true;
      byId('workspace-project-name').textContent = entry.name;
      document.dispatchEvent(new CustomEvent('qto-project-open', { detail: entry }));
      await Promise.all([loadDrawings(), refreshStates()]);
      byId('project-dialog').close();
      byId('projects-status').textContent = '';
      if (entry.warning) notify(entry.warning);
    } catch (error) {
      project = null;
      document.body.classList.add('projects-home');
      byId('projects-screen').hidden = false;
      throw error;
    } finally { restoring = false; }
  }

  async function projectAction(command, args) {
    if (projectBusy) return;
    projectBusy = true;
    try { await save(); await enter(await invoke(command, args)); }
    catch (error) { byId('projects-status').textContent = String(error); notify(error); }
    finally { projectBusy = false; }
  }

  async function pickSelection() {
    try {
      await pollSelection();
      if (!selection.entities.length) throw Error('Select objects in CAD first.');
      ui.acceptCadSelection(selection.entities);
      await applyAllToCad();
      if (project) scheduleSave();
    } catch (error) { notify(error); }
  }

  async function drawingAction(command, args = {}) {
    if (!project) return;
    try {
      const entry = project;
      const result = await invoke(command, { projectPath: entry.path, ...args });
      if (project !== entry) return;
      if (result !== null) await loadDrawings();
    } catch (error) { byId('drawing-status').textContent = String(error); notify(error); }
  }

  window.qtoDesktop = {
    save: () => save().catch(notify),
    openRecent: (entry) => projectAction('load_project', { projectPath: entry.path }),
    pickSelection,
    applyAll: () => applyAllToCad(),
    openDrawing: async (ids) => {
      if (!project || !ids.length) return;
      try {
        for (const drawingId of ids) await invoke('open_project_drawing', { projectPath: project.path, drawingId });
        await pollSelection();
      } catch (error) { byId('drawing-status').textContent = String(error); notify(error); }
    },
    saveDrawingDescription: async (drawingId, short) => {
      const details = await invoke('get_drawing_details', { projectPath: project.path, drawingId });
      await invoke('save_drawing_details', { projectPath: project.path, drawingId, short: short.trim(), full: details.full });
      await loadDrawings();
    },
  };

  byId('project-open').onclick = () => projectAction('open_project');
  byId('project-refresh').onclick = () => refreshProjects().catch(notify);
  byId('project-create-form').onsubmit = (event) => {
    event.preventDefault();
    const name = byId('project-name').value.trim();
    if (name) projectAction('create_project', { name });
  };
  byId('project-back').onclick = async () => {
    try {
      await save();
      project = null;
      selection = { entities: [], dwg: '', version: 0 };
      document.body.classList.add('projects-home');
      byId('projects-screen').hidden = false;
      await refreshProjects();
    } catch (error) { notify(error); }
  };
  byId('drawing-add').onclick = () => drawingAction('add_project_drawings');
  byId('drawing-active').onclick = () => drawingAction('add_open_cad_drawings', { activeOnly: true });
  byId('drawing-open-all').onclick = () => drawingAction('add_open_cad_drawings', { activeOnly: false });
  byId('drawing-remove').onclick = () => drawingAction('remove_project_drawings', { ids: window.qtoDrawingUI.selected() });
  byId('general-state').onchange = () => {
    const state = states.find((entry) => entry.name === byId('general-state').value);
    if (!state) return;
    byId('general-state-name').value = state.name;
    for (const [name, key] of Object.entries(fields)) {
      byId('general-' + name).value = state.values[key];
      byId('general-' + name).disabled = state.actions[key] === 'nothing';
    }
    scheduleSave();
  };
  byId('general-state-save').onclick = async () => {
    try {
      if (!project) return;
      const name = byId('general-state-name').value.trim();
      const state = { name, values: {}, actions: {} };
      for (const [field, key] of Object.entries(fields)) {
        const input = byId('general-' + field);
        state.values[key] = input.value;
        state.actions[key] = input.disabled ? 'nothing' : 'update';
      }
      await invoke('save_general_state', { projectPath: project.path, state });
      await refreshStates();
      byId('general-state').value = name;
      byId('general-status').textContent = 'State saved.';
    } catch (error) { byId('general-status').textContent = String(error); }
  };
  byId('general-state-delete').onclick = async () => {
    try {
      const name = byId('general-state').value;
      if (!project || !name) return;
      await invoke('delete_general_state', { projectPath: project.path, name });
      await refreshStates();
      byId('general-state-name').value = '';
      byId('general-status').textContent = 'State deleted.';
    } catch (error) { byId('general-status').textContent = String(error); }
  };

  // General field direct edits applied to CAD (only touches the specified CAD property, never touches XData or other panels)
  async function applyGeneralFieldToCad(fieldName, val) {
    if (!selection.entities || !selection.entities.length) {
      await pollSelection();
    }
    if (!selection.entities || !selection.entities.length) return;
    const keyMap = { color: 'clr', layer: 'lyr', transparency: 'trp', visible: 'visible' };
    const propKey = keyMap[fieldName];
    if (!propKey) return;
    let propVal = val;
    if (propKey === 'clr') {
      if (String(val).toLowerCase() === 'bylayer') propVal = 256;
      else if (String(val).toLowerCase() === 'byblock') propVal = 0;
      else if (!isNaN(Number(val))) propVal = Number(val);
    } else if (propKey === 'trp') {
      if (String(val).toLowerCase() === 'bylayer') propVal = -2;
      else if (String(val).toLowerCase() === 'byblock') propVal = -1;
      else if (!isNaN(Number(val))) propVal = Number(val);
    } else if (propKey === 'visible') {
      propVal = val === 'true' || val === true;
    } else if (propKey === 'lyr') {
      propVal = String(val).trim() || '0';
    }

    const patches = selection.entities.map((ent) => ({
      handle: ent.hand || ent.handle,
      properties: { [propKey]: propVal },
    }));

    try {
      await invoke('cad_apply_qto_xdata', { patches, port: 8123 });
      const statusEl = byId('general-status');
      if (statusEl) statusEl.textContent = `Applied ${fieldName} (${propVal}) to ${patches.length} object(s)`;
      for (const ent of selection.entities) {
        ent[propKey] = propVal;
      }
      await pollSelection();
    } catch (e) {
      const statusEl = byId('general-status');
      if (statusEl) statusEl.textContent = `Apply error: ${e}`;
    }
  }

  ['color', 'layer', 'transparency', 'visible'].forEach((name) => {
    const el = byId('general-' + name);
    if (!el) return;
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        applyGeneralFieldToCad(name, el.value);
      }
    });
    el.addEventListener('change', () => applyGeneralFieldToCad(name, el.value));
  });

  document.querySelectorAll('#general-panel button[data-action]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const fieldEl = btn.closest('.general-field');
      const fieldName = fieldEl?.dataset?.generalField;
      const action = btn.dataset.action;
      if (!fieldName) return;
      const input = byId('general-' + fieldName);
      if (action === 'reset' || action === 'remove') {
        if (input) {
          if (fieldName === 'color') input.value = 'ByLayer';
          else if (fieldName === 'layer') input.value = '0';
          else if (fieldName === 'transparency') input.value = 'ByLayer';
          else if (fieldName === 'visible') input.value = 'true';
        }
        await applyGeneralFieldToCad(fieldName, input ? input.value : '');
      }
    });
  });

  // Pick CAD button, Apply button, and item change sync
  document.addEventListener('click', async (e) => {
    const applyBtn = e.target.closest('#btn-apply-xdata');
    if (applyBtn) {
      e.stopPropagation();
      e.preventDefault();
      await applyAllToCad();
      return;
    }

    const btn = e.target.closest('.pick-sim-btn, #btn-send-fake-cad');
    if (btn) {
      e.stopPropagation();
      e.preventDefault();
      pickSelection();
      return;
    }

    if (e.target.closest('input, textarea, select, [contenteditable="true"]')) {
      return;
    }

    setTimeout(() => {
      const activeInfo = ui.getActiveItem ? ui.getActiveItem() : null;
      if (activeInfo?.item && selection.entities && selection.entities.length > 0) {
        if (ui.syncCadSelection) {
          ui.syncCadSelection(selection.entities);
        }
        updateCadSelectionUI(selection, selection.entities);
      }
      ensureApplyButton();
    }, 30);
  }, true);

  // Location field direct edits applied to CAD (only touches that location key in data.props, never touches CAD properties or other panels)
  async function applyLocationFieldToCad(key, val) {
    if (!selection.entities || !selection.entities.length) {
      await pollSelection();
    }
    if (!selection.entities || !selection.entities.length) return;
    const patches = selection.entities.map((ent) => {
      const existingData = ent.data || {};
      const existingProps = existingData.props || {};
      return {
        handle: ent.hand || ent.handle,
        data: {
          ...existingData,
          props: {
            ...existingProps,
            [key]: val
          }
        }
      };
    });

    try {
      await invoke('cad_apply_qto_xdata', { patches, port: 8123 });
      const genStatus = byId('general-status');
      if (genStatus) genStatus.textContent = `Applied location ${key} to ${patches.length} object(s)`;
      for (const ent of selection.entities) {
        if (!ent.data) ent.data = {};
        if (!ent.data.props) ent.data.props = {};
        ent.data.props[key] = val;
      }
      await pollSelection();
    } catch (e) {
      const genStatus = byId('general-status');
      if (genStatus) genStatus.textContent = `Apply error: ${e}`;
    }
  }

  ['building', 'floor', 'area', 'custom'].forEach((name) => {
    const el = byId('location-' + name);
    if (!el) return;
    el.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        e.preventDefault();
        applyLocationFieldToCad(name, el.value);
      }
    });
    el.addEventListener('change', () => {
      applyLocationFieldToCad(name, el.value);
    });
  });

  // QTO item code/wbs/name direct edit applied to CAD (only touches item XData, does not touch general properties or location)
  async function applyItemXdataToCad() {
    if (!selection.entities || !selection.entities.length) {
      await pollSelection();
    }
    if (!selection.entities || !selection.entities.length) return;

    const activeInfo = ui.getActiveItem ? ui.getActiveItem() : null;
    const cat = activeInfo?.cat;
    const item = activeInfo?.item;
    if (!item) return;

    const typeKey = cat ? cat.id : 'length';
    const totalQty = item.rows ? item.rows.reduce((sum, r) => sum + (Number(r.value) || 0), 0) : 0;

    const patches = selection.entities.map((ent) => {
      const existingData = ent.data || {};
      const existingProps = existingData.props || {};
      const nextProps = { ...existingProps };

      if (item.code) {
        const nextTypes = { ...(nextProps.types || {}) };
        const entQty = Number(ent[typeKey === 'length' ? 'l' : (typeKey === 'area' || typeKey === 'wall' ? 'a' : (typeKey === 'solid' ? 'v' : 'l'))] || ent.l || ent.a || 0) || totalQty;

        const record = {
          code: item.code,
          qty: entQty,
          name: item.name || '',
          unit: item.unit || '',
          wbs: item.wbsCode || undefined
        };

        if (!nextTypes[typeKey]) nextTypes[typeKey] = [];
        const existingIdx = nextTypes[typeKey].findIndex((r) => r.code === item.code);
        if (existingIdx >= 0) nextTypes[typeKey][existingIdx] = record;
        else nextTypes[typeKey].push(record);

        nextProps.types = nextTypes;
        nextProps.code = item.code;
        nextProps.category = typeKey;
        if (item.wbsCode) nextProps.wbs = item.wbsCode;
      }

      return {
        handle: ent.hand || ent.handle,
        data: {
          ...existingData,
          props: nextProps,
          ...(item.code ? { code: item.code } : {})
        }
      };
    });

    try {
      await invoke('cad_apply_qto_xdata', { patches, port: 8123 });
      const itemCodeMsg = item.code ? ` (${item.code})` : '';
      notify(`Applied QTO item${itemCodeMsg} to ${patches.length} CAD object(s)`);
      for (const p of patches) {
        const ent = selection.entities.find(e => (e.hand || e.handle) === p.handle);
        if (ent) ent.data = p.data;
      }
      await pollSelection();
      if (project) scheduleSave();
    } catch (e) {
      notify(`Apply error: ${e}`);
    }
  }

  document.addEventListener('change', async (e) => {
    const codeEl = e.target.closest('#input-item-code');
    const wbsEl = e.target.closest('#input-item-wbs-code');
    const nameEl = e.target.closest('#input-item-name');
    if (!codeEl && !wbsEl && !nameEl) return;

    const activeInfo = ui.getActiveItem ? ui.getActiveItem() : null;
    if (!activeInfo?.item) return;
    if (codeEl) activeInfo.item.code = codeEl.value.trim() || activeInfo.item.code;
    else if (wbsEl) activeInfo.item.wbsCode = wbsEl.value.trim();
    else if (nameEl) activeInfo.item.name = nameEl.value.trim() || activeInfo.item.name;

    if (activeInfo.cat) {
      activeInfo.cat.expanded = true;
      if (ui.state) {
        if (!ui.state.visibleCategoryIds.includes(activeInfo.cat.id)) {
          ui.state.visibleCategoryIds.push(activeInfo.cat.id);
        }
        ui.state.activeCategoryId = activeInfo.cat.id;
        ui.state.activeItemId = activeInfo.item.id;
        ui.state.detailExpanded = true;
      }
    }

    await applyItemXdataToCad();
    const isEditing = document.activeElement && document.activeElement.matches('input, textarea, select, [contenteditable="true"]');
    if (!isEditing && ui.render) ui.render();
  });

  // Apply all current UI properties (Item QTO + Location + General) to selected CAD objects
  async function applyAllToCad() {
    if (!selection.entities || !selection.entities.length) {
      await pollSelection();
    }
    if (!selection.entities || !selection.entities.length) {
      try {
        const fresh = await invoke('cad_poll_selection', { lastVersion: 0, port: 8123 });
        if (fresh && fresh.entities && fresh.entities.length > 0) {
          selection = { ...fresh, entities: fresh.entities };
          updateCadSelectionUI(fresh, selection.entities);
        }
      } catch {}
    }

    if (!selection.entities || !selection.entities.length) {
      notify('No CAD objects are selected. Select objects in CAD first.');
      return;
    }

    const activeInfo = ui.getActiveItem ? ui.getActiveItem() : null;
    const cat = activeInfo?.cat;
    const item = activeInfo?.item;
    const typeKey = cat ? cat.id : 'length';

    // 1. Ensure live CAD geometry is calculated into the item
    if (ui.syncCadSelection) {
      ui.syncCadSelection(selection.entities);
    }

    const totalQty = item?.rows ? item.rows.reduce((sum, r) => sum + (Number(r.value) || 0), 0) : 0;

    // 2. General properties
    const keyMap = { color: 'clr', layer: 'lyr', transparency: 'trp', visible: 'visible' };
    const properties = {};
    for (const [name, propKey] of Object.entries(keyMap)) {
      const el = byId('general-' + name);
      if (!el || el.value === '' || el.value === '<Varies>') continue;
      const val = el.value;
      if (propKey === 'clr') {
        if (String(val).toLowerCase() === 'bylayer') properties.clr = 256;
        else if (String(val).toLowerCase() === 'byblock') properties.clr = 0;
        else if (!isNaN(Number(val))) properties.clr = Number(val);
      } else if (propKey === 'trp') {
        if (String(val).toLowerCase() === 'bylayer') properties.trp = -2;
        else if (String(val).toLowerCase() === 'byblock') properties.trp = -1;
        else if (!isNaN(Number(val))) properties.trp = Number(val);
      } else if (propKey === 'visible') {
        properties.visible = val === 'true';
      } else if (propKey === 'lyr') {
        properties.lyr = val;
      }
    }

    // 3. Location properties
    const locUpdates = {};
    for (const key of ['building', 'floor', 'area', 'custom']) {
      const el = byId('location-' + key);
      if (el && el.value !== '' && el.value !== '<Varies>') {
        locUpdates[key] = el.value;
      }
    }

    // 4. Construct patches for CAD entities
    const patches = selection.entities.map((ent) => {
      const existingData = ent.data || {};
      const existingProps = existingData.props || {};
      const nextProps = { ...existingProps, ...locUpdates };

      if (item && item.code) {
        const nextTypes = { ...(nextProps.types || {}) };
        const entQty = Number(ent[typeKey === 'length' ? 'l' : (typeKey === 'area' || typeKey === 'wall' ? 'a' : (typeKey === 'solid' ? 'v' : 'l'))] || ent.l || ent.a || 0) || totalQty;

        const record = {
          code: item.code,
          qty: entQty,
          name: item.name || '',
          unit: item.unit || '',
          wbs: item.wbsCode || undefined
        };

        if (!nextTypes[typeKey]) nextTypes[typeKey] = [];
        const existingIdx = nextTypes[typeKey].findIndex((r) => r.code === item.code);
        if (existingIdx >= 0) nextTypes[typeKey][existingIdx] = record;
        else nextTypes[typeKey].push(record);

        nextProps.types = nextTypes;
        nextProps.code = item.code;
        nextProps.category = typeKey;
        if (item.wbsCode) nextProps.wbs = item.wbsCode;
      }

      return {
        handle: ent.hand || ent.handle,
        data: {
          ...existingData,
          props: nextProps,
          ...(item?.code ? { code: item.code } : {})
        },
        properties: Object.keys(properties).length > 0 ? { ...properties } : undefined
      };
    });

    try {
      await invoke('cad_apply_qto_xdata', { patches, port: 8123 });
      const itemCodeMsg = item?.code ? ` (${item.code})` : '';
      notify(`Applied XData${itemCodeMsg} to ${patches.length} CAD object(s) · Qty: ${Number(totalQty).toFixed(2)} ${item?.unit || 'm'}`);
      const genStatus = byId('general-status');
      if (genStatus) genStatus.textContent = `Applied to ${patches.length} object(s)`;
      const refreshed = await invoke('cad_poll_selection', { lastVersion: 0, port: 8123 });
      if (refreshed && refreshed.entities) {
        selection = { ...refreshed, entities: refreshed.entities };
        updateCadSelectionUI(refreshed, selection.entities);
      }
      if (project) scheduleSave();
    } catch (e) {
      notify(`Apply error: ${e}`);
    }
  }

  // F5 shortcut and Enter key on inputs to apply properties to CAD (capture phase)
  window.addEventListener('keydown', async (e) => {
    if (e.key === 'F5' && !e.ctrlKey && !e.altKey && !e.metaKey) {
      e.preventDefault();
      e.stopPropagation();
      await applyAllToCad();
    } else if (e.key === 'Enter') {
      const codeInput = e.target.closest('#input-item-code');
      const wbsInput = e.target.closest('#input-item-wbs-code');
      const nameInput = e.target.closest('#input-item-name');
      const locInput = e.target.closest('#location-panel input, #location-panel textarea');
      const genInput = e.target.closest('#general-panel input, #general-panel select');
      const rowInput = e.target.closest('.measurement-row-panel input, .row-val-input');

      if (codeInput || wbsInput || nameInput || locInput || genInput || rowInput) {
        e.preventDefault();
        e.stopPropagation();
        e.target.blur();

        if (genInput) {
          const fieldEl = genInput.closest('.general-field');
          const fieldName = fieldEl?.dataset?.generalField || genInput.id.replace('general-', '');
          if (fieldName) {
            await applyGeneralFieldToCad(fieldName, genInput.value);
          }
        } else if (locInput) {
          const fieldEl = locInput.closest('.location-field');
          const key = locInput.id ? locInput.id.replace('location-', '') : fieldEl?.dataset?.locationField;
          if (key) {
            await applyLocationFieldToCad(key, locInput.value);
          }
        } else if (codeInput || wbsInput || nameInput || rowInput) {
          const activeInfo = ui.getActiveItem ? ui.getActiveItem() : null;
          if (activeInfo?.item) {
            if (codeInput) activeInfo.item.code = codeInput.value.trim() || activeInfo.item.code;
            if (wbsInput) activeInfo.item.wbsCode = wbsInput.value.trim();
            if (nameInput) activeInfo.item.name = nameInput.value.trim() || activeInfo.item.name;
          }
          if (activeInfo?.cat) {
            activeInfo.cat.expanded = true;
            if (ui.state) {
              if (!ui.state.visibleCategoryIds.includes(activeInfo.cat.id)) {
                ui.state.visibleCategoryIds.push(activeInfo.cat.id);
              }
              ui.state.activeCategoryId = activeInfo.cat.id;
              if (activeInfo.item) {
                ui.state.activeItemId = activeInfo.item.id;
              }
              ui.state.detailExpanded = true;
            }
          }
          await applyItemXdataToCad();
          if (ui.render) ui.render();
        }
      }
    }
  }, true);

  window.qtoSaveProjectUI = () => save().then(() => notify('Project saved.'));
  for (const name of ['click', 'input', 'change', 'qto-wbs-change', 'qto-panel-visibility']) document.addEventListener(name, scheduleSave);
  document.addEventListener('toggle', scheduleSave, true);
  window.addEventListener('scroll', scheduleSave, { passive: true, capture: true });
  document.addEventListener('visibilitychange', () => { if (document.hidden) save().catch(notify); });

  const syncCategoryBarAria = () => {
    const bar = byId('category-filter-bar');
    if (!bar) return;
    bar.querySelectorAll('button').forEach((b) => {
      let isOn = false;
      if (b.id === 'btn-category-general') {
        isOn = document.getElementById('general-panel')?.hidden === false;
      } else if (b.id === 'btn-category-location') {
        isOn = document.getElementById('location-panel')?.hidden === false;
      } else if (b.id === 'btn-category-drawings') {
        isOn = document.getElementById('drawing-manager-panel')?.hidden === false;
      } else if (b.id === 'btn-category-preview') {
        isOn = document.getElementById('cad-simulator-panel')?.hidden === false;
      } else if (b.title?.includes('(On)')) {
        isOn = true;
      } else if (b.title?.includes('(Off)')) {
        isOn = false;
      } else {
        isOn = b.classList.contains('glass-pill-active') || b.classList.contains('bg-indigo-500/20');
      }
      const targetVal = isOn ? 'true' : 'false';
      if (b.getAttribute('aria-pressed') !== targetVal) {
        b.setAttribute('aria-pressed', targetVal);
      }
    });
  };
  const catBar = byId('category-filter-bar');
  if (catBar) {
    new MutationObserver(syncCategoryBarAria).observe(catBar, { childList: true, subtree: true, attributes: true, attributeFilter: ['class', 'title'] });
    syncCategoryBarAria();
  }

  const syncRowModesAria = () => {
    const rowButtons = document.querySelectorAll('.measurement-row-panel div.rounded-full > button');
    rowButtons.forEach((b) => {
      const isActive = b.className.includes('bg-gradient');
      const targetVal = isActive ? 'true' : 'false';
      if (b.getAttribute('aria-pressed') !== targetVal) {
        b.setAttribute('aria-pressed', targetVal);
      }
    });
  };
  const mRows = byId('measurement-rows') || document.body;
  if (mRows) {
    new MutationObserver(syncRowModesAria).observe(mRows, { childList: true, subtree: true, attributes: true, attributeFilter: ['class'] });
    syncRowModesAria();
  }

  setInterval(pollSelection, 400);
  pollSelection();
  refreshProjects().catch((error) => { byId('projects-status').textContent = String(error); });
}
