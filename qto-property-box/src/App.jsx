import React, { useState, useMemo, useEffect, useCallback, useRef } from 'react';
import { invoke } from '@tauri-apps/api/core';
import {
  Ban,
  BrickWall,
  Columns3,
  Dice5,
  Hash,
  Layers,
  PaintRoller,
  Pencil,
  Rows3,
  Ruler,
  RotateCw,
  SquareDashed,
  SquareX,
  Trash2
} from 'lucide-react';
import WbsReportView from './WbsReportView';
import DescriptionModal from './DescriptionModal';
import { buildComputedTree } from './computedTree';
import { formatUsQuantity } from './numberFormat';
import UsNumberInput from './UsNumberInput';
import DrawingManager from './DrawingManager';
import ThemeControl from './ThemeControl';
import CategoryPanels from './CategoryPanels';
import { Folder, Menu, ArrowLeft, FolderOpen, FileChartColumn, SlidersHorizontal, MapPin, Braces, Pin, MousePointer2 } from 'lucide-react';
import { saveProjectSetting } from './projectSettings';

// ============================================================================
// 1. CONSTANTS & METADATA
// ============================================================================
const TYPE_PREFIXES = {
  Length: 'L',
  Area: 'A',
  Wall: 'W',
  'Wall Finishes': 'WF',
  'Concrete Wall': 'CW',
  'Steel Wall': 'SW',
  Beam: 'B',
  Column: 'C',
  Slab: 'S',
  Number: 'Nr'
};

const GENERAL_FIELDS = [
  { key: 'clr', label: 'Color', hint: 'ACI 0–256, #RRGGBB, or rgb(r,g,b). Remove resets to ByLayer.', reset: 256 },
  { key: 'lyr', label: 'Layer', hint: 'Layer name. Remove resets to layer 0.', reset: '0' },
  { key: 'trp', label: 'Transparency', hint: '0–100%; -2 = ByLayer, -1 = ByBlock. Remove resets to ByLayer.', reset: -2 },
  { key: 'visible', label: 'Visible', hint: 'Show or hide the entity. Remove resets to visible.', reset: true }
];

const SEQUENCE_LIMITS = {
  L: 4,
  A: 4,
  W: 4,
  WF: 4,
  CW: 1,
  SW: 1,
  B: 1,
  C: 1,
  S: 1,
  Nr: 1
};

const TYPE_ICONS = {
  Length: Ruler,
  Area: SquareDashed,
  Wall: BrickWall,
  'Wall Finishes': PaintRoller,
  'Concrete Wall': Dice5,
  'Steel Wall': SquareX,
  Beam: Rows3,
  Column: Columns3,
  Slab: Layers,
  Number: Hash
};

const TYPE_COLORS = {
  Length: '#19cde2',
  Area: '#22d6a7',
  Wall: '#c879fa',
  'Wall Finishes': '#f59e0b',
  'Concrete Wall': '#ec4899',
  'Steel Wall': '#8b5cf6',
  Beam: '#3b82f6',
  Column: '#10b981',
  Slab: '#06b6d4',
  Number: '#eab308'
};

const TYPE_UNITS = {
  Length: 'm',
  Area: 'm²',
  Wall: 'm²',
  'Wall Finishes': 'm²',
  'Concrete Wall': 'm²',
  'Steel Wall': 'm²',
  Beam: 'm',
  Column: 'm',
  Slab: 'm²',
  Number: 'Nr'
};

// Global math / function evaluations for Factor / Expression
if (typeof window !== 'undefined') {
  window.customFn = function (a, b, c) {
    return Number(a) * (Number(b) - Number(c));
  };
  window.doFun = window.customFn;
  window.sumproduct = function (...values) {
    return values.reduce((product, value) => product * Number(value), 1);
  };
}

function parseFactorObject(source) {
  const normalized = source.replace(/([{,]\s*)([A-Za-z_$][\w$]*)\s*:/g, '$1"$2":');
  const value = JSON.parse(normalized);
  if (
    !value ||
    Array.isArray(value) ||
    typeof value !== 'object' ||
    Object.values(value).some((item) => !Number.isFinite(Number(item)))
  ) {
    throw new Error('Use an object with numeric values, e.g. {a:1, b:2, c:3}');
  }
  return value;
}

function evaluateFactorExpression(source) {
  const text = String(source || '1').trim();
  if (/^[-+]?(?:\d+\.?\d*|\.\d+)$/.test(text)) return Number(text);
  const call = text.match(/^([A-Za-z_$][\w$]*)\s*\(([\s\S]*)\)$/);
  if (call) {
    const parameters = parseFactorObject(call[2]);
    const fn = window[call[1]];
    if (typeof fn !== 'function') throw new Error('Unknown global function: ' + call[1]);
    const result = fn(...Object.values(parameters));
    if (!Number.isFinite(Number(result))) throw new Error('The function result must be a number');
    return Number(result);
  }
  const parameters = parseFactorObject(text);
  return Object.values(parameters).reduce((product, value) => product * Number(value), 1);
}

function prefixFor(type) {
  return TYPE_PREFIXES[type] || 'I';
}

function getNextSequence(prefix, currentItems, excludeId) {
  const limit = SEQUENCE_LIMITS[prefix] || 1;
  for (let n = 1; n <= limit; n++) {
    const candidate = prefix + n;
    if (!currentItems.some((item) => item.id !== excludeId && item.key === candidate)) {
      return candidate;
    }
  }
  return null;
}

const DEFAULT_ITEMS = [
  {
    id: 'item-1',
    key: 'L1',
    type: 'Length',
    code: 'FloorFin/CT1',
    action: 'update',
    collapsed: false,
    factor: '1',
    param: {}
  },
  {
    id: 'item-2',
    key: 'A1',
    type: 'Area',
    code: 'FloorFin/CT1',
    action: 'update',
    collapsed: false,
    factor: '{a:1,b:2,c:3}',
    param: {}
  },
  {
    id: 'item-3',
    key: 'W1',
    type: 'Wall',
    code: 'Wall/W1',
    action: 'update',
    collapsed: false,
    factor: 'doFun({a:1,b:2,c:3})',
    param: {
      material: 'other',
      wt: 150,
      wh: 3.0,
      dwh: 50
    }
  }
];

const DEFAULT_SORT_ROWS = [
  { column: 'Building', sortOn: 'Code', order: 'A to Z' },
  { column: 'Floor', sortOn: 'Code', order: 'A to Z' },
  { column: 'Area', sortOn: 'Code', order: 'A to Z' },
  { column: 'Type', sortOn: 'Code', order: 'A to Z' },
  { column: 'Code', sortOn: 'Code', order: 'A to Z' }
];

function XdataActions({ value, onChange, label }) {
  return (
    <div className="item-action-mode location-actions" role="group" aria-label={label}>
      {[
        ['nothing', 'Nothing', Ban],
        ['update', 'Update', RotateCw],
        ['remove', 'Remove', Trash2]
      ].map(([action, title, Icon]) => (
        <button key={action} type="button"
          className={value === action ? `active ${action}` : ''}
          aria-pressed={value === action} aria-label={title} title={title}
          onClick={() => onChange(action)}>
          <Icon aria-hidden="true" />
        </button>
      ))}
    </div>
  );
}

function ComputedTreeNodes({ nodes }) {
  return nodes.map((node) => node.leaf ? (
    <div key={node.id} className="code-leaf tree-node">
      <span className="code-detail">
        {node.description && <span className="code-description">{node.description}</span>}
        <span className="code-label">{node.value || '?'}</span>
      </span>
      <span className="value">{Number.isFinite(node.qty) ? `${formatUsQuantity(node.qty)} ${node.unit}` : 'Error'}</span>
    </div>
  ) : (
    <details key={node.id} className="tree-node" open>
      <summary>
        <svg className="tree-chevron" viewBox="0 0 16 16" aria-hidden="true"><path d="m5 6 3 3 3-3" /></svg>
        {node.column === 'Type' && <span className="type-icon">{React.createElement(TYPE_ICONS[node.value] || SquareDashed)}</span>}
        <b className="node-title">{!['Building', 'Floor', 'Type'].includes(node.column) && `${node.column}: `}{node.description || node.value || '?'}</b>
        <span className="node-meta">{node.entityCount} entities selected</span>
      </summary>
      <div className="branch"><ComputedTreeNodes nodes={node.children} /></div>
    </details>
  ));
}

export default function App({ project, onCloseProject }) {
  // Navigation: 'qto' | 'report'
  const [activeNavMenu, setActiveNavMenu] = useState('qto');
  const [settingsOpen, setSettingsOpen] = useState(false);
  const [drawingManagerOpen, setDrawingManagerOpen] = useState(false);
  const [workspaceLoaded, setWorkspaceLoaded] = useState(false);

  // CAD Connection & IPC State
  const [cadConnected, setCadConnected] = useState(false);
  const [cadPlatform, setCadPlatform] = useState('GstarCAD');
  const [cadDwgName, setCadDwgName] = useState('');
  const [ipcPort] = useState(8123);
  const selectionVersionRef = useRef(0);
  const selectionDwgRef = useRef(null);
  const selectionHashRef = useRef(null);
  const [selectionHash, setSelectionHash] = useState(null);
  const selectionBusyRef = useRef(false);
  const selectionRefreshRef = useRef(null);
  const [isPinned, setIsPinned] = useState(true);
  const [scale, setScale] = useState('m → m (1:1)');

  // Selected Entities from CAD
  const [entities, setEntities] = useState([]);

  // Toast message
  const [toastMsg, setToastMsg] = useState('');
  const toastTimeoutRef = useRef(null);
  const showToast = useCallback((msg) => {
    setToastMsg(msg);
    if (toastTimeoutRef.current) clearTimeout(toastTimeoutRef.current);
    toastTimeoutRef.current = setTimeout(() => setToastMsg(''), 2800);
  }, []);

  // Section Collapsed States
  const [collapsedSections, setCollapsedSections] = useState({
    general: false,
    building: false,
    array: false,
    computed: false
  });

  const toggleSection = (name) => {
    setCollapsedSections((prev) => ({ ...prev, [name]: !prev[name] }));
  };

  // Section 1: Building (props.building / props.location)
  const [general, setGeneral] = useState({ clr: '', lyr: '', trp: '', visible: '' });
  const [generalActions, setGeneralActions] = useState({ clr: 'nothing', lyr: 'nothing', trp: 'nothing', visible: 'nothing' });
  const [generalStates, setGeneralStates] = useState([]);
  const [selectedGeneralState, setSelectedGeneralState] = useState('');
  const [generalStateName, setGeneralStateName] = useState('');
  const [generalStatesBusy, setGeneralStatesBusy] = useState(true);
  const [generalStatesError, setGeneralStatesError] = useState('');

  useEffect(() => {
    let cancelled = false;
    invoke('list_general_states', { projectPath: project.path }).then((states) => {
      if (!cancelled) setGeneralStates(states);
    }).catch((error) => {
      if (!cancelled) setGeneralStatesError(String(error));
    }).finally(() => { if (!cancelled) setGeneralStatesBusy(false); });
    return () => { cancelled = true; };
  }, []);

  const handleLoadGeneralState = (name) => {
    setSelectedGeneralState(name);
    setGeneralStateName(name);
    const state = generalStates.find((entry) => entry.name === name);
    if (!state) return;
    setGeneral({ ...state.values });
    setGeneralActions({ ...state.actions });
    showToast(`General state loaded: ${name}. Apply XData (F5) to write to CAD.`);
  };

  const handleSaveGeneralState = async () => {
    const name = generalStateName.trim();
    if (!name) { showToast('Enter a name for this General state'); return; }
    setGeneralStatesBusy(true);
    try {
      const saved = await invoke('save_general_state', { projectPath: project.path, state: { name, values: general, actions: generalActions } });
      setGeneralStates(await invoke('list_general_states', { projectPath: project.path }));
      setSelectedGeneralState(saved.name);
      setGeneralStateName(saved.name);
      setGeneralStatesError('');
      showToast(`General state saved: ${saved.name}`);
    } catch (error) {
      setGeneralStatesError(String(error));
      showToast(`Could not save General state: ${String(error)}`);
    } finally { setGeneralStatesBusy(false); }
  };

  const handleDeleteGeneralState = async () => {
    if (!selectedGeneralState) return;
    setGeneralStatesBusy(true);
    try {
      await invoke('delete_general_state', { projectPath: project.path, name: selectedGeneralState });
      setGeneralStates(await invoke('list_general_states', { projectPath: project.path }));
      setSelectedGeneralState('');
      setGeneralStateName('');
      setGeneralStatesError('');
      showToast('General state deleted');
    } catch (error) {
      setGeneralStatesError(String(error));
    } finally { setGeneralStatesBusy(false); }
  };
  const [building, setBuilding] = useState({
    building: '',
    floor: '',
    area: ''
  });

  // Location XData actions
  const [buildingActions, setBuildingActions] = useState({
    building: 'update',
    floor: 'update',
    area: 'update'
  });

  // Section 2: Array Items (props.array_items / props.types)
  const [items, setItems] = useState([]);
  const [emptyArrayAction, setEmptyArrayAction] = useState('nothing');
  const [itemToDelete, setItemToDelete] = useState(null);

  // Section 3: Sort Levels & Output Tree
  const [sortRows, setSortRows] = useState(DEFAULT_SORT_ROWS);
  const [sortModalOpen, setSortModalOpen] = useState(false);
  const [draftSortRows, setDraftSortRows] = useState(DEFAULT_SORT_ROWS);
  const [selectedSortIndex, setSelectedSortIndex] = useState(0);

  // SQLite Descriptions Map
  const [descriptions, setDescriptions] = useState({});

  // Modals
  const [descModalOpen, setDescModalOpen] = useState(false);
  const [descModalCode, setDescModalCode] = useState('');
  const [descModalScope, setDescModalScope] = useState(null); // 'building' | 'floor' | 'area' | null
  const [jsonModalOpen, setJsonModalOpen] = useState(false);

  // Project settings and descriptions are stored beside the project's drawings list.
  useEffect(() => {
    let cancelled = false;
    Promise.all([
      invoke('get_project_setting', { projectPath: project.path, key: 'workspace' }),
      invoke('list_project_descriptions', { projectPath: project.path })
    ]).then(([settings, catalog]) => {
      if (cancelled) return;
      setDescriptions(catalog);
      if (settings) {
        if (Array.isArray(settings.sortRows) && settings.sortRows.length) setSortRows(settings.sortRows);
        if (typeof settings.scale === 'string') setScale(settings.scale);
        if (typeof settings.isPinned === 'boolean') {
          setIsPinned(settings.isPinned);
          invoke('set_always_on_top', { alwaysOnTop: settings.isPinned }).catch(() => {});
        }
        if (settings.collapsedSections) setCollapsedSections((prev) => ({ ...prev, ...settings.collapsedSections }));
        if (settings.buildingActions) setBuildingActions(settings.buildingActions);
        if (settings.generalActions) setGeneralActions(settings.generalActions);
      }
    }).catch((error) => showToast(`Could not load project settings: ${String(error)}`))
      .finally(() => { if (!cancelled) setWorkspaceLoaded(true); });
    return () => { cancelled = true; };
  }, [project.path, showToast]);

  const workspaceSettings = useMemo(() => ({ sortRows, scale, isPinned, collapsedSections, buildingActions, generalActions }),
    [sortRows, scale, isPinned, collapsedSections, buildingActions, generalActions]);

  useEffect(() => {
    if (!workspaceLoaded) return;
    const timer = setTimeout(() => {
      saveProjectSetting(project.path, 'workspace', workspaceSettings)
        .catch((error) => showToast(`Could not save project settings: ${String(error)}`));
    }, 250);
    return () => clearTimeout(timer);
  }, [project.path, workspaceLoaded, workspaceSettings, showToast]);

  const handleCloseProject = async () => {
    try {
      await saveProjectSetting(project.path, 'workspace', workspaceSettings);
      onCloseProject();
    } catch (error) { showToast(`Could not save project: ${String(error)}`); }
  };

  // Manual refresh and versioned updates from the CAD selection reactor.
  const handlePickSelection = async (options = {}) => {
    if (selectionBusyRef.current) return;
    selectionBusyRef.current = true;
    const { silent = false, lastVersion = 0, isCancelled = () => false } = options;
    if (!silent) showToast('Getting selection from CAD...');
    try {
      const res = await invoke('cad_poll_selection', { lastVersion, port: ipcPort });
      if (isCancelled()) return;
      if (res && !res.error) {
        if (res.changed === false && res.dwg === selectionDwgRef.current) {
          setCadConnected(true);
          return;
        }
        // A drawing switch needs a full snapshot even if its selection version matches.
        if (res.changed === false) {
          selectionVersionRef.current = 0;
          return;
        }
        const sameDrawing = res.dwg === selectionDwgRef.current;
        selectionVersionRef.current = res.version ?? 0;
        selectionDwgRef.current = res.dwg;
        setCadConnected(true);
        // Hash the canonical selected-ID array. Check the drawing separately
        // because entity IDs can be reused in different drawings.
        const selectedIds = Array.isArray(res.handles) ? res.handles :
          (res.entities || []).map((ent) => ent.hand ?? ent.handle ??
            (typeof ent.id === 'number' ? ent.id.toString(16) : ent.id));
        const selectionBytes = new TextEncoder().encode(JSON.stringify(
          [...new Set(selectedIds.map((id) => String(id).trim().toUpperCase()))].sort()
        ));
        const selectionDigest = await crypto.subtle.digest('SHA-256', selectionBytes);
        if (isCancelled()) return;
        const selectionHash = Array.from(new Uint8Array(selectionDigest),
          (byte) => byte.toString(16).padStart(2, '0')).join('');
        if (sameDrawing && selectionHashRef.current === selectionHash) return;
        selectionHashRef.current = selectionHash;
        setSelectionHash(selectionHash);
        setSelectedGeneralState('');
        setGeneralStateName('');
        if (res.platform) setCadPlatform(res.platform);
        if (res.dwg) setCadDwgName(res.dwg);
        if (Array.isArray(res.entities) && res.entities.length > 0) {
          const selectedEntities = res.entities.map((ent) => ({
            ...ent,
            handle: ent.hand ?? ent.handle,
            rawCadData: ent.data ?? ent.rawCadData ?? {}
          }));
          setEntities(selectedEntities);
          const selectedGeneral = {};
          for (const { key } of GENERAL_FIELDS) {
            const values = selectedEntities.map((ent) => {
              const value = ent[key];
              if (key === 'clr' && value && typeof value === 'object') return `rgb(${value.r},${value.g},${value.b})`;
              return value == null ? '' : String(value);
            });
            selectedGeneral[key] = values.every((value) => value === values[0]) ? values[0] : '<Varies>';
          }
          setGeneral(selectedGeneral);
          const selectedLocation = {};
          for (const field of ['building', 'floor', 'area']) {
            const values = selectedEntities.map((ent) => String(ent.rawCadData.props?.[field] ?? ''));
            selectedLocation[field] = values.every((value) => value === values[0]) ? values[0] : '<Varies>';
          }
          setBuilding(selectedLocation);
          const selectedItems = new Map();
          for (const ent of selectedEntities) {
            for (const [typeKey, records] of Object.entries(ent.rawCadData.props?.types || {})) {
              const type = Object.keys(TYPE_PREFIXES).find((name) => name.toLowerCase().replace(/\s+/g, '_') === typeKey);
              if (!type || !Array.isArray(records)) continue;
              for (const record of records) {
                const identity = JSON.stringify([typeKey, record.code]);
                if (selectedItems.has(identity)) continue;
                const index = [...selectedItems.values()].filter((item) => item.type === type).length + 1;
                selectedItems.set(identity, {
                  id: identity, key: `${TYPE_PREFIXES[type]}${index}`, type,
                  code: record.code, factor: '1', param: record.param || {},
                  action: 'nothing', collapsed: false
                });
              }
            }
          }
          setItems([...selectedItems.values()]);
          setEmptyArrayAction('nothing');
          if (!silent) showToast(`Connected to ${res.platform}: ${res.entities.length} selected object(s)`);
        } else if (Array.isArray(res.handles) && res.handles.length > 0) {
          const newEnts = res.handles.map((h, i) => ({
            id: 20000 + i,
            handle: h,
            lyr: '0',
            l: 28.0,
            a: 18.0,
            rawCadData: {}
          }));
          setEntities(newEnts);
          setGeneral({ clr: '', lyr: '', trp: '', visible: '' });
          setBuilding({ building: '', floor: '', area: '' });
          setItems([]);
          setEmptyArrayAction('nothing');
          if (!silent) showToast(`Connected to ${res.platform}: ${res.handles.length} selected object(s)`);
        } else {
          setEntities([]);
          setGeneral({ clr: '', lyr: '', trp: '', visible: '' });
          setBuilding({ building: '', floor: '', area: '' });
          setItems([]);
          setEmptyArrayAction('nothing');
          if (!silent) showToast('No CAD objects are selected (0 objects)');
        }
      } else {
        if (!silent) showToast('No response from CAD (Demo Mode)');
      }
    } catch (_e) {
      if (isCancelled()) return;
      selectionVersionRef.current = 0;
      setCadConnected(false);
      if (!silent) showToast('CAD IPC is not connected (127.0.0.1:8123) — Demo Mode');
    } finally {
      selectionBusyRef.current = false;
    }
  };
  selectionRefreshRef.current = handlePickSelection;

  useEffect(() => {
    let cancelled = false;
    let timer;
    const poll = async () => {
      await selectionRefreshRef.current({
        silent: true,
        lastVersion: selectionVersionRef.current,
        isCancelled: () => cancelled
      });
      if (!cancelled) timer = setTimeout(poll, 500);
    };
    poll();
    return () => { cancelled = true; clearTimeout(timer); };
  }, [ipcPort]);

  // Add Item to Array
  const handleAddItem = (requestedType = 'Length') => {
    const type = typeof requestedType === 'string' ? requestedType : 'Length';
    const nextSeq = getNextSequence(prefixFor(type), items);
    if (!nextSeq) {
      showToast('All Length sequence slots (L1–L4) are in use');
      return;
    }
    const newItem = {
      id: `item-${Date.now()}`,
      key: nextSeq,
      type,
      code: nextSeq,
      action: 'update',
      collapsed: false,
      factor: '1',
      param: type === 'Wall' ? { material: 'other', wt: 150, wh: 3, dwh: 0 }
        : type === 'Beam' ? { material: 'concrete', member: 'BB', bh: 450, bb: 250 } : {}
    };
    setItems((prev) => [...prev, newItem]);
    showToast(`New item (${nextSeq}) added`);
  };

  // Delete Item
  const handleDeleteItem = (id) => {
    const it = items.find((x) => x.id === id);
    if (!it) return;
    setItemToDelete(it);
  };

  const handleConfirmDeleteItem = () => {
    if (!itemToDelete) return;
    const { id, key } = itemToDelete;
    setItems((prev) => prev.filter((x) => x.id !== id));
    setItemToDelete(null);
    showToast(`Item ${key} deleted`);
  };

  useEffect(() => {
    if (!itemToDelete) return undefined;
    const handleKeyDown = (event) => {
      if (event.key === 'Escape') {
        event.preventDefault();
        setItemToDelete(null);
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [itemToDelete]);

  const handleSetItemAction = (id, action) => {
    setItems((prev) => prev.map((it) => (it.id === id ? { ...it, action } : it)));
  };

  // Toggle Item Collapse
  const handleToggleItemCollapse = (id) => {
    setItems((prev) =>
      prev.map((it) => (it.id === id ? { ...it, collapsed: !it.collapsed } : it))
    );
  };

  // Change Item Type in place
  const handleChangeItemType = (id, newType) => {
    setItems((prev) =>
      prev.map((it) => {
        if (it.id !== id) return it;
        const prefix = prefixFor(newType);
        const nextSeq = getNextSequence(prefix, prev, id);
        if (!nextSeq) {
          showToast(`All sequence slots for ${newType} are in use`);
          return it;
        }
        let defaultParam = {};
        if (newType === 'Wall' || newType === 'Wall Finishes') {
          defaultParam = { material: 'other', wt: 150, wh: 3.0, dwh: 0 };
        } else if (newType === 'Concrete Wall') {
          defaultParam = { wt: 150, wch: 3.0, dwch: 0 };
        } else if (newType === 'Steel Wall') {
          defaultParam = { wt: 10, wsh: 3.0, dwsh: 0 };
        } else if (newType === 'Beam') {
          defaultParam = { material: 'concrete', member: 'BB', bh: 450, bb: 250 };
        } else if (newType === 'Column') {
          defaultParam = { material: 'concrete', bh: 400, bb: 400, steelWeight: false };
        } else if (newType === 'Slab') {
          defaultParam = { material: 'concrete', steelWeight: false };
        }
        return {
          ...it,
          type: newType,
          key: nextSeq,
          param: defaultParam
        };
      })
    );
    showToast(`${newType} updated on the item card`);
  };

  // Change Item Sequence
  const handleChangeItemSeq = (id, newSeq) => {
    if (items.some((it) => it.id !== id && it.key === newSeq)) {
      showToast(`Sequence ${newSeq} is already in use`);
      return;
    }
    setItems((prev) => prev.map((it) => (it.id === id ? { ...it, key: newSeq } : it)));
  };

  // Update Item Fields
  const handleUpdateItemField = (id, field, value) => {
    setItems((prev) => prev.map((it) => (it.id === id ? { ...it, [field]: value } : it)));
  };

  // Update Item Parameter Field
  const handleUpdateItemParam = (id, paramKey, value) => {
    setItems((prev) =>
      prev.map((it) =>
        it.id === id ? { ...it, param: { ...it.param, [paramKey]: value } } : it
      )
    );
  };

  // Calculate Computed Result for an item
  const getItemComputedResult = useCallback(
    (item, entity) => {
      let baseVal = 1;
      const count = entities.length || 1;
      const firstEnt = entity || entities[0] || {};
      const entL = typeof firstEnt.l === 'number' ? firstEnt.l : 28.0;
      const entA = typeof firstEnt.a === 'number' ? firstEnt.a : 18.0;

      switch (item.type) {
        case 'Length':
          baseVal = entL;
          break;
        case 'Area':
          baseVal = entA;
          break;
        case 'Wall': {
          const h = Number(item.param?.wh ?? 3.0);
          const ded = Number(item.param?.dwh ?? 0) / 1000;
          baseVal = entL * Math.max(0, h - ded);
          break;
        }
        case 'Wall Finishes': {
          const h = Number(item.param?.wfh ?? 3.0);
          const ded = Number(item.param?.dwfh ?? 0) / 1000;
          baseVal = entL * Math.max(0, h - ded);
          break;
        }
        case 'Concrete Wall': {
          const h = Number(item.param?.wch ?? 3.0);
          const ded = Number(item.param?.dwch ?? 0) / 1000;
          baseVal = entL * Math.max(0, h - ded);
          break;
        }
        case 'Steel Wall': {
          const h = Number(item.param?.wsh ?? 3.0);
          const ded = Number(item.param?.dwsh ?? 0) / 1000;
          baseVal = entL * Math.max(0, h - ded);
          break;
        }
        case 'Beam':
          baseVal = entL;
          break;
        case 'Column':
          baseVal = entL > 0 ? entL : 3.0;
          break;
        case 'Slab':
          baseVal = entA;
          break;
        case 'Number':
          baseVal = entity ? 1 : count;
          break;
        default:
          baseVal = 1;
      }

      try {
        const factorNum = evaluateFactorExpression(item.factor);
        return baseVal * factorNum;
      } catch (_e) {
        return NaN;
      }
    },
    [entities]
  );

  // Apply XData to CAD Entities
  const handleApplyXdata = async () => {
    const invalidNumber = typeof document === 'undefined' ? null :
      document.querySelector('.qto-app .section:not(.collapsed) input[data-number-format="en-US"][aria-invalid="true"]');
    if (invalidNumber) {
      invalidNumber.focus();
      showToast('Use US number format, for example 1,234.56');
      return;
    }
    // 1. If any tree is collapsed, skip update according to User Requirement 1
    const skipBuilding = collapsedSections.building;
    const skipArray = collapsedSections.array;
    const skipGeneral = collapsedSections.general;
    const properties = {};
    if (!skipGeneral) {
      for (const { key, reset } of GENERAL_FIELDS) {
        const action = generalActions[key];
        const value = general[key];
        if (action === 'nothing' || (action === 'update' && (value === '<Varies>' || value === ''))) continue;
        let nextValue = action === 'remove' ? reset : value;
        if (key === 'trp' || (key === 'clr' && /^\d+$/.test(String(nextValue)))) nextValue = Number(nextValue);
        if (key === 'visible') nextValue = nextValue === true || nextValue === 'true';
        if (key === 'trp' && (!Number.isInteger(nextValue) || nextValue < -2 || nextValue > 100)) {
          showToast('Transparency must be -2, -1, or an integer from 0 to 100');
          return;
        }
        if (key === 'clr' && typeof nextValue === 'number' && (nextValue < 0 || nextValue > 256)) {
          showToast('Color index must be from 0 to 256');
          return;
        }
        properties[key] = nextValue;
      }
    }

    if (skipBuilding && skipArray && Object.keys(properties).length === 0) {
      showToast('No updates enabled in the expanded sections');
      return;
    }

    if (!entities || entities.length === 0) {
      showToast('No CAD objects are selected to apply XData to');
      return;
    }

    const updatedFieldsList = Object.keys(properties);
    if (!skipBuilding) {
      if (buildingActions.building !== 'nothing') updatedFieldsList.push('building');
      if (buildingActions.floor !== 'nothing') updatedFieldsList.push('floor');
      if (buildingActions.area !== 'nothing') updatedFieldsList.push('area');
    }

    const clearArrayItems = !skipArray && items.length === 0 && emptyArrayAction === 'remove';
    const activeItems = skipArray ? [] : items.filter((it) => it.action !== 'nothing');
    const updateItems = activeItems.filter((it) => it.action === 'update');
    const removeItems = activeItems.filter((it) => it.action === 'remove');
    if (!skipArray) {
      updatedFieldsList.push(...(clearArrayItems
        ? ['all array item data removed']
        : [`${updateItems.length} item(s) updated`, `${removeItems.length} removed`]));
    }

    // Prepare patches per entity
    const patches = entities.map((ent) => {
      const existingData = ent.rawCadData || {};
      const existingProps = existingData.props || {};

      const nextProps = { ...existingProps };

      // Update Building / Floor / Area when enabled and the section is expanded.
      if (!skipBuilding) {
        if (buildingActions.building === 'remove') {
          delete nextProps.building;
        } else if (buildingActions.building === 'update' && building.building !== '<Varies>') {
          nextProps.building = building.building;
        }
        if (buildingActions.floor === 'remove') {
          delete nextProps.floor;
        } else if (buildingActions.floor === 'update' && building.floor !== '<Varies>') {
          nextProps.floor = building.floor;
        }
        if (buildingActions.area === 'remove') {
          delete nextProps.area;
        } else if (buildingActions.area === 'update' && building.area !== '<Varies>') {
          nextProps.area = building.area;
        }
      }

      // Apply selected Array Item actions if the section is expanded.
      if (clearArrayItems) {
        delete nextProps.types;
        delete nextProps.array_items;
      } else if (!skipArray) {
        const nextTypes = { ...(nextProps.types || {}) };
        for (const it of activeItems) {
          const typeKey = it.type.toLowerCase().replace(/\s+/g, '_');
          if (it.action === 'remove') {
            const remainingRecords = (nextTypes[typeKey] || []).filter((record) => record.code !== it.code);
            if (remainingRecords.length > 0) {
              nextTypes[typeKey] = remainingRecords;
            } else {
              delete nextTypes[typeKey];
            }
            continue;
          }

          const evalQty = getItemComputedResult(it, ent);
          const record = {
            code: it.code,
            qty: Number.isFinite(evalQty) ? parseFloat(evalQty.toFixed(3)) : 1,
            // User Requirement 2: Key for entity data is 'param'
            param: it.param && Object.keys(it.param).length > 0 ? it.param : undefined
          };
          if (!nextTypes[typeKey]) {
            nextTypes[typeKey] = [];
          }
          // Update or push code record
          const existingIdx = nextTypes[typeKey].findIndex((r) => r.code === it.code);
          if (existingIdx >= 0) {
            nextTypes[typeKey][existingIdx] = record;
          } else {
            nextTypes[typeKey].push(record);
          }
        }
        nextProps.types = nextTypes;
      }

      return {
        handle: ent.handle,
        properties,
        data: {
          ...existingData,
          props: nextProps
        }
      };
    });

    try {
      if (cadConnected) {
        const res = await invoke('cad_apply_qto_xdata', { patches, port: ipcPort });
        if (res?.error || res?.ok !== true || res.count !== patches.length) {
          throw new Error(res?.error || 'CAD did not confirm all XData updates');
        }
        setEntities((prev) => prev.map((ent) => {
          const patch = patches.find((item) => item.handle === ent.handle);
          return patch ? { ...ent, ...patch.properties, data: patch.data, rawCadData: patch.data } : ent;
        }));
        showToast(`XData written to ${res.count} CAD object(s) (${updatedFieldsList.join(', ')})`);
      } else {
        showToast(`[Demo] XData applied to ${entities.length} object(s) (${updatedFieldsList.join(', ')})`);
      }
    } catch (err) {
      alert(`Failed to write XData to CAD:\n${String(err)}`);
      showToast(`Error: ${String(err)}`);
    }
  };

  useEffect(() => {
    const handleApplyShortcut = (event) => {
      if (event.key !== 'F5' || event.ctrlKey || event.altKey || event.metaKey || activeNavMenu !== 'qto') return;
      event.preventDefault();
      if (event.repeat || drawingManagerOpen || descModalOpen || jsonModalOpen || sortModalOpen || itemToDelete) return;
      handleApplyXdata();
    };
    window.addEventListener('keydown', handleApplyShortcut);
    return () => window.removeEventListener('keydown', handleApplyShortcut);
  }, [activeNavMenu, drawingManagerOpen, descModalOpen, jsonModalOpen, sortModalOpen, itemToDelete, handleApplyXdata]);

  // Save Schedule Handler
  const handleSaveSchedule = async () => {
    try {
      await saveProjectSetting(project.path, 'schedule', {
        savedAt: new Date().toISOString(), building, items, general, buildingActions, generalActions, sortRows,
        selectedHandles: entities.map((entity) => entity.handle), data: entityJsonPayload
      });
      showToast('QTO schedule saved in this project');
    } catch (error) { showToast(`Could not save schedule: ${String(error)}`); }
  };

  // Open Description Modal for a work item or scope
  const handleOpenDesc = (codeOrScope, isScope = false) => {
    if (isScope) {
      setDescModalScope(codeOrScope);
      setDescModalCode(building[codeOrScope] || '');
    } else {
      setDescModalScope(null);
      setDescModalCode(codeOrScope);
    }
    setDescModalOpen(true);
  };

  // Save Description Callback
  const handleSaveDescription = (code, descData) => {
    setDescriptions((prev) => ({ ...prev, [code]: descData }));
  };

  const handleDeleteDescription = (code) => {
    setDescriptions((prev) => {
      const next = { ...prev };
      delete next[code];
      return next;
    });
  };

  const computedTree = useMemo(() => {
    const records = [];
    for (const ent of entities) {
      for (const item of items) {
        const storedRecords = ent.rawCadData?.props?.types?.[item.type.toLowerCase().replace(/\s+/g, '_')];
        const storedRecord = Array.isArray(storedRecords) ? storedRecords.find((record) => record.code === item.code) : undefined;
        if (item.action === 'nothing' && !storedRecord) continue;
        const location = {};
        for (const field of ['building', 'floor', 'area']) {
          location[field] = building[field] === '<Varies>' ? ent.rawCadData?.props?.[field] ?? '' : building[field];
        }
        records.push({
          Building: location.building, Floor: location.floor, Area: location.area,
          Type: item.type, Code: item.code, unit: TYPE_UNITS[item.type] || '',
          qty: item.action === 'nothing' && Number.isFinite(storedRecord?.qty) ? storedRecord.qty : getItemComputedResult(item, ent),
          entityId: ent.handle ?? ent.id
        });
      }
    }
    return buildComputedTree(records, sortRows, descriptions);
  }, [entities, items, building, sortRows, descriptions, getItemComputedResult]);

  // JSON Preview Schema
  const entityJsonPayload = useMemo(() => {
    const typesObj = {};
    for (const it of items) {
      const typeKey = it.type.toLowerCase().replace(/\s+/g, '_');
      const evalQty = getItemComputedResult(it);
      if (!typesObj[typeKey]) typesObj[typeKey] = [];
      typesObj[typeKey].push({
        code: it.code,
        qty: Number.isFinite(evalQty) ? parseFloat(evalQty.toFixed(3)) : 1,
        // User Requirement 2: Key for entity data is 'param'
        param: it.param && Object.keys(it.param).length > 0 ? it.param : undefined
      });
    }

    return {
      props: {
        building: building.building,
        floor: building.floor,
        area: building.area,
        types: typesObj
      }
    };
  }, [building, items, getItemComputedResult]);

  if (!workspaceLoaded) return <div className="project-loading">Opening {project.name}...</div>;

  return (
    <div className="qto-app bg-[#0b0e12] text-[#edf3fa] font-sans">
      <main className="qto-window w-full max-w-[1050px] bg-[#101318] border border-[#46515e] rounded-[9px] shadow-[0_18px_55px_#0009] overflow-hidden flex flex-col">
        {/* ====================================================================
            1. TOP NAVIGATION MENU STRIP
        ==================================================================== */}
        <div className="project-workspace-bar">
          <button type="button" onClick={handleCloseProject}><ArrowLeft size={12} /> Projects</button>
          <strong title={project.path}>{project.name}</strong>
        </div>
        <header className="reference-header">
          <div className="reference-brand-row">
            <div className="reference-brand"><span className="reference-brand-icon"><Folder size={17} /></span><div><h1>QTO Property</h1><p>Quantity Take-Off ? <span className="theme-caption-dark">Dark Glass</span><span className="theme-caption-light">Light Glass</span></p></div></div>
            <button type="button" className="reference-menu" aria-label="Menu" aria-expanded={settingsOpen} aria-controls="workspace-settings" onClick={() => setSettingsOpen((prev) => !prev)}><Menu size={19} /></button>
          </div>
          <div id="workspace-settings" className="reference-settings" hidden={!settingsOpen}>
            <div className="reference-settings-row">
              <button type="button" className={`btn ${activeNavMenu === 'qto' ? 'primary' : ''}`} onClick={() => setActiveNavMenu('qto')}><Folder size={14} /> QTO</button>
              <button type="button" className={`btn ${activeNavMenu === 'report' ? 'primary' : ''}`} onClick={() => setActiveNavMenu('report')}><FileChartColumn size={14} /> Report</button>
              <ThemeControl />
            </div>
            <div className="reference-settings-row">
              <button type="button" className="reference-icon-button" title="Drawings" aria-label="Drawings" onClick={() => setDrawingManagerOpen(true)}><FolderOpen size={15} /></button>
              <button type="button" className="reference-icon-button" title="General" aria-label="General panel" onClick={() => document.getElementById('generalSection')?.scrollIntoView({ block: 'start', behavior: 'smooth' })}><SlidersHorizontal size={15} /></button>
              <button type="button" className="reference-icon-button" title="Location" aria-label="Location panel" onClick={() => document.getElementById('buildingSection')?.scrollIntoView({ block: 'start', behavior: 'smooth' })}><MapPin size={15} /></button>
              <button type="button" className="reference-icon-button" title="JSON" aria-label="View JSON" onClick={() => setJsonModalOpen(true)}><Braces size={15} /></button>
              <span className="reference-connection">{cadPlatform} ? {cadConnected ? 'Connected' : 'Offline'}</span>
            </div>
          </div>
        </header>
        {project.warning && <p className="project-error">{project.warning}</p>}

        {/* If Report Menu is Active, render WBS Report View */}
        {activeNavMenu === 'report' ? (
          <WbsReportView
            project={project}
            cadConnected={cadConnected}
            cadPlatform={cadPlatform}
            cadDwgName={cadDwgName}
            ipcPort={ipcPort}
            selectedEntities={entities}
            selectionHash={selectionHash}
            descriptions={descriptions}
            onSaveDescription={handleSaveDescription}
            onDeleteDescription={handleDeleteDescription}
          />
        ) : (
          <>
            {/* ====================================================================
                2. SECOND TOOLBAR
            ==================================================================== */}
            <div className="toolbar" aria-label="QTO toolbar" hidden={!settingsOpen}>
              <button
                type="button"
                className="tool-button tool-qto"
                onClick={() => showToast('QTO menu is active')}
              >
                <span>☷</span>
                <span>QTO</span>
              </button>

              <span className="doc" title={cadDwgName}>
                {cadDwgName} · {entities.length} selected
              </span>

              <span className="cad-status" title="CAD connection status">
                ⌁ {cadPlatform}
              </span>

              <select
                value={scale}
                onChange={(e) => {
                  setScale(e.target.value);
                  showToast(`Drawing scale set to ${e.target.value}`);
                }}
                className="scale-select"
                aria-label="Drawing scale"
              >
                <option value="m → m (1:1)">m → m (1:1)</option>
                <option value="mm → m (1:1000)">mm → m (1:1000)</option>
                <option value="mm → mm (1:1)">mm → mm (1:1)</option>
                <option value="ft → ft (1:1)">ft → ft (1:1)</option>
              </select>

              <button
                type="button"
                id="pickSelection"
                onClick={handlePickSelection}
                className="tool-button pick"
                title="Get the current CAD selection"
              >
                <span>⊕</span>
                <span>Pick Selection</span>
              </button>

              <button
                type="button"
                id="pinBtn"
                onClick={() => {
                  setIsPinned(!isPinned);
                  showToast(!isPinned ? 'Window pinned (Always on Top)' : 'Window unpinned');
                }}
                className={`tool-button pin ${isPinned ? 'active' : ''}`}
                title={isPinned ? 'Pinned Always on Top (click to unpin)' : 'Pin Always on Top'}
              >
                📌
              </button>

              <button
                type="button"
                id="gridBtn"
                onClick={() => showToast(`Active array items: ${items.length}`)}
                className="tool-button tool-count"
                title="Item count"
              >
                ▦ {items.length}
              </button>

              <button
                type="button"
                id="jsonBtn"
                onClick={() => setJsonModalOpen(true)}
                className="tool-button json"
                title="View the XData JSON input/output schema"
              >
                &lt;/&gt; JSON
              </button>
            </div>

            <div className="qto-scroll-view">
            <section className={`section ${collapsedSections.general ? 'collapsed' : ''}`} id="generalSection">
              <div className="location-head">
                <button type="button" className="section-head location-toggle"
                  onClick={() => toggleSection('general')} aria-expanded={!collapsedSections.general}>
                  <span className="chev"><svg viewBox="0 0 16 16" aria-hidden="true"><path d="m5 6 3 3 3-3" /></svg></span>
                  <span aria-hidden="true">⚙</span><span>General</span>
                </button>
                <XdataActions label="All General property actions"
                  value={Object.values(generalActions).every((action) => action === generalActions.clr) ? generalActions.clr : null}
                  onChange={(action) => setGeneralActions({ clr: action, lyr: action, trp: action, visible: action })} />
              </div>
              <div className="section-body">
                <div className="general-states">
                  <label htmlFor="general-state">State</label>
                  <select id="general-state" className="field" value={selectedGeneralState}
                    disabled={generalStatesBusy} onChange={(event) => handleLoadGeneralState(event.target.value)}>
                    <option value="">Current values / New state</option>
                    {generalStates.map((state) => <option key={state.name} value={state.name}>{state.name}</option>)}
                  </select>
                  <div className="general-state-save">
                    <input className="field" aria-label="General state name" placeholder="State name" maxLength={100}
                      value={generalStateName} disabled={generalStatesBusy}
                      onChange={(event) => setGeneralStateName(event.target.value)} />
                    <button type="button" className="btn" onClick={handleSaveGeneralState}
                      disabled={generalStatesBusy || !generalStateName.trim()} title="Save current General values and actions">Save</button>
                    <button type="button" className="btn" onClick={handleDeleteGeneralState}
                      disabled={generalStatesBusy || !selectedGeneralState} title="Delete the selected saved state">Delete</button>
                  </div>
                  <small className="hint">Load a state, then Apply XData (F5) to write its settings to CAD.</small>
                  {generalStatesError && <small role="alert" className="general-state-error">{generalStatesError}</small>}
                </div>
                {GENERAL_FIELDS.map(({ key, label, hint }) => (
                  <div className="formrow" key={key}>
                    <label htmlFor={`general-${key}`}>{label} <span className="hint">({key})</span></label>
                    {key === 'visible' ? (
                      <select id={`general-${key}`} className="field" value={general[key]}
                        onChange={(event) => setGeneral((prev) => ({ ...prev, [key]: event.target.value }))}>
                        <option value="">Select visibility</option>
                        {general[key] === '<Varies>' && <option value="<Varies>">&lt;Varies&gt;</option>}
                        <option value="true">Visible</option><option value="false">Hidden</option>
                      </select>
                    ) : (
                      <input id={`general-${key}`} type="text" value={general[key]}
                        className={`field ${general[key] === '<Varies>' ? 'varies' : ''}`}
                        placeholder={key === 'clr' ? 'e.g. 256 / #FF0000' : key === 'lyr' ? 'e.g. 0 / Walls' : 'e.g. 0 / 50 / -2'}
                        onChange={(event) => setGeneral((prev) => ({ ...prev, [key]: event.target.value }))} />
                    )}
                    <small className="scope-desc hint">{hint}</small>
                    <XdataActions label={`${label} property action`} value={generalActions[key]}
                      onChange={(action) => setGeneralActions((prev) => ({ ...prev, [key]: action }))} />
                  </div>
                ))}
              </div>
            </section>
            {/* ====================================================================
                3. SECTION 1: BUILDING (props.building / props.location)
            ==================================================================== */}
            <section
              className={`section ${collapsedSections.building ? 'collapsed' : ''}`}
              id="buildingSection"
            >
              <div className="location-head">
              <button
                type="button"
                onClick={() => toggleSection('building')}
                className="section-head location-toggle"
                aria-expanded={!collapsedSections.building}
              >
                <span className="chev">
                  <svg viewBox="0 0 16 16" aria-hidden="true">
                    <path d="m5 6 3 3 3-3" />
                  </svg>
                </span>
                <span>◎</span>
                <span>Location</span>
                <code className="head-key">props.building</code>
              </button>
                <XdataActions
                  label="All Location XData actions"
                  value={Object.values(buildingActions).every((action) => action === buildingActions.building) ? buildingActions.building : null}
                  onChange={(action) => setBuildingActions({ building: action, floor: action, area: action })}
                />
              </div>

              <div className="section-body">
                {/* Building Row */}
                <div className="formrow">
                  <label
                    htmlFor="building"
                  >
                    Building
                  </label>
                  <input
                    id="building"
                    type="text"
                    value={building.building}
                    onChange={(e) => setBuilding((prev) => ({ ...prev, building: e.target.value }))}
                    className="field mixed"
                    placeholder="e.g. Building A / House"
                  />
                  <button
                    type="button"
                    onClick={() => handleOpenDesc('building', true)}
                    className="btn small scope-desc"
                  >
                    Description
                  </button>
                  <XdataActions
                    label="Building XData action"
                    value={buildingActions.building}
                    onChange={(action) => setBuildingActions((prev) => ({ ...prev, building: action }))}
                  />
                </div>

                {/* Floor Row */}
                <div className="formrow">
                  <label
                    htmlFor="floor"
                  >
                    Floor
                  </label>
                  <input
                    id="floor"
                    type="text"
                    value={building.floor}
                    onChange={(e) => setBuilding((prev) => ({ ...prev, floor: e.target.value }))}
                    className="field mixed"
                    placeholder="e.g. Level 1 / Basement"
                  />
                  <button
                    type="button"
                    onClick={() => handleOpenDesc('floor', true)}
                    className="btn small scope-desc"
                  >
                    Description
                  </button>
                  <XdataActions
                    label="Floor XData action"
                    value={buildingActions.floor}
                    onChange={(action) => setBuildingActions((prev) => ({ ...prev, floor: action }))}
                  />
                </div>

                {/* Area Row */}
                <div className="formrow">
                  <label
                    htmlFor="area"
                  >
                    Area
                  </label>
                  <input
                    id="area"
                    type="text"
                    value={building.area}
                    onChange={(e) => setBuilding((prev) => ({ ...prev, area: e.target.value }))}
                    className="field mixed"
                    placeholder="e.g. Room A / Corridor"
                  />
                  <button
                    type="button"
                    onClick={() => handleOpenDesc('area', true)}
                    className="btn small scope-desc"
                  >
                    Description
                  </button>
                  <XdataActions
                    label="Area XData action"
                    value={buildingActions.area}
                    onChange={(action) => setBuildingActions((prev) => ({ ...prev, area: action }))}
                  />
                </div>
              </div>
            </section>

            {/* ====================================================================
                4. SECTION 2: UNIFIED ARRAY ITEMS GROUP (props.array_items / props.types)
            ==================================================================== */}
            <section
              className={`section ${collapsedSections.array ? 'collapsed' : ''}`}
              id="arraySection"
            >
              <div className="location-head">
              <button
                type="button"
                onClick={() => toggleSection('array')}
                className="section-head location-toggle"
                aria-expanded={!collapsedSections.array}
              >
                <span className="chev">
                  <svg viewBox="0 0 16 16" aria-hidden="true">
                    <path d="m5 6 3 3 3-3" />
                  </svg>
                </span>
                <span>▱</span>
                <span>Array Items ({items.length})</span>
                <code className="head-key">props.array_items</code>
              </button>
                <XdataActions
                  label="All Array Items XData actions"
                  value={items.length === 0 ? emptyArrayAction : items.every((item) => item.action === items[0].action) ? items[0].action : null}
                  onChange={(action) => {
                    setEmptyArrayAction(action);
                    setItems((prev) => prev.map((item) => ({ ...item, action })));
                  }}
                />
              </div>

              <div className="section-body">
                <div className="array-toolbar">
                  <button type="button" onClick={handleAddItem} className="btn primary" id="addItem">
                    ＋ Add Item
                  </button>
                  <span className="hint">
                    Each item has its own type, parameters, quantity factor, and calculated result.
                  </span>
                </div>

                <div className="array-help">
                  {items.length === 0 && emptyArrayAction === 'remove' && (
                    <p>Apply XData will remove all array item data from the selected CAD objects.</p>
                  )}
                  ⓘ <b>&lt;Varies&gt;</b> indicates different values across the selected CAD objects. Choose Nothing, Update, or Remove for each item.
                </div>

                <div id="items"><CategoryPanels onAdd={handleAddItem}>
                  {items.map((it) => {
                    const prefix = prefixFor(it.type);
                    const limit = SEQUENCE_LIMITS[prefix] || 1;
                    const seqOptions = Array.from({ length: limit }, (_, i) => prefix + (i + 1));
                    const itemColor = TYPE_COLORS[it.type] || '#19cde2';
                    const computedVal = getItemComputedResult(it);
                    const unitStr = TYPE_UNITS[it.type] || '';
                    const descInfo = descriptions[it.code?.trim()];
                    const TypeIcon = TYPE_ICONS[it.type] || SquareDashed;

                    return (
                      <article
                        key={it.id}
                        className={`item ${it.collapsed ? 'collapsed' : ''}`}
                        style={{ '--item-color': itemColor }}
                        data-key={it.key}
                        data-type={it.type}
                        data-item-id={it.id}
                        data-code={it.code}
                        data-description={descInfo?.short || descInfo?.shortDescription || it.code}
                        data-quantity={Number.isFinite(computedVal) ? `${formatUsQuantity(computedVal)} ${unitStr}` : 'Error'}
                      >
                        {/* Item Card Header */}
                        <header className="item-head">
                          <button
                            type="button"
                            onClick={() => handleToggleItemCollapse(it.id)}
                            className="item-toggle"
                            aria-label={it.collapsed ? 'Expand item' : 'Collapse item'}
                            aria-expanded={!it.collapsed}
                          >
                            <svg viewBox="0 0 16 16" aria-hidden="true">
                              <path d="m5 6 3 3 3-3" />
                            </svg>
                          </button>

                          <span className="drag" title="Item order handle">
                            ⠿
                          </span>

                          <span className="seq-type-icon" aria-hidden="true">
                            <TypeIcon />
                          </span>

                          <select
                            value={it.key}
                            onChange={(e) => handleChangeItemSeq(it.id, e.target.value)}
                            className="item-seq"
                            aria-label="Type sequence"
                          >
                            {seqOptions.map((opt) => (
                              <option key={opt} value={opt}>
                                {opt}
                              </option>
                            ))}
                          </select>

                          <label className="head-field">
                            <span>
                              Item Code
                            </span>
                            <input
                              type="text"
                              value={it.code}
                              onChange={(e) => handleUpdateItemField(it.id, 'code', e.target.value)}
                              className="field code-field"
                              placeholder="e.g. FloorFin/CT1"
                            />
                          </label>

                          <button
                            type="button"
                            onClick={() => handleOpenDesc(it.code)}
                            className="btn small desc"
                            title="Open the item description (SQLite)"
                          >
                            📄 Description
                          </button>

                          <div className="item-actions">
                            <div className="item-action-mode" role="group" aria-label={`XData action for ${it.key}`}>
                              <button
                                type="button"
                                className={it.action === 'nothing' ? 'active nothing' : ''}
                                aria-pressed={it.action === 'nothing'}
                                aria-label="Nothing"
                                title="Nothing"
                                onClick={() => handleSetItemAction(it.id, 'nothing')}
                              >
                                <Ban aria-hidden="true" />
                                <span>Nothing</span>
                              </button>
                              <button
                                type="button"
                                className={it.action === 'update' ? 'active update' : ''}
                                aria-pressed={it.action === 'update'}
                                aria-label="Update"
                                title="Update"
                                onClick={() => handleSetItemAction(it.id, 'update')}
                              >
                                <RotateCw aria-hidden="true" />
                                <span>Update</span>
                              </button>
                              <button
                                type="button"
                                className={it.action === 'remove' ? 'active remove' : ''}
                                aria-pressed={it.action === 'remove'}
                                aria-label="Remove"
                                title="Remove"
                                onClick={() => handleSetItemAction(it.id, 'remove')}
                              >
                                <Trash2 aria-hidden="true" />
                                <span>Remove</span>
                              </button>
                            </div>
                            <button
                              type="button"
                              onClick={() => {
                                const inputEl = document.querySelector(`[data-key="${it.key}"] .code-field`);
                                if (inputEl) inputEl.focus();
                              }}
                              className="iconbtn edit"
                              title="Focus & edit item code"
                            >
                              <Pencil aria-hidden="true" />
                            </button>
                            <button
                              type="button"
                              onClick={() => handleDeleteItem(it.id)}
                              className="iconbtn delete"
                              title="Delete item"
                            >
                              <Trash2 aria-hidden="true" />
                            </button>
                          </div>
                        </header>

                        {/* Description Preview under Header if available */}
                        {descInfo?.short && !it.collapsed && (
                          <div className="px-3 py-1 bg-[#10141a] text-[11px] text-emerald-400 border-b border-[#1b222d] truncate flex items-center gap-1 font-sans">
                            <span className="opacity-75">↳</span>
                            <span>{descInfo.short}</span>
                          </div>
                        )}

                        {/* Item Card Body */}
                        <div className="item-body">
                          <div className="grid detailed-grid">
                            {/* 1. Type Control with in-card dropdown */}
                            <div className="control type-control">
                              <label>Type</label>
                              <div className="type-select-wrap">
                                <span className="type-icon" aria-hidden="true">
                                  <TypeIcon />
                                </span>
                                <select
                                  value={it.type}
                                  onChange={(e) => handleChangeItemType(it.id, e.target.value)}
                                  className="type-select"
                                  aria-label="Type selector"
                                >
                                  <option value="Length">Length</option>
                                  <option value="Area">Area</option>
                                  <option value="Wall">Wall</option>
                                  <option value="Wall Finishes">Wall Finishes</option>
                                  <option value="Concrete Wall">Concrete Wall</option>
                                  <option value="Steel Wall">Steel Wall</option>
                                  <option value="Beam">Beam</option>
                                  <option value="Column">Column</option>
                                  <option value="Slab">Slab</option>
                                  <option value="Number">Number</option>
                                </select>
                              </div>
                            </div>

                            {/* 2. Type-specific Parameter Controls (User Requirement 2: Parameter / param) */}
                            <div className="control parameters-control">
                              <label className="paramlabel">Parameter ({it.type})</label>
                              <div className="param-grid">
                                {(it.type === 'Length' || it.type === 'Area') && (
                                  <div className="empty">No parameters</div>
                                )}

                                {it.type === 'Wall' && (
                                  <>
                                    <div className="param-choice">
                                      <span className="paramlabel">Material</span>
                                      <div className="radio-row">
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-${it.id}`}
                                            value="concrete"
                                            checked={it.param?.material === 'concrete'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'concrete')}
                                          />
                                          Concrete
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-${it.id}`}
                                            value="steel"
                                            checked={it.param?.material === 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'steel')}
                                          />
                                          Steel
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-${it.id}`}
                                            value="other"
                                            checked={(it.param?.material ?? 'other') === 'other'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'other')}
                                          />
                                          Other
                                        </label>
                                      </div>
                                    </div>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.wt ?? 150}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wt', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <UsNumberInput
                                        step="0.1"
                                        value={it.param?.wh ?? 3.0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.dwh ?? 0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'dwh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Wall Finishes' && (
                                  <>
                                    <div className="param-choice">
                                      <span className="paramlabel">Material</span>
                                      <div className="radio-row">
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-wf-${it.id}`}
                                            value="concrete"
                                            checked={it.param?.material === 'concrete'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'concrete')}
                                          />
                                          Concrete
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-wf-${it.id}`}
                                            value="steel"
                                            checked={it.param?.material === 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'steel')}
                                          />
                                          Steel
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-wf-${it.id}`}
                                            value="other"
                                            checked={(it.param?.material ?? 'other') === 'other'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'other')}
                                          />
                                          Other
                                        </label>
                                      </div>
                                    </div>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.wt ?? 150}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wt', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <UsNumberInput
                                        step="0.1"
                                        value={it.param?.wfh ?? 3.0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wfh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.dwfh ?? 0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'dwfh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Concrete Wall' && (
                                  <>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.wt ?? 150}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wt', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <UsNumberInput
                                        step="0.1"
                                        value={it.param?.wch ?? 3.0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wch', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.dwch ?? 0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'dwch', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Steel Wall' && (
                                  <>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.wt ?? 10}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wt', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <UsNumberInput
                                        step="0.1"
                                        value={it.param?.wsh ?? 3.0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'wsh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <UsNumberInput
                                        value={it.param?.dwsh ?? 0}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'dwsh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Beam' && (
                                  <>
                                    <div className="param-choice">
                                      <span className="paramlabel">Material</span>
                                      <div className="radio-row">
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-bm-${it.id}`}
                                            value="concrete"
                                            checked={it.param?.material !== 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'concrete')}
                                          />
                                          Concrete
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-bm-${it.id}`}
                                            value="steel"
                                            checked={it.param?.material === 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'steel')}
                                          />
                                          Steel
                                        </label>
                                      </div>
                                    </div>
                                    <div className="param-choice">
                                      <span className="paramlabel">Member</span>
                                      <div className="radio-row">
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mem-bm-${it.id}`}
                                            value="BB"
                                            checked={it.param?.member !== 'BT'}
                                            onChange={() => handleUpdateItemParam(it.id, 'member', 'BB')}
                                          />
                                          Beam (BB)
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mem-bm-${it.id}`}
                                            value="BT"
                                            checked={it.param?.member === 'BT'}
                                            onChange={() => handleUpdateItemParam(it.id, 'member', 'BT')}
                                          />
                                          Tie Beam (BT)
                                        </label>
                                      </div>
                                    </div>
                                    <label className="param-field">
                                      <span>Height <small>(mm)</small><em>optional</em></span>
                                      <UsNumberInput
                                        value={it.param?.bh ?? 450}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'bh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Width <small>(mm)</small><em>optional</em></span>
                                      <UsNumberInput
                                        value={it.param?.bb ?? 250}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'bb', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Column' && (
                                  <>
                                    <div className="param-choice">
                                      <span className="paramlabel">Material</span>
                                      <div className="radio-row">
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-col-${it.id}`}
                                            value="concrete"
                                            checked={it.param?.material !== 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'concrete')}
                                          />
                                          Concrete
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-col-${it.id}`}
                                            value="steel"
                                            checked={it.param?.material === 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'steel')}
                                          />
                                          Steel
                                        </label>
                                      </div>
                                    </div>
                                    <label className="param-field">
                                      <span>Height <small>(mm)</small><em>optional</em></span>
                                      <UsNumberInput
                                        value={it.param?.bh ?? 400}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'bh', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Width <small>(mm)</small><em>optional</em></span>
                                      <UsNumberInput
                                        value={it.param?.bb ?? 400}
                                        onValueChange={(value) => handleUpdateItemParam(it.id, 'bb', value)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-check">
                                      <input
                                        type="checkbox"
                                        checked={Boolean(it.param?.steelWeight)}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'steelWeight', e.target.checked)}
                                      />
                                      Steel Weight
                                    </label>
                                  </>
                                )}

                                {it.type === 'Slab' && (
                                  <>
                                    <div className="param-choice">
                                      <span className="paramlabel">Material</span>
                                      <div className="radio-row">
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-slab-${it.id}`}
                                            value="concrete"
                                            checked={it.param?.material !== 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'concrete')}
                                          />
                                          Concrete
                                        </label>
                                        <label>
                                          <input
                                            type="radio"
                                            name={`mat-slab-${it.id}`}
                                            value="steel"
                                            checked={it.param?.material === 'steel'}
                                            onChange={() => handleUpdateItemParam(it.id, 'material', 'steel')}
                                          />
                                          Steel
                                        </label>
                                      </div>
                                    </div>
                                    <label className="param-check">
                                      <input
                                        type="checkbox"
                                        checked={Boolean(it.param?.steelWeight)}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'steelWeight', e.target.checked)}
                                      />
                                      Steel Weight
                                    </label>
                                  </>
                                )}

                                {it.type === 'Number' && (
                                  <div className="empty">Count quantity (Nr); no geometric parameters</div>
                                )}
                              </div>
                            </div>

                            {/* 3. Factor / Expression */}
                            <div className="control factor-control">
                              <label>Factor / Expression</label>
                              <input
                                type="text"
                                value={it.factor}
                                onChange={(e) => handleUpdateItemField(it.id, 'factor', e.target.value)}
                                className="field factor-field"
                                placeholder="1 or {a:1,b:2,c:3} or doFun(...)"
                              />
                            </div>

                            {/* 4. Evaluated Result */}
                            <div className="control result-control">
                              <label>Result</label>
                              <div className="qty-value factor-result">
                                {Number.isFinite(computedVal)
                                  ? `${formatUsQuantity(computedVal)} ${unitStr}`
                                  : 'Error'}
                              </div>
                            </div>
                          </div>
                        </div>
                      </article>
                    );
                  })}
                </CategoryPanels></div>
              </div>
            </section>

            {/* ====================================================================
                5. SECTION 3: COMPUTED OUTPUT — TREE VIEW (props.result)
            ==================================================================== */}
            <section
              className={`section computed ${collapsedSections.computed ? 'collapsed' : ''}`}
              id="computedSection"
            >
              <button
                type="button"
                onClick={() => toggleSection('computed')}
                className="section-head"
                aria-expanded={!collapsedSections.computed}
              >
                <span className="chev">
                  <svg viewBox="0 0 16 16" aria-hidden="true">
                    <path d="m5 6 3 3 3-3" />
                  </svg>
                </span>
                <span>▦</span>
                <span>Computed Output — Tree View</span>
                <code className="head-key">props.result</code>
              </button>

              <div className="section-body">
                <div className="tree-controls">
                  <button
                    type="button"
                    onClick={() => {
                      setDraftSortRows(sortRows.map((r) => ({ ...r })));
                      setSelectedSortIndex(0);
                      setSortModalOpen(true);
                    }}
                    className="btn"
                    id="sortLevelsOpen"
                  >
                    ↕ Change Level
                  </button>
                  <span id="treeLevelHint">
                    Sort levels: {sortRows.map((r) => r.column).join(' → ')}
                  </span>
                </div>

                <div className="tree" id="outputTree">
                  <ComputedTreeNodes nodes={computedTree} />
                  {computedTree.length === 0 && <span className="hint">No items to display for the selected entities.</span>}
                </div>
                <div className="tree-note">
                  Building, Floor, and Area show only their short description (or code when no short description is saved) and the selected entity count. Each Type may contain multiple Code rows; quantities appear on Code rows only.
                </div>
              </div>
            </section>
            </div>

            {/* ====================================================================
                6. ACTION FOOTER
            ==================================================================== */}
            <div className="actions">
              <span className="note">
                ⓘ Collapsed sections are skipped. Nothing keeps existing values, Update changes them, and Remove resets General properties or deletes metadata.
              </span>
              <div className="right">
                <button
                  type="button"
                  id="apply"
                  onClick={handleApplyXdata}
                  aria-keyshortcuts="F5"
                  className="btn primary"
                  title="Apply QTO metadata to CAD entities (props.building & props.types)"
                >
                  ☁ Apply XData (F5)
                </button>
                <button
                  type="button"
                  id="save"
                  onClick={handleSaveSchedule}
                  className="btn orange"
                  title="Save QTO schedule"
                >
                  ✓ Save Schedule
                </button>
              </div>
            </div>
          </>
        )}
      </main>

      {/* ====================================================================
          7. FLOATING TOAST NOTIFICATION
      ==================================================================== */}
      {toastMsg && (
        <div className="toast show" role="status">
          {toastMsg}
        </div>
      )}

      {drawingManagerOpen && <DrawingManager project={project} onClose={() => setDrawingManagerOpen(false)}
        onDrawingOpened={() => {
          selectionVersionRef.current = 0;
          selectionHashRef.current = null;
          setDrawingManagerOpen(false);
        }} />}
      {itemToDelete && (
        <div className="modal show delete-confirm-modal">
          <section
            className="modalbox delete-confirm-box"
            role="alertdialog"
            aria-modal="true"
            aria-labelledby="delete-confirm-title"
            aria-describedby="delete-confirm-message"
          >
            <h2 id="delete-confirm-title" className="delete-confirm-title">
              Confirm delete
            </h2>
            <p id="delete-confirm-message" className="delete-confirm-message">
              Delete item <strong>{itemToDelete.key}</strong>?
            </p>
            <div className="delete-confirm-actions">
              <button
                type="button"
                className="btn"
                autoFocus
                onClick={() => setItemToDelete(null)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="btn danger"
                onClick={handleConfirmDeleteItem}
              >
                Delete
              </button>
            </div>
          </section>
        </div>
      )}

      {/* ====================================================================
          8. SORT LEVELS MODAL (Windows Style Dialog)
      ==================================================================== */}
      {sortModalOpen && (
        <div className="modal show" id="sortModal" role="dialog" aria-modal="true">
          <section className="modalbox sort-box">
            <header className="sort-titlebar">
              <span className="window-dots">
                <i></i>
                <i></i>
                <i></i>
              </span>
              <h2>Sort Levels</h2>
              <button
                type="button"
                className="sort-close"
                onClick={() => setSortModalOpen(false)}
                aria-label="Close"
              >
                ×
              </button>
            </header>

            <div className="sort-toolbar">
              <button
                type="button"
                className="sort-action add"
                onClick={() => {
                  const cols = ['Building', 'Floor', 'Area', 'Type', 'Code'];
                  const available = cols.find((c) => !draftSortRows.some((r) => r.column === c));
                  if (!available) {
                    showToast('All available sort levels have been added');
                    return;
                  }
                  setDraftSortRows((prev) => [
                    ...prev,
                    { column: available, sortOn: 'Code', order: 'A to Z' }
                  ]);
                  setSelectedSortIndex(draftSortRows.length);
                }}
              >
                ＋ Add Level
              </button>

              <button
                type="button"
                className="sort-action delete"
                onClick={() => {
                  if (draftSortRows.length <= 1) {
                    showToast('At least one sort level is required');
                    return;
                  }
                  setDraftSortRows((prev) => prev.filter((_, i) => i !== selectedSortIndex));
                  setSelectedSortIndex((prev) => Math.max(0, prev - 1));
                }}
              >
                × Delete Level
              </button>

              <span className="sort-spacer"></span>

              <button
                type="button"
                className="sort-move"
                title="Move level up"
                onClick={() => {
                  if (selectedSortIndex <= 0) return;
                  setDraftSortRows((prev) => {
                    const next = [...prev];
                    const temp = next[selectedSortIndex - 1];
                    next[selectedSortIndex - 1] = next[selectedSortIndex];
                    next[selectedSortIndex] = temp;
                    return next;
                  });
                  setSelectedSortIndex((prev) => prev - 1);
                }}
              >
                ⌃
              </button>

              <button
                type="button"
                className="sort-move"
                title="Move level down"
                onClick={() => {
                  if (selectedSortIndex >= draftSortRows.length - 1) return;
                  setDraftSortRows((prev) => {
                    const next = [...prev];
                    const temp = next[selectedSortIndex + 1];
                    next[selectedSortIndex + 1] = next[selectedSortIndex];
                    next[selectedSortIndex] = temp;
                    return next;
                  });
                  setSelectedSortIndex((prev) => prev + 1);
                }}
              >
                ⌄
              </button>
            </div>

            <div className="sort-content">
              <div className="sort-grid-header">
                <span></span>
                <span>Column</span>
                <span>Sort On</span>
                <span>Order</span>
              </div>
              <div className="sort-rows">
                {draftSortRows.map((row, idx) => (
                  <div
                    key={idx}
                    className={`sort-row ${idx === selectedSortIndex ? 'selected' : ''}`}
                    onClick={() => setSelectedSortIndex(idx)}
                  >
                    <div className="sort-row-label">
                      {idx === 0 ? 'Sort by' : 'Then by'}
                    </div>

                    <div className="sort-row-fields">
                      <label className="sort-field">
                        <span>Column</span>
                        <select
                          value={row.column}
                          onChange={(e) => {
                            const val = e.target.value;
                            if (draftSortRows.some((r, i) => i !== idx && r.column === val)) {
                              showToast('Sort columns must be unique');
                              return;
                            }
                            setDraftSortRows((prev) =>
                              prev.map((r, i) => (i === idx ? { ...r, column: val } : r))
                            );
                          }}
                        >
                          {['Building', 'Floor', 'Area', 'Type', 'Code'].map((col) => (
                            <option key={col} value={col}>
                              {col}
                            </option>
                          ))}
                        </select>
                      </label>

                      <label className="sort-field">
                        <span>Sort On</span>
                        <select
                          value={row.sortOn}
                          onChange={(e) => {
                            const val = e.target.value;
                            setDraftSortRows((prev) =>
                              prev.map((r, i) => (i === idx ? { ...r, sortOn: val } : r))
                            );
                          }}
                        >
                          <option value="Code">Code</option>
                          <option value="Short Description">Short Description</option>
                        </select>
                      </label>

                      <label className="sort-field">
                        <span>Order</span>
                        <select
                          value={row.order}
                          onChange={(e) => {
                            const val = e.target.value;
                            setDraftSortRows((prev) =>
                              prev.map((r, i) => (i === idx ? { ...r, order: val } : r))
                            );
                          }}
                        >
                          <option value="A to Z">A to Z</option>
                          <option value="Z to A">Z to A</option>
                          <option value="Smallest to Largest">Smallest to Largest</option>
                          <option value="Largest to Smallest">Largest to Smallest</option>
                        </select>
                      </label>
                    </div>
                  </div>
                ))}
              </div>
            </div>

            <footer className="sort-footer">
              <button
                type="button"
                className="btn"
                onClick={() => setSortModalOpen(false)}
              >
                Cancel
              </button>
              <button
                type="button"
                className="btn sort-ok"
                onClick={() => {
                  setSortRows(draftSortRows);
                  setSortModalOpen(false);
                  showToast('Display sort order applied');
                }}
              >
                OK
              </button>
            </footer>
          </section>
        </div>
      )}

      {/* ====================================================================
          9. MANAGE DESCRIPTION MODAL (SQLite)
      ==================================================================== */}
      {descModalOpen && (
        <DescriptionModal
          projectPath={project.path}
          isOpen={descModalOpen}
          onClose={() => setDescModalOpen(false)}
          initialCode={descModalCode}
          descriptions={descriptions}
          onSaveDescription={handleSaveDescription}
          onDeleteDescription={handleDeleteDescription}
          cadConnected={cadConnected}
        />
      )}

      {/* ====================================================================
          10. JSON SCHEMA MODAL
      ==================================================================== */}
      {jsonModalOpen && (
        <div className="modal show" onClick={() => setJsonModalOpen(false)}>
          <div
            className="modalbox"
            onClick={(e) => e.stopPropagation()}
            style={{ maxWidth: '620px' }}
          >
            <h2>Entity XData Schema (props)</h2>
            <pre className="text-emerald-300 bg-[#0d1117] p-3 rounded border border-slate-700/80 font-mono text-xs max-h-[60vh] overflow-auto">
              {JSON.stringify(entityJsonPayload, null, 2)}
            </pre>
            <div className="modalfoot">
              <button
                type="button"
                onClick={() => {
                  navigator.clipboard.writeText(JSON.stringify(entityJsonPayload, null, 2));
                  showToast('JSON copied to clipboard');
                }}
                className="btn primary"
              >
                Copy JSON
              </button>
              <button
                type="button"
                onClick={() => setJsonModalOpen(false)}
                className="btn"
              >
                Close
              </button>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
