import React, { useState, useMemo, useEffect, useCallback, useRef } from 'react';
import { invoke } from '@tauri-apps/api/core';
import WbsReportView from './WbsReportView';
import DescriptionModal from './DescriptionModal';

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
  Length: '↔',
  Area: '▧',
  Wall: '▤',
  'Wall Finishes': '▥',
  'Concrete Wall': '▤',
  'Steel Wall': '▥',
  Beam: '▰',
  Column: '⬡',
  Slab: '▱',
  Number: '#'
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
    throw new Error('Gunakan objek dengan nilai numerik, misal {a:1, b:2, c:3}');
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
    if (typeof fn !== 'function') throw new Error('Fungsi global tidak dikenal: ' + call[1]);
    const result = fn(...Object.values(parameters));
    if (!Number.isFinite(Number(result))) throw new Error('Hasil fungsi harus berupa angka');
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
    update: true,
    collapsed: false,
    factor: '1',
    param: {}
  },
  {
    id: 'item-2',
    key: 'A1',
    type: 'Area',
    code: 'FloorFin/CT1',
    update: true,
    collapsed: false,
    factor: '{a:1,b:2,c:3}',
    param: {}
  },
  {
    id: 'item-3',
    key: 'W1',
    type: 'Wall',
    code: 'Wall/W1',
    update: true,
    collapsed: false,
    factor: 'doFun({a:1,b:2,c:3})',
    param: {
      material: 'concrete',
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

export default function App() {
  // Navigation: 'qto' | 'report'
  const [activeNavMenu, setActiveNavMenu] = useState('qto');

  // CAD Connection & IPC State
  const [cadConnected, setCadConnected] = useState(true);
  const [cadPlatform, setCadPlatform] = useState('GstarCAD');
  const [cadDwgName, setCadDwgName] = useState('Drawing2.dwg');
  const [ipcPort] = useState(8123);
  const [isPinned, setIsPinned] = useState(true);
  const [scale, setScale] = useState('m → m (1:1)');

  // Selected Entities from CAD
  const [entities, setEntities] = useState([
    {
      id: 21362,
      handle: '5372',
      lyr: '0',
      l: 28.0,
      a: 18.0,
      rawCadData: { props: { building: 'Rumah', floor: 'Lt. 1', area: 'Room A' } }
    },
    {
      id: 21361,
      handle: '5371',
      lyr: '0',
      l: 12.0,
      a: 18.0,
      rawCadData: { props: { building: 'Rumah', floor: 'Lt. 1', area: 'Room A' } }
    },
    {
      id: 21360,
      handle: '5370',
      lyr: '0',
      l: 18.0,
      a: 12.0,
      rawCadData: { props: { building: 'Rumah', floor: 'Lt. 1', area: 'Room A' } }
    }
  ]);

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
    building: false,
    array: false,
    computed: false
  });

  const toggleSection = (name) => {
    setCollapsedSections((prev) => ({ ...prev, [name]: !prev[name] }));
  };

  // Section 1: Building (props.building / props.location)
  const [building, setBuilding] = useState({
    building: 'Rumah',
    floor: 'Lt. 1',
    area: 'Room A'
  });

  // Building Update Checkboxes
  const [buildingUpdate, setBuildingUpdate] = useState({
    building: true,
    floor: true,
    area: true
  });

  // Section 2: Array Items (props.array_items / props.types)
  const [items, setItems] = useState(DEFAULT_ITEMS);

  // Section 3: Sort Levels & Output Tree
  const [sortRows, setSortRows] = useState(DEFAULT_SORT_ROWS);
  const [sortModalOpen, setSortModalOpen] = useState(false);
  const [draftSortRows, setDraftSortRows] = useState(DEFAULT_SORT_ROWS);
  const [selectedSortIndex, setSelectedSortIndex] = useState(0);

  // SQLite Descriptions Map
  const [descriptions, setDescriptions] = useState({
    'FloorFin/CT1': {
      items: ['Pek. Arsitektur', 'Pekerjaan Lantai', 'Keramik'],
      short: 'Keramik ex. Roman uk.60x60',
      full: 'Keramik lantai ukuran 60x60 polished'
    },
    'FloorFin/CT2': {
      items: ['Pek. Arsitektur', 'Pekerjaan Lantai', 'Porcelain'],
      short: 'Porcelain floor tile 60x60',
      full: ''
    },
    'Wall/W1': {
      items: ['Pek. Struktur', 'Dinding', 'Beton'],
      short: 'Concrete wall t=150mm',
      full: ''
    }
  });

  // Modals
  const [descModalOpen, setDescModalOpen] = useState(false);
  const [descModalCode, setDescModalCode] = useState('');
  const [descModalScope, setDescModalScope] = useState(null); // 'building' | 'floor' | 'area' | null
  const [jsonModalOpen, setJsonModalOpen] = useState(false);

  // Fetch initial descriptions from SQLite
  useEffect(() => {
    const fetchDesc = async () => {
      try {
        const res = await invoke('cad_get_descriptions', { port: ipcPort });
        if (res && typeof res === 'object' && !res.error) {
          setDescriptions((prev) => ({ ...prev, ...res }));
        }
      } catch (_e) {
        // Fallback in demo mode
      }
    };
    fetchDesc();
  }, [ipcPort]);

  // Poll CAD on Demand (Pick Selection)
  const handlePickSelection = async () => {
    showToast('Polling objek dari CAD...');
    try {
      const res = await invoke('cad_poll_selection', { lastVersion: 0, port: ipcPort });
      if (res && !res.error) {
        setCadConnected(true);
        if (res.platform) setCadPlatform(res.platform);
        if (res.dwg) setCadDwgName(res.dwg);
        if (Array.isArray(res.entities) && res.entities.length > 0) {
          setEntities(res.entities);
          showToast(`Terhubung ke ${res.platform}: ${res.entities.length} objek terpilih`);
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
          showToast(`Terhubung ke ${res.platform}: ${res.handles.length} objek terpilih`);
        } else {
          showToast('Tidak ada objek CAD yang dipilih (0 objek)');
        }
      } else {
        showToast('Tidak ada respon dari CAD (Demo Mode)');
      }
    } catch (_e) {
      setCadConnected(false);
      showToast('CAD IPC tidak terhubung (127.0.0.1:8123) — Demo Mode');
    }
  };

  // Add Item to Array
  const handleAddItem = () => {
    const nextSeq = getNextSequence('L', items);
    if (!nextSeq) {
      showToast('Slot sequence tipe Length (L1-L4) sudah penuh');
      return;
    }
    const newItem = {
      id: `item-${Date.now()}`,
      key: nextSeq,
      type: 'Length',
      code: nextSeq,
      update: true,
      collapsed: false,
      factor: '1',
      param: {}
    };
    setItems((prev) => [...prev, newItem]);
    showToast(`Item baru (${nextSeq}) berhasil ditambahkan`);
  };

  // Delete Item
  const handleDeleteItem = (id) => {
    const it = items.find((x) => x.id === id);
    if (!it) return;
    if (window.confirm(`Hapus item ${it.key}?`)) {
      setItems((prev) => prev.filter((x) => x.id !== id));
      showToast(`Item ${it.key} dihapus`);
    }
  };

  // Toggle Item Collapse
  const handleToggleItemCollapse = (id) => {
    setItems((prev) =>
      prev.map((it) => (it.id === id ? { ...it, collapsed: !it.collapsed } : it))
    );
  };

  // Toggle Item Update Checkbox (or Double Click on Label)
  const handleToggleItemUpdate = (id) => {
    setItems((prev) =>
      prev.map((it) => (it.id === id ? { ...it, update: !it.update } : it))
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
          showToast(`Slot sequence tipe ${newType} sudah penuh`);
          return it;
        }
        let defaultParam = {};
        if (newType === 'Wall' || newType === 'Wall Finishes') {
          defaultParam = { material: 'concrete', wt: 150, wh: 3.0, dwh: 0 };
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
    showToast(`Tipe ${newType} diperbarui langsung pada kartu`);
  };

  // Change Item Sequence
  const handleChangeItemSeq = (id, newSeq) => {
    if (items.some((it) => it.id !== id && it.key === newSeq)) {
      showToast(`Sequence ${newSeq} sudah digunakan dalam array`);
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
    (item) => {
      let baseVal = 1;
      const count = entities.length || 1;
      const firstEnt = entities[0] || {};
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
          baseVal = count;
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
    // 1. If any tree is collapsed, skip update according to User Requirement 1
    const skipBuilding = collapsedSections.building;
    const skipArray = collapsedSections.array;

    if (skipBuilding && skipArray) {
      showToast('Apply XData dilewati: Grup Building & Array Items keduanya dalam keadaan collapse');
      return;
    }

    if (!entities || entities.length === 0) {
      showToast('Tidak ada objek CAD yang dipilih untuk di-apply XData');
      return;
    }

    const updatedFieldsList = [];
    if (!skipBuilding) {
      if (buildingUpdate.building) updatedFieldsList.push('building');
      if (buildingUpdate.floor) updatedFieldsList.push('floor');
      if (buildingUpdate.area) updatedFieldsList.push('area');
    }

    const activeItems = skipArray ? [] : items.filter((it) => it.update);
    if (!skipArray) {
      updatedFieldsList.push(`${activeItems.length} item(s)`);
    }

    // Prepare patches per entity
    const patches = entities.map((ent) => {
      const existingData = ent.rawCadData || {};
      const existingProps = existingData.props || {};

      const nextProps = { ...existingProps };

      // Update Building / Floor / Area if not collapsed and update checkbox is checked
      if (!skipBuilding) {
        if (buildingUpdate.building && building.building !== '<Varies>') {
          nextProps.building = building.building;
        }
        if (buildingUpdate.floor && building.floor !== '<Varies>') {
          nextProps.floor = building.floor;
        }
        if (buildingUpdate.area && building.area !== '<Varies>') {
          nextProps.area = building.area;
        }
      }

      // Update Types / Array Items if not collapsed
      if (!skipArray) {
        const nextTypes = { ...(nextProps.types || {}) };
        for (const it of activeItems) {
          const typeKey = it.type.toLowerCase().replace(/\s+/g, '_');
          const evalQty = getItemComputedResult(it);
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
        data: {
          ...existingData,
          props: nextProps
        }
      };
    });

    try {
      if (cadConnected) {
        const res = await invoke('cad_apply_qto_xdata', { patches, port: ipcPort });
        showToast(`Sukses menulis XData ke ${res?.count || entities.length} objek CAD (${updatedFieldsList.join(', ')})`);
      } else {
        showToast(`[Demo] XData diaplikasikan ke ${entities.length} objek (${updatedFieldsList.join(', ')})`);
      }
    } catch (err) {
      alert(`Gagal menulis XData ke CAD:\n${String(err)}`);
      showToast(`Error: ${String(err)}`);
    }
  };

  // Save Schedule Handler
  const handleSaveSchedule = () => {
    showToast('Schedule QTO berhasil disimpan dalam sesi ini');
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

  return (
    <div className="min-h-screen bg-[#0b0e12] text-[#edf3fa] font-sans p-4 flex flex-col items-center">
      <main className="w-full max-w-[1050px] bg-[#101318] border border-[#46515e] rounded-[9px] shadow-[0_18px_55px_#0009] overflow-hidden flex flex-col">
        {/* ====================================================================
            1. TOP NAVIGATION MENU STRIP
        ==================================================================== */}
        <nav className="menu-strip" aria-label="Main menu">
          <button
            type="button"
            onClick={() => setActiveNavMenu('qto')}
            className={`menutab ${activeNavMenu === 'qto' ? 'active' : ''}`}
          >
            <span>☷</span>
            <span>QTO</span>
          </button>
          <button
            type="button"
            onClick={() => setActiveNavMenu('report')}
            className={`menutab report ${activeNavMenu === 'report' ? 'active' : ''}`}
          >
            <span>▣</span>
            <span>Report (WBS Tree View)</span>
          </button>
          <span className="top-cad">
            ⌁ {cadPlatform} {cadConnected ? '(Connected)' : '(Offline)'}
          </span>
        </nav>

        {/* If Report Menu is Active, render WBS Report View */}
        {activeNavMenu === 'report' ? (
          <WbsReportView
            cadConnected={cadConnected}
            cadPlatform={cadPlatform}
            cadDwgName={cadDwgName}
            ipcPort={ipcPort}
            selectedEntities={entities}
            descriptions={descriptions}
            onSaveDescription={handleSaveDescription}
            onDeleteDescription={handleDeleteDescription}
          />
        ) : (
          <>
            {/* ====================================================================
                2. SECOND TOOLBAR
            ==================================================================== */}
            <div className="toolbar" aria-label="QTO toolbar">
              <button
                type="button"
                className="tool-button tool-qto"
                onClick={() => showToast('Menu QTO Aktif')}
              >
                <span>☷</span>
                <span>QTO</span>
              </button>

              <span className="doc" title={cadDwgName}>
                {cadDwgName} · {entities.length} selected
              </span>

              <span className="cad-status" title="Status koneksi CAD">
                ⌁ {cadPlatform}
              </span>

              <select
                value={scale}
                onChange={(e) => {
                  setScale(e.target.value);
                  showToast(`Skala gambar diatur: ${e.target.value}`);
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
                title="Ambil objek yang sedang dipilih di CAD"
              >
                <span>⊕</span>
                <span>Pick Selection</span>
              </button>

              <button
                type="button"
                id="pinBtn"
                onClick={() => {
                  setIsPinned(!isPinned);
                  showToast(!isPinned ? 'Jendela di-pin (Always on Top)' : 'Jendela di-unpin');
                }}
                className={`tool-button pin ${isPinned ? 'active' : ''}`}
                title={isPinned ? 'Pinned Always-on-Top (Klik untuk unpin)' : 'Pin Always-on-Top'}
              >
                📌
              </button>

              <button
                type="button"
                id="gridBtn"
                onClick={() => showToast(`Jumlah item array aktif: ${items.length}`)}
                className="tool-button tool-count"
                title="Jumlah Item"
              >
                ▦ {items.length}
              </button>

              <button
                type="button"
                id="jsonBtn"
                onClick={() => setJsonModalOpen(true)}
                className="tool-button json"
                title="Lihat skema XData JSON input/output"
              >
                &lt;/&gt; JSON
              </button>
            </div>

            {/* ====================================================================
                3. SECTION 1: BUILDING (props.building / props.location)
            ==================================================================== */}
            <section
              className={`section ${collapsedSections.building ? 'collapsed' : ''}`}
              id="buildingSection"
            >
              <button
                type="button"
                onClick={() => toggleSection('building')}
                className="section-head"
                aria-expanded={!collapsedSections.building}
              >
                <span className="chev">
                  <svg viewBox="0 0 16 16" aria-hidden="true">
                    <path d="m5 6 3 3 3-3" />
                  </svg>
                </span>
                <span>◎</span>
                <span>Building</span>
                <code className="head-key">props.building</code>
              </button>

              <div className="section-body">
                {/* Building Row */}
                <div className="formrow">
                  <label
                    htmlFor="building"
                    onDoubleClick={() => {
                      setBuildingUpdate((prev) => ({ ...prev, building: !prev.building }));
                      showToast(`Update Building ${!buildingUpdate.building ? 'ON' : 'OFF'}`);
                    }}
                    title="Double click label untuk toggle Update checkbox on/off"
                    className="cursor-pointer select-none"
                  >
                    Building
                  </label>
                  <input
                    id="building"
                    type="text"
                    value={building.building}
                    onChange={(e) => setBuilding((prev) => ({ ...prev, building: e.target.value }))}
                    className="field mixed"
                    placeholder="Contoh: Gedung A / Rumah"
                  />
                  <button
                    type="button"
                    onClick={() => handleOpenDesc('building', true)}
                    className="btn small scope-desc"
                  >
                    Description
                  </button>
                  <label
                    className="update-check"
                    onDoubleClick={() => {
                      setBuildingUpdate((prev) => ({ ...prev, building: !prev.building }));
                    }}
                  >
                    <input
                      type="checkbox"
                      checked={buildingUpdate.building}
                      onChange={(e) =>
                        setBuildingUpdate((prev) => ({ ...prev, building: e.target.checked }))
                      }
                    />
                    Update
                  </label>
                </div>

                {/* Floor Row */}
                <div className="formrow">
                  <label
                    htmlFor="floor"
                    onDoubleClick={() => {
                      setBuildingUpdate((prev) => ({ ...prev, floor: !prev.floor }));
                      showToast(`Update Floor ${!buildingUpdate.floor ? 'ON' : 'OFF'}`);
                    }}
                    title="Double click label untuk toggle Update checkbox on/off"
                    className="cursor-pointer select-none"
                  >
                    Floor
                  </label>
                  <input
                    id="floor"
                    type="text"
                    value={building.floor}
                    onChange={(e) => setBuilding((prev) => ({ ...prev, floor: e.target.value }))}
                    className="field mixed"
                    placeholder="Contoh: Lantai 1 / Lt. Basement"
                  />
                  <button
                    type="button"
                    onClick={() => handleOpenDesc('floor', true)}
                    className="btn small scope-desc"
                  >
                    Description
                  </button>
                  <label
                    className="update-check"
                    onDoubleClick={() => {
                      setBuildingUpdate((prev) => ({ ...prev, floor: !prev.floor }));
                    }}
                  >
                    <input
                      type="checkbox"
                      checked={buildingUpdate.floor}
                      onChange={(e) =>
                        setBuildingUpdate((prev) => ({ ...prev, floor: e.target.checked }))
                      }
                    />
                    Update
                  </label>
                </div>

                {/* Area Row */}
                <div className="formrow">
                  <label
                    htmlFor="area"
                    onDoubleClick={() => {
                      setBuildingUpdate((prev) => ({ ...prev, area: !prev.area }));
                      showToast(`Update Area ${!buildingUpdate.area ? 'ON' : 'OFF'}`);
                    }}
                    title="Double click label untuk toggle Update checkbox on/off"
                    className="cursor-pointer select-none"
                  >
                    Area
                  </label>
                  <input
                    id="area"
                    type="text"
                    value={building.area}
                    onChange={(e) => setBuilding((prev) => ({ ...prev, area: e.target.value }))}
                    className="field mixed"
                    placeholder="Contoh: Room A / Koridor"
                  />
                  <button
                    type="button"
                    onClick={() => handleOpenDesc('area', true)}
                    className="btn small scope-desc"
                  >
                    Description
                  </button>
                  <label
                    className="update-check"
                    onDoubleClick={() => {
                      setBuildingUpdate((prev) => ({ ...prev, area: !prev.area }));
                    }}
                  >
                    <input
                      type="checkbox"
                      checked={buildingUpdate.area}
                      onChange={(e) =>
                        setBuildingUpdate((prev) => ({ ...prev, area: e.target.checked }))
                      }
                    />
                    Update
                  </label>
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
              <button
                type="button"
                onClick={() => toggleSection('array')}
                className="section-head"
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

              <div className="section-body">
                <div className="array-toolbar">
                  <button type="button" onClick={handleAddItem} className="btn primary" id="addItem">
                    ＋ Add Item
                  </button>
                  <span className="hint">
                    Setiap item menyimpan tipe, parameter, faktor kuantitas, dan hasil evaluasi tersendiri.
                  </span>
                </div>

                <div className="array-help">
                  ⓘ Nilai <b>&lt;Varies&gt;</b> menunjukkan perbedaan nilai antar objek CAD yang dipilih. Perubahan hanya diterapkan pada item yang dicentang Update.
                </div>

                <div id="items">
                  {items.map((it) => {
                    const prefix = prefixFor(it.type);
                    const limit = SEQUENCE_LIMITS[prefix] || 1;
                    const seqOptions = Array.from({ length: limit }, (_, i) => prefix + (i + 1));
                    const itemColor = TYPE_COLORS[it.type] || '#19cde2';
                    const computedVal = getItemComputedResult(it);
                    const unitStr = TYPE_UNITS[it.type] || '';
                    const descInfo = descriptions[it.code?.trim()];

                    return (
                      <article
                        key={it.id}
                        className={`item ${it.collapsed ? 'collapsed' : ''}`}
                        style={{ '--item-color': itemColor }}
                        data-key={it.key}
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
                            {TYPE_ICONS[it.type] || '◇'}
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

                          <label
                            className="update-check"
                            onDoubleClick={() => handleToggleItemUpdate(it.id)}
                            title="Double click untuk toggle Update"
                          >
                            <input
                              type="checkbox"
                              checked={it.update}
                              onChange={(e) => handleUpdateItemField(it.id, 'update', e.target.checked)}
                              className="item-update"
                            />
                            Update
                          </label>

                          <label className="head-field">
                            <span
                              onDoubleClick={() => handleToggleItemUpdate(it.id)}
                              className="cursor-pointer select-none"
                              title="Double click label untuk toggle Update checkbox on/off"
                            >
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
                            title="Buka deskripsi pekerjaan (SQLite deskripsi)"
                          >
                            📄 Description
                          </button>

                          <div className="item-actions">
                            <button
                              type="button"
                              onClick={() => {
                                const inputEl = document.querySelector(`[data-key="${it.key}"] .code-field`);
                                if (inputEl) inputEl.focus();
                              }}
                              className="iconbtn edit"
                              title="Focus & edit item code"
                            >
                              ✎
                            </button>
                            <button
                              type="button"
                              onClick={() => handleDeleteItem(it.id)}
                              className="iconbtn delete"
                              title="Delete item"
                            >
                              ▤
                            </button>
                          </div>
                        </header>

                        {/* Description Preview under Header if available */}
                        {descInfo?.short && (
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
                                  {TYPE_ICONS[it.type] || '◇'}
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
                                            checked={it.param?.material !== 'steel'}
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
                                      </div>
                                    </div>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.wt ?? 150}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wt', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <input
                                        type="number"
                                        step="0.1"
                                        value={it.param?.wh ?? 3.0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wh', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.dwh ?? 0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'dwh', parseFloat(e.target.value) || 0)}
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
                                            checked={it.param?.material !== 'steel'}
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
                                      </div>
                                    </div>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.wt ?? 150}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wt', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <input
                                        type="number"
                                        step="0.1"
                                        value={it.param?.wfh ?? 3.0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wfh', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.dwfh ?? 0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'dwfh', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Concrete Wall' && (
                                  <>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.wt ?? 150}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wt', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <input
                                        type="number"
                                        step="0.1"
                                        value={it.param?.wch ?? 3.0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wch', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.dwch ?? 0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'dwch', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                  </>
                                )}

                                {it.type === 'Steel Wall' && (
                                  <>
                                    <label className="param-field">
                                      <span>Thickness <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.wt ?? 10}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wt', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Height <small>(m)</small></span>
                                      <input
                                        type="number"
                                        step="0.1"
                                        value={it.param?.wsh ?? 3.0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'wsh', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Deduction <small>(mm)</small></span>
                                      <input
                                        type="number"
                                        value={it.param?.dwsh ?? 0}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'dwsh', parseFloat(e.target.value) || 0)}
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
                                      <input
                                        type="number"
                                        value={it.param?.bh ?? 450}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'bh', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Width <small>(mm)</small><em>optional</em></span>
                                      <input
                                        type="number"
                                        value={it.param?.bb ?? 250}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'bb', parseFloat(e.target.value) || 0)}
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
                                      <input
                                        type="number"
                                        value={it.param?.bh ?? 400}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'bh', parseFloat(e.target.value) || 0)}
                                        className="field param-value"
                                      />
                                    </label>
                                    <label className="param-field">
                                      <span>Width <small>(mm)</small><em>optional</em></span>
                                      <input
                                        type="number"
                                        value={it.param?.bb ?? 400}
                                        onChange={(e) => handleUpdateItemParam(it.id, 'bb', parseFloat(e.target.value) || 0)}
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
                                placeholder="1 atau {a:1,b:2,c:3} atau doFun(...)"
                              />
                            </div>

                            {/* 4. Evaluated Result */}
                            <div className="control result-control">
                              <label>Result</label>
                              <div className="qty-value factor-result">
                                {Number.isFinite(computedVal)
                                  ? `${computedVal.toFixed(3)} ${unitStr}`
                                  : 'Error'}
                              </div>
                            </div>
                          </div>
                        </div>
                      </article>
                    );
                  })}
                </div>
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
                  <details className="tree-node" open>
                    <summary>
                      <svg className="tree-chevron" viewBox="0 0 16 16" aria-hidden="true">
                        <path d="m5 6 3 3 3-3" />
                      </svg>
                      <b className="node-title">
                        Building: {descriptions[`building:${building.building}`]?.short || building.building || '—'}
                      </b>
                      <span className="node-meta">{entities.length} entities selected</span>
                    </summary>
                    <div className="branch">
                      <details className="tree-node" open>
                        <summary>
                          <svg className="tree-chevron" viewBox="0 0 16 16" aria-hidden="true">
                            <path d="m5 6 3 3 3-3" />
                          </svg>
                          <b className="node-title">
                            {descriptions[`floor:${building.floor}`]?.short || building.floor || '—'}
                          </b>
                          <span className="node-meta">{entities.length} entities selected</span>
                        </summary>
                        <div className="branch">
                          <details className="tree-node" open>
                            <summary>
                              <svg className="tree-chevron" viewBox="0 0 16 16" aria-hidden="true">
                                <path d="m5 6 3 3 3-3" />
                              </svg>
                              <b className="node-title">
                                {descriptions[`area:${building.area}`]?.short || building.area || '—'}
                              </b>
                              <span className="node-meta">{entities.length} entities selected</span>
                            </summary>
                            <div className="branch">
                              {items.map((it) => {
                                const computedVal = getItemComputedResult(it);
                                const unitStr = TYPE_UNITS[it.type] || '';
                                const descInfo = descriptions[it.code?.trim()];
                                const displayName = descInfo?.short || it.code;

                                return (
                                  <details key={it.id} className="tree-node type-node" open>
                                    <summary>
                                      <svg className="tree-chevron" viewBox="0 0 16 16" aria-hidden="true">
                                        <path d="m5 6 3 3 3-3" />
                                      </svg>
                                      <span className="tree-type-seq">
                                        <span className="type-icon">{TYPE_ICONS[it.type] || '◇'}</span>
                                        {it.key}
                                      </span>
                                      <span className="tree-type-name">
                                        <b>{it.type}</b>
                                      </span>
                                    </summary>
                                    <div className="branch">
                                      <div className="code-leaf tree-node">
                                        <span className="code-label">
                                          {it.code}
                                          {descInfo?.short && (
                                            <span className="subtle">({descInfo.short})</span>
                                          )}
                                        </span>
                                        <span className="value">
                                          {Number.isFinite(computedVal)
                                            ? `${computedVal.toFixed(3)} ${unitStr}`
                                            : 'Error'}
                                        </span>
                                      </div>
                                    </div>
                                  </details>
                                );
                              })}
                            </div>
                          </details>
                        </div>
                      </details>
                    </div>
                  </details>
                  <div className="tree-note">
                    Building, Floor, and Area show only their short description (or code when no short description is saved) and the selected entity count. Each Type may contain multiple Code rows; quantities appear on Code rows only.
                  </div>
                </div>
              </div>
            </section>

            {/* ====================================================================
                6. ACTION FOOTER
            ==================================================================== */}
            <div className="actions">
              <span className="note">
                ⓘ Apply XData dilewati jika grup Building atau Array Items dalam kondisi collapse. Kolom Update yang tidak dicentang tetap dipertahankan nilainya.
              </span>
              <div className="right">
                <button
                  type="button"
                  id="apply"
                  onClick={handleApplyXdata}
                  className="btn primary"
                  title="Terapkan metadata QTO ke entitas CAD (props.building & props.types)"
                >
                  ☁ Apply XData
                </button>
                <button
                  type="button"
                  id="save"
                  onClick={handleSaveSchedule}
                  className="btn orange"
                  title="Simpan jadwal QTO"
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
                    showToast('Semua level kolom sudah ditambahkan');
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
                    showToast('Minimal harus ada 1 sort level');
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
                title="Pindah level ke atas"
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
                title="Pindah level ke bawah"
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

                    <select
                      value={row.column}
                      onChange={(e) => {
                        const val = e.target.value;
                        if (draftSortRows.some((r, i) => i !== idx && r.column === val)) {
                          showToast('Kolom sort tidak boleh sama');
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
                  showToast('Level urutan tampilan berhasil diterapkan');
                }}
              >
                OK
              </button>
            </footer>
          </section>
        </div>
      )}

      {/* ====================================================================
          9. MANAGE DESCRIPTION MODAL (SQLite deskripsi)
      ==================================================================== */}
      {descModalOpen && (
        <DescriptionModal
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
                  showToast('JSON disalin ke clipboard');
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
