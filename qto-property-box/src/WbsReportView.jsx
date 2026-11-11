import React, { useState, useMemo, useCallback, useEffect } from 'react';
import { invoke } from '@tauri-apps/api/core';
import {
  FolderTree,
  ChevronDown,
  ChevronRight,
  Filter,
  Search,
  Check,
  RefreshCw,
  X,
  Layers,
  Database,
  Plus,
  Trash2,
  Edit2,
  Save,
  CheckSquare,
  Square,
  Tag,
  Info
} from 'lucide-react';
import DescriptionModal from './DescriptionModal';

// ============================================================================
// 1. DEFAULT SAVED DSL QUERIES (Gambar 2 & Gambar 3)
// ============================================================================
export const DEFAULT_SAVED_DSL_QUERIES = [
  {
    id: 'q-wall-floor',
    name: 'Dinding: Luas per Lantai (Floor & Wall Height) [m²]',
    dsl: "{e.data.props.floor : {all: {qty: += e.l * (e?.props?.types?.wall?.[0]?.height ?? 0), unit: 'm²'}}}"
  },
  {
    id: 'q-code-summary',
    name: 'By Code: Rekap Luas / Panjang / Jumlah [m², m, unit]',
    dsl: "{e.code : {qty: += e.a/1e6 || += e.l/1e3 || ++, unit: e.a ? 'm²' : (e.l ? 'm' : 'unit')}}"
  },
  {
    id: 'q-floor-code',
    name: 'By Floor & Code: Panjang per Lantai [m]',
    dsl: "{e.data.props.floor : {e.code : {qty: += e.l, unit: 'm'}}}"
  },
  {
    id: 'q-layer-length',
    name: 'By Layer: Total Panjang per Layer [m]',
    dsl: "{e.layer : {all: {qty: += e.l, unit: 'm'}}}"
  }
];

// Offline simulation data generator if CAD is not running
function generateDemoDslResult(dsl) {
  if (dsl.includes('floor') && dsl.includes('wall')) {
    return {
      'Lantai Basement 1': {
        'Area Parkir Utama': {
          'Galian Tanah Struktur': { qty: 1250, unit: 'm³', handles: ['50A1', '50A2'] },
          'Pengecoran Beton Slab': { qty: 480, unit: 'm³', handles: ['50A3'] },
          'Floor Hardener Metallic': { qty: 1600, unit: 'm²', handles: ['50A4', '50A5'] }
        }
      },
      'Lantai Dasar (Ground Floor)': {
        'Lobby Utama & Reception': {
          'Pasangan Homogeneous Tile': { code: 'HT1', qty: 340, unit: 'm²', handles: ['50B1'] },
          'Plafon Gypsum Board': { code: 'CLG1', qty: 310, unit: 'm²', handles: ['50B2'] },
          'Dinding Glass Partisi': { code: 'GLS1', qty: 85, unit: 'm²', handles: ['50B3'] }
        },
        'Area Koridor & Toilet Public': {
          'Pasangan Keramik Dinding': { code: 'FloorFin/CT1', qty: 140, unit: 'm²', handles: ['50C1'] },
          'Pekerjaan Sanitair Closet': { code: 'SAN1', qty: 12, unit: 'unit', handles: ['50C2'] }
        }
      },
      'Lantai 2 (Office Space)': {
        'Ruang Kerja Open Plan A': {
          'Pemasangan Karpet Tile': { code: 'CPT1', qty: 520, unit: 'm²', handles: ['50D1'] },
          'Pengecatan Dinding Emulsi': { code: 'PNT1', qty: 410, unit: 'm²', handles: ['50D2'] }
        }
      }
    };
  }
  return {
    'FloorFin/CT1': { qty: 140, unit: 'm²', handles: ['50C1'] },
    'CPT1': { qty: 520, unit: 'm²', handles: ['50D1'] },
    'SAN1': { qty: 12, unit: 'unit', handles: ['50C2'] }
  };
}

// Convert any nested CAD SS-QUERY result object into WBS Tree Hierarchy
export function convertQueryResultToWbsNodes(obj, prefix = 'cad-node', descriptions = {}) {
  if (!obj || typeof obj !== 'object') return [];

  return Object.entries(obj).map(([key, val], idx) => {
    const id = `${prefix}-${idx}`;

    // Leaf item with quantity and unit
    if (val && typeof val === 'object' && (val.qty !== undefined || val.unit !== undefined)) {
      const explicitCode = val.code || (key !== 'all' ? key : '');
      const descInfo = descriptions[explicitCode] || descriptions[key];
      const displayName = descInfo?.short ? descInfo.short : (key === 'all' ? 'Total Kuantitas' : key);

      return {
        id,
        name: displayName,
        rawKey: key,
        code: explicitCode,
        qty: typeof val.qty === 'number' ? val.qty : parseFloat(val.qty) || 0,
        unit: val.unit ? String(val.unit).replace(/^['"]|['"]$/g, '') : '',
        handles: Array.isArray(val.handles) ? val.handles : [],
        descInfo: descInfo || null
      };
    }

    // Branch node with children
    if (val && typeof val === 'object') {
      const children = convertQueryResultToWbsNodes(val, id, descriptions);
      return {
        id,
        name: key === 'undefined' ? 'Unassigned / Luar Lantai' : key,
        rawKey: key,
        children
      };
    }

    return {
      id,
      name: key,
      rawKey: key,
      qty: val,
      unit: '',
      handles: []
    };
  });
}

// Collect all node IDs recursively for expand/collapse
function getAllNodeIds(nodes) {
  let ids = [];
  for (const node of nodes) {
    ids.push(node.id);
    if (node.children && node.children.length > 0) {
      ids = ids.concat(getAllNodeIds(node.children));
    }
  }
  return ids;
}

// Collect all items that have quantities (leaf items with qty)
function getQuantityNodes(nodes) {
  let items = [];
  for (const node of nodes) {
    if (node.qty !== undefined && node.qty !== null && !node.children?.length) {
      items.push(node);
    }
    if (node.children && node.children.length > 0) {
      items = items.concat(getQuantityNodes(node.children));
    }
  }
  return items;
}

// Format numbers with thousands separator
function formatQty(val) {
  if (val === undefined || val === null || val === '') return '-';
  const num = Number(val);
  if (isNaN(num)) return String(val);
  return num.toLocaleString('en-US', {
    maximumFractionDigits: 2,
    minimumFractionDigits: Number.isInteger(num) ? 0 : 1
  });
}

export default function WbsReportView({
  cadConnected,
  cadPlatform,
  cadDwgName,
  ipcPort,
  selectedEntities = [],
  descriptions = {},
  onSaveDescription,
  onDeleteDescription
}) {
  // 1. Saved DSL Queries List (CRUD)
  const [savedQueries, setSavedQueries] = useState(() => {
    try {
      const saved = localStorage.getItem('grx_saved_dsl_queries');
      return saved ? JSON.parse(saved) : DEFAULT_SAVED_DSL_QUERIES;
    } catch {
      return DEFAULT_SAVED_DSL_QUERIES;
    }
  });

  // Active Query ID & Query Text
  const [activeQueryId, setActiveQueryId] = useState(() => {
    return DEFAULT_SAVED_DSL_QUERIES[0].id;
  });

  // 2. Pill Option Button: 'all' vs 'selection'
  const [queryScope, setQueryScope] = useState('all'); // 'all' | 'selection'

  // 3. TreeView Data (100% generated from DSL Query)
  const [wbsData, setWbsData] = useState([]);
  const [expandedNodes, setExpandedNodes] = useState(new Set());
  const [searchQuery, setSearchQuery] = useState('');
  const [projectTitle, setProjectTitle] = useState('Pembangunan Gedung Perkantoran & Komersial');
  const [statusMsg, setStatusMsg] = useState('');
  const [isLoading, setIsLoading] = useState(false);

  // 4. Visual Filter Mode & Checkboxes (Requirement 3)
  const [filterModeActive, setFilterModeActive] = useState(false);
  const [checkedNodeIds, setCheckedNodeIds] = useState(new Set());

  // 5. Modals State
  const [showCrudModal, setShowCrudModal] = useState(false);
  const [editingQueryId, setEditingQueryId] = useState(null);
  const [queryFormName, setQueryFormName] = useState('');
  const [queryFormDsl, setQueryFormDsl] = useState('');

  // Description Modal
  const [descModalOpen, setDescModalOpen] = useState(false);
  const [descModalCode, setDescModalCode] = useState('');

  // Save queries to localStorage
  const persistQueries = (queries) => {
    setSavedQueries(queries);
    try {
      localStorage.setItem('grx_saved_dsl_queries', JSON.stringify(queries));
    } catch (e) {
      console.warn('Failed to save queries to localStorage:', e);
    }
  };

  // Active DSL Query String
  const currentDslQuery = useMemo(() => {
    const q = savedQueries.find((item) => item.id === activeQueryId);
    return q ? q.dsl : DEFAULT_SAVED_DSL_QUERIES[0].dsl;
  }, [savedQueries, activeQueryId]);

  // Execute SS-QUERY in CAD and build TreeView from result (Manual / On-Demand ONLY)
  const executeQueryAndBuildTree = useCallback(
    async (dslOverride, scopeOverride) => {
      const dsl = (dslOverride || currentDslQuery).trim();
      const scope = scopeOverride || queryScope;
      if (!dsl) return;

      setIsLoading(true);
      setStatusMsg('Mengevaluasi SS-QUERY di CAD...');

      // Handles for selection scope: fetched ON-DEMAND when user clicks "Pick Selection"
      let targetHandles = null;
      if (scope === 'selection') {
        if (cadConnected) {
          try {
            const pollRes = await invoke('cad_poll_selection', {
              lastVersion: 0,
              port: ipcPort
            });
            if (pollRes && pollRes.handles && pollRes.handles.length > 0) {
              targetHandles = pollRes.handles;
            }
          } catch (e) {
            console.warn('Failed to poll selection on demand:', e);
          }
        }
        if (!targetHandles || targetHandles.length === 0) {
          targetHandles = selectedEntities.map((e) => e.handle).filter(Boolean);
        }
        if (!targetHandles || targetHandles.length === 0) {
          alert('Belum ada objek yang dipilih di CAD. Silakan pilih objek di AutoCAD / GstarCAD, lalu klik tombol "Pick Selection".');
          setStatusMsg('0 objek dipilih di CAD');
          setIsLoading(false);
          setTimeout(() => setStatusMsg(''), 3000);
          return;
        }
      }

      if (cadConnected) {
        try {
          // Automatically inject handles: push(e.handle) if not present to support visual filter
          const dslWithHandles = dsl.includes('handles:')
            ? dsl
            : dsl.replace(/(unit\s*:\s*['"][^'"]*['"])/g, '$1, handles: push(e.handle)');

          let res;
          try {
            res = await invoke('cad_run_ss_query', {
              query: dslWithHandles,
              handles: targetHandles,
              port: ipcPort
            });
            if (res && res.error) {
              res = await invoke('cad_run_ss_query', {
                query: dsl,
                handles: targetHandles,
                port: ipcPort
              });
            }
          } catch {
            res = await invoke('cad_run_ss_query', {
              query: dsl,
              handles: targetHandles,
              port: ipcPort
            });
          }

          if (res && typeof res === 'object') {
            if (res.error) {
              alert(`CAD SS-QUERY Error:\n${res.error}`);
              setStatusMsg(`Error: ${res.error}`);
              setIsLoading(false);
              return;
            }

            const nodes = convertQueryResultToWbsNodes(res, 'wbs-cad', descriptions);
            setWbsData(nodes);
            const allIds = getAllNodeIds(nodes);
            setExpandedNodes(new Set(allIds));
            const qNodes = getQuantityNodes(nodes);
            setCheckedNodeIds(new Set(qNodes.map((n) => n.id)));
            const scopeLabel = scope === 'selection' ? `Seleksi (${targetHandles?.length || 0} obj)` : 'Seluruh Gambar';
            setStatusMsg(`Sukses: ${scopeLabel} • ${qNodes.length} item kuantitas`);
          } else {
            setStatusMsg('Hasil query kosong');
          }
        } catch (err) {
          alert(`Gagal menjalankan SS-QUERY di CAD:\n${String(err)}`);
          setStatusMsg(`Query gagal: ${String(err)}`);
        }
      } else {
        // Offline simulation mode
        const demoRes = generateDemoDslResult(dsl);
        const nodes = convertQueryResultToWbsNodes(demoRes, 'wbs-demo', descriptions);
        setWbsData(nodes);
        const allIds = getAllNodeIds(nodes);
        setExpandedNodes(new Set(allIds));
        const qNodes = getQuantityNodes(nodes);
        setCheckedNodeIds(new Set(qNodes.map((n) => n.id)));
        setStatusMsg('Tree dimuat dari simulasi offline');
      }

      setIsLoading(false);
      setTimeout(() => setStatusMsg(''), 3000);
    },
    [currentDslQuery, queryScope, cadConnected, ipcPort, descriptions]
  );

  // Manual trigger for Pick Selection button
  const handlePickSelection = async () => {
    setQueryScope('selection');
    await executeQueryAndBuildTree(currentDslQuery, 'selection');
  };

  // Manual trigger for All Entities button
  const handleQueryAll = async () => {
    setQueryScope('all');
    await executeQueryAndBuildTree(currentDslQuery, 'all');
  };

  // Run query once on mount or when activeQueryId changes (NEVER on realtime selection!)
  useEffect(() => {
    executeQueryAndBuildTree(currentDslQuery, queryScope);
  }, [activeQueryId]);

  // CAD Visual Filter: Dark Grey (ACI 8) for unchecked entities, restore original _clr when OFF
  useEffect(() => {
    if (!cadConnected) return;

    const qNodes = getQuantityNodes(wbsData);
    const allHandlesSet = new Set();
    const checkedHandlesSet = new Set();

    for (const node of qNodes) {
      if (node.handles && node.handles.length > 0) {
        for (const h of node.handles) {
          allHandlesSet.add(h);
          if (checkedNodeIds.has(node.id)) {
            checkedHandlesSet.add(h);
          }
        }
      }
    }

    const allHandles = Array.from(allHandlesSet);
    const checkedHandles = Array.from(checkedHandlesSet);

    if (allHandles.length === 0) return;

    let cancel = false;
    const timer = setTimeout(async () => {
      try {
        await invoke('cad_filter_entities_color', {
          checkedHandles,
          allHandles,
          isFilterOn: filterModeActive,
          port: ipcPort
        });
        if (!cancel) {
          if (filterModeActive) {
            setStatusMsg(`Filter: ${checkedHandles.length} normal, ${allHandles.length - checkedHandles.length} dark grey`);
          } else {
            setStatusMsg('Warna entitas CAD dipulihkan');
          }
          setTimeout(() => setStatusMsg(''), 2000);
        }
      } catch (err) {
        if (!cancel) console.warn('Color filter sync error:', err);
      }
    }, 60);

    return () => {
      cancel = true;
      clearTimeout(timer);
    };
  }, [filterModeActive, checkedNodeIds, wbsData, cadConnected, ipcPort]);

  // Checkbox toggle
  const handleToggleCheck = (id) => {
    setCheckedNodeIds((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  const handleExpandAll = () => setExpandedNodes(new Set(getAllNodeIds(wbsData)));
  const handleCollapseAll = () => setExpandedNodes(new Set());

  const toggleNode = (id) => {
    setExpandedNodes((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  };

  // Open Description Modal for a code
  const handleOpenDescription = (code) => {
    if (!code) return;
    setDescModalCode(code);
    setDescModalOpen(true);
  };

  // CRUD Modal Handlers
  const handleOpenCrudModal = () => {
    const active = savedQueries.find((q) => q.id === activeQueryId) || savedQueries[0];
    if (active) {
      setEditingQueryId(active.id);
      setQueryFormName(active.name);
      setQueryFormDsl(active.dsl);
    } else {
      setEditingQueryId(null);
      setQueryFormName('');
      setQueryFormDsl('');
    }
    setShowCrudModal(true);
  };

  const handleSelectQueryToEdit = (query) => {
    setEditingQueryId(query.id);
    setQueryFormName(query.name);
    setQueryFormDsl(query.dsl);
  };

  const handleAddNewQueryForm = () => {
    setEditingQueryId(null);
    setQueryFormName('Query DSL Baru');
    setQueryFormDsl("{e.data.props.floor : {all: {qty: += e.l, unit: 'm'}}}");
  };

  const handleSaveQueryForm = () => {
    if (!queryFormName.trim()) {
      alert('Nama query tidak boleh kosong.');
      return;
    }
    if (!queryFormDsl.trim()) {
      alert('Ekspresi DSL query tidak boleh kosong.');
      return;
    }

    let updated;
    if (editingQueryId) {
      // Edit existing
      updated = savedQueries.map((q) =>
        q.id === editingQueryId ? { ...q, name: queryFormName.trim(), dsl: queryFormDsl.trim() } : q
      );
    } else {
      // Create new
      const newQuery = {
        id: `q-custom-${Date.now()}`,
        name: queryFormName.trim(),
        dsl: queryFormDsl.trim()
      };
      updated = [...savedQueries, newQuery];
      setEditingQueryId(newQuery.id);
      setActiveQueryId(newQuery.id);
    }

    persistQueries(updated);
    setStatusMsg('Query berhasil disimpan!');
    setTimeout(() => setStatusMsg(''), 2000);
  };

  const handleDeleteQueryForm = (idToDelete) => {
    if (savedQueries.length <= 1) {
      alert('Minimal harus ada 1 query tersimpan.');
      return;
    }
    if (!window.confirm('Hapus query DSL ini?')) return;

    const updated = savedQueries.filter((q) => q.id !== idToDelete);
    persistQueries(updated);
    if (activeQueryId === idToDelete) {
      setActiveQueryId(updated[0].id);
    }
    if (editingQueryId === idToDelete) {
      setEditingQueryId(updated[0].id);
      setQueryFormName(updated[0].name);
      setQueryFormDsl(updated[0].dsl);
    }
  };

  const handleApplyAndExecuteFromModal = () => {
    handleSaveQueryForm();
    setShowCrudModal(false);
    if (editingQueryId) {
      setActiveQueryId(editingQueryId);
    }
    executeQueryAndBuildTree(queryFormDsl.trim());
  };

  // Filter nodes by search query
  const filteredNodes = useMemo(() => {
    if (!searchQuery.trim()) return wbsData;
    const q = searchQuery.toLowerCase();

    function filterRecursive(nodes) {
      const result = [];
      for (const node of nodes) {
        const matchesSelf =
          node.name?.toLowerCase().includes(q) ||
          node.code?.toLowerCase().includes(q) ||
          node.unit?.toLowerCase().includes(q) ||
          node.descInfo?.short?.toLowerCase().includes(q);

        let matchingChildren = [];
        if (node.children && node.children.length > 0) {
          matchingChildren = filterRecursive(node.children);
        }

        if (matchesSelf || matchingChildren.length > 0) {
          result.push({
            ...node,
            children: matchingChildren.length > 0 ? matchingChildren : node.children
          });
        }
      }
      return result;
    }

    return filterRecursive(wbsData);
  }, [wbsData, searchQuery]);

  // Render recursive Tree Node
  const renderTreeNode = (node, depth = 0) => {
    const hasChildren = node.children && node.children.length > 0;
    const isExpanded = expandedNodes.has(node.id);
    const isChecked = checkedNodeIds.has(node.id);
    const hasQty = node.qty !== undefined && node.qty !== null && !hasChildren;
    const paddingLeft = `${depth * 20 + 16}px`;

    // Check if code has description in descriptions map
    const codeKey = node.code || node.rawKey;
    const descData = descriptions[codeKey] || node.descInfo;

    return (
      <React.Fragment key={node.id}>
        <div
          className={`group flex items-center justify-between py-2 border-b border-[#1b2230] transition-colors ${
            hasChildren
              ? 'bg-[#151a24]/80 hover:bg-[#1a2130]'
              : 'hover:bg-[#161c28]'
          }`}
          style={{ paddingLeft }}
        >
          {/* Left Column: Chevron + Checkbox + Name + Code + Description */}
          <div className="flex items-center gap-2.5 min-w-0 flex-1 pr-4">
            {hasChildren ? (
              <button
                type="button"
                onClick={() => toggleNode(node.id)}
                className="w-4 h-4 flex items-center justify-center text-slate-400 hover:text-white transition-colors shrink-0"
              >
                {isExpanded ? (
                  <ChevronDown className="w-4 h-4" />
                ) : (
                  <ChevronRight className="w-4 h-4" />
                )}
              </button>
            ) : (
              <span className="w-4 flex items-center justify-center text-slate-500 font-bold text-base">
                •
              </span>
            )}

            {/* Checkbox (Requirement 3: Checkbox hanya yang punya kuantitas saat Filter button ON) */}
            {filterModeActive && hasQty && (
              <input
                type="checkbox"
                checked={isChecked}
                onChange={() => handleToggleCheck(node.id)}
                title={isChecked ? 'Checked: Normal Color' : 'Unchecked: Dark Grey in CAD'}
                className="w-4 h-4 rounded bg-[#10141d] border-slate-600 text-sky-500 focus:ring-0 cursor-pointer shrink-0 accent-sky-500"
              />
            )}

            <div className="min-w-0 flex-1">
              <div className="flex items-center gap-2">
                <span
                  className={`truncate ${
                    hasChildren
                      ? 'font-bold text-slate-100 text-[13px]'
                      : isChecked || !filterModeActive
                      ? 'font-medium text-slate-200 text-xs'
                      : 'text-slate-500 line-through text-xs'
                  }`}
                >
                  {descData?.short || node.name}
                </span>

                {/* Code Badge */}
                {codeKey && (
                  <button
                    type="button"
                    onClick={() => handleOpenDescription(codeKey)}
                    title={`Klik untuk lihat / edit deskripsi kode ${codeKey} (SQLite)`}
                    className="px-1.5 py-0.2 text-[10px] font-mono bg-sky-950/60 hover:bg-sky-900/80 text-sky-400 hover:text-sky-300 border border-sky-800/40 rounded shrink-0 flex items-center gap-1 transition-colors"
                  >
                    <span>{codeKey}</span>
                    <span className="text-[9px] opacity-75">📝</span>
                  </button>
                )}
              </div>

              {/* Description Breadcrumb Items */}
              {descData?.items && descData.items.length > 0 && (
                <div className="text-[10px] text-slate-400 flex items-center gap-1 mt-0.5 truncate font-sans">
                  {descData.items.map((it, i) => (
                    <React.Fragment key={i}>
                      <span>{it}</span>
                      {i < descData.items.length - 1 && <span className="text-slate-600">›</span>}
                    </React.Fragment>
                  ))}
                </div>
              )}
            </div>
          </div>

          {/* Right Columns: KUANTITAS & SATUAN */}
          <div className="flex items-center gap-4 shrink-0 pr-6">
            {/* Quantity */}
            <div className="w-24 text-right">
              {hasQty ? (
                <span className="font-mono font-bold text-slate-100 text-xs">
                  {formatQty(node.qty)}
                </span>
              ) : (
                <span className="text-slate-600 text-xs">-</span>
              )}
            </div>

            {/* Unit */}
            <div className="w-16 flex justify-start">
              {hasQty && node.unit ? (
                <span className="px-2 py-0.5 rounded text-[11px] font-mono font-bold bg-[#1e2533] text-sky-400 border border-slate-700/80 shadow-sm">
                  {node.unit}
                </span>
              ) : (
                <span className="text-slate-600 text-xs">-</span>
              )}
            </div>
          </div>
        </div>

        {/* Children */}
        {hasChildren && isExpanded && (
          <div>
            {node.children.map((child) => renderTreeNode(child, depth + 1))}
          </div>
        )}
      </React.Fragment>
    );
  };

  return (
    <div className="flex-1 flex flex-col bg-[#111620] text-slate-200 min-h-0 overflow-hidden font-sans select-none">
      {/* ====================================================================
          1. HEADER BAR
      ==================================================================== */}
      <header className="bg-[#151a24] border-b border-[#1f283b] px-4 py-3 flex items-center justify-between gap-3 shrink-0">
        <div className="flex items-center gap-3 min-w-0">
          <div className="w-9 h-9 rounded-lg bg-sky-500/15 border border-sky-500/40 flex items-center justify-center text-sky-400 shadow-sm shrink-0">
            <FolderTree className="w-5 h-5" />
          </div>
          <div className="min-w-0">
            <h1 className="text-sm font-bold text-white tracking-wide flex items-center gap-2">
              <span>WBS QTO Tree View</span>
              {statusMsg && (
                <span className="text-[11px] font-normal text-amber-300 font-mono animate-pulse">
                  • {statusMsg}
                </span>
              )}
            </h1>
            <input
              type="text"
              value={projectTitle}
              onChange={(e) => setProjectTitle(e.target.value)}
              title="Edit Project / Drawing Title"
              className="text-[11px] text-slate-400 bg-transparent hover:bg-[#1a2130] focus:bg-[#1a2130] px-1 py-0.5 rounded border border-transparent focus:border-slate-700 outline-none w-72 truncate transition-colors"
            />
          </div>
        </div>

        {/* Top-Right Buttons */}
        <div className="flex items-center gap-2 shrink-0">
          <button
            onClick={handleOpenCrudModal}
            title="Kelola Query DSL (Create, Edit, Delete, Save)"
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-md bg-white text-[#10141d] hover:bg-slate-100 font-semibold text-xs transition-colors shadow-sm"
          >
            <Database className="w-3.5 h-3.5 text-sky-600" />
            <span>Kelola Query DSL</span>
          </button>

          <button
            onClick={() => executeQueryAndBuildTree()}
            title="Refresh SS-QUERY in AutoCAD / GstarCAD"
            className="p-1.5 rounded-md bg-[#1a2130] hover:bg-[#222b3d] border border-slate-700 text-slate-300 hover:text-sky-300 transition-colors"
          >
            <RefreshCw className={`w-3.5 h-3.5 ${isLoading ? 'animate-spin text-sky-400' : ''}`} />
          </button>
        </div>
      </header>

      {/* ====================================================================
          2. TOOLBAR: Gambar 2 Dropdown (Only DSL Queries) + Pill (Selection/All) + Filter Button
      ==================================================================== */}
      <div className="bg-[#131822] border-b border-[#1c2333] px-4 py-2.5 flex flex-wrap items-center justify-between gap-2.5 shrink-0">
        <div className="flex items-center gap-2.5 flex-1 min-w-[320px]">
          {/* Gambar 2: Dropdown ONLY DSL Queries */}
          <div className="relative">
            <select
              value={activeQueryId}
              onChange={(e) => setActiveQueryId(e.target.value)}
              className="appearance-none bg-[#19202c] border border-slate-700/80 hover:border-slate-600 text-slate-200 text-xs rounded-md pl-7 pr-8 py-1.5 outline-none focus:border-sky-500 transition-colors cursor-pointer max-w-xs truncate"
            >
              {savedQueries.map((q) => (
                <option key={q.id} value={q.id}>
                  {q.name}
                </option>
              ))}
            </select>
            <Filter className="w-3.5 h-3.5 text-sky-400 absolute left-2 top-2 pointer-events-none" />
            <ChevronDown className="w-3.5 h-3.5 text-slate-400 absolute right-2 top-2 pointer-events-none" />
          </div>

          {/* User Request: Explicit Button Pick Selection or All (No realtime freeze) */}
          <div className="flex items-center gap-1.5 shrink-0">
            <button
              type="button"
              onClick={handlePickSelection}
              disabled={isLoading}
              title="Klik untuk mengambil objek yang sedang dipilih di CAD dan menjalankan query DSL"
              className={`px-3 py-1.5 rounded-md text-xs font-semibold flex items-center gap-1.5 border transition-all ${
                queryScope === 'selection'
                  ? 'bg-amber-500/25 text-amber-300 border-amber-500/70 shadow-sm shadow-amber-500/20'
                  : 'bg-[#19202c] hover:bg-[#202938] border-slate-700 text-slate-300'
              }`}
            >
              <span>🎯</span>
              <span>Pick Selection</span>
            </button>

            <button
              type="button"
              onClick={handleQueryAll}
              disabled={isLoading}
              title="Klik untuk menjalankan query DSL pada seluruh gambar CAD"
              className={`px-3 py-1.5 rounded-md text-xs font-semibold flex items-center gap-1.5 border transition-all ${
                queryScope === 'all'
                  ? 'bg-sky-500/25 text-sky-300 border-sky-500/70 shadow-sm shadow-sky-500/20'
                  : 'bg-[#19202c] hover:bg-[#202938] border-slate-700 text-slate-300'
              }`}
            >
              <span>🌐</span>
              <span>All</span>
            </button>
          </div>

          {/* Search Box */}
          <div className="relative flex-1 max-w-xs">
            <Search className="w-3.5 h-3.5 text-slate-400 absolute left-2.5 top-2" />
            <input
              type="text"
              placeholder="Cari item pekerjaan / kode..."
              value={searchQuery}
              onChange={(e) => setSearchQuery(e.target.value)}
              className="w-full bg-[#19202c] border border-slate-700/80 focus:border-sky-500 rounded-md pl-8 pr-7 py-1.5 text-xs text-slate-200 placeholder-slate-500 outline-none transition-colors"
            />
            {searchQuery && (
              <button
                onClick={() => setSearchQuery('')}
                className="absolute right-2 top-2 text-slate-400 hover:text-white"
              >
                <X className="w-3.5 h-3.5" />
              </button>
            )}
          </div>
        </div>

        {/* Right: Expand/Collapse & Filter Button */}
        <div className="flex items-center gap-2 shrink-0">
          <button
            onClick={handleExpandAll}
            className="flex items-center gap-1 px-2.5 py-1 rounded bg-[#19202c] hover:bg-[#202938] border border-slate-700/80 text-slate-300 text-xs transition-colors"
          >
            <ChevronDown className="w-3 h-3 text-slate-400" />
            <span>Expand All</span>
          </button>

          <button
            onClick={handleCollapseAll}
            className="flex items-center gap-1 px-2.5 py-1 rounded bg-[#19202c] hover:bg-[#202938] border border-slate-700/80 text-slate-300 text-xs transition-colors"
          >
            <ChevronRight className="w-3 h-3 text-slate-400" />
            <span>Collapse All</span>
          </button>

          {/* Requirement 3: BUTTON FILTER (Visual entity color filter) */}
          <button
            onClick={() => setFilterModeActive((prev) => !prev)}
            title={
              filterModeActive
                ? 'Filter Aktif: Unchecked entities diberi warna Dark Grey (Klik untuk OFF & restore warna)'
                : 'Klik untuk aktifkan Filter Seleksi & Checkbox (Unchecked diberi warna Dark Grey)'
            }
            className={`flex items-center gap-1.5 px-3 py-1 rounded-md text-xs font-semibold border transition-all ${
              filterModeActive
                ? 'bg-amber-500/20 text-amber-300 border-amber-500/60 shadow-sm shadow-amber-500/20'
                : 'bg-[#19202c] hover:bg-[#202938] border-slate-700 text-slate-300'
            }`}
          >
            <Filter className="w-3.5 h-3.5" />
            <span>{filterModeActive ? 'Filter Visual: ON' : 'Filter Visual'}</span>
          </button>
        </div>
      </div>

      {/* ====================================================================
          3. TREEVIEW TABLE HEADER
      ==================================================================== */}
      <div className="bg-[#151a24] border-b border-[#21293a] px-4 py-2 flex items-center justify-between text-[11px] font-bold text-slate-400 uppercase tracking-wider shrink-0">
        <div className="flex-1">PEKERJAAN (HASIL QUERY DSL)</div>
        <div className="flex items-center gap-4 pr-6">
          <div className="w-24 text-right">KUANTITAS</div>
          <div className="w-16 text-left">SATUAN</div>
        </div>
      </div>

      {/* ====================================================================
          4. TREEVIEW BODY
      ==================================================================== */}
      <div className="flex-1 overflow-y-auto min-h-0 divide-y divide-[#181f2c]">
        {filteredNodes.length > 0 ? (
          filteredNodes.map((node) => renderTreeNode(node, 0))
        ) : (
          <div className="p-8 text-center text-slate-500 text-xs">
            {isLoading
              ? 'Mengevaluasi SS-QUERY di CAD...'
              : 'Tidak ada hasil query. Pastikan objek CAD terpilih atau sesuaikan ekspresi query DSL.'}
          </div>
        )}
      </div>

      {/* ====================================================================
          5. MODAL: GAMBAR 3 CRUD MANAGER QUERY DSL
      ==================================================================== */}
      {showCrudModal && (
        <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-sm flex items-center justify-center p-4">
          <div className="bg-[#151a24] border border-slate-700 rounded-lg w-full max-w-2xl max-h-[85vh] flex flex-col shadow-2xl">
            {/* Header */}
            <div className="px-4 py-3 border-b border-slate-700 flex items-center justify-between">
              <div className="flex items-center gap-2">
                <Database className="w-4 h-4 text-sky-400" />
                <h3 className="text-sm font-bold text-white tracking-wide">
                  Kelola Query DSL (SS-QUERY)
                </h3>
              </div>
              <button
                onClick={() => setShowCrudModal(false)}
                className="p-1 rounded text-slate-400 hover:text-white transition-colors"
              >
                <X className="w-4 h-4" />
              </button>
            </div>

            {/* Body: Two columns (List on left, Editor on right) */}
            <div className="p-4 flex-1 overflow-hidden flex gap-4">
              {/* Left Column: Saved Queries List */}
              <div className="w-56 flex flex-col border-r border-slate-700/80 pr-3">
                <div className="flex items-center justify-between mb-2">
                  <span className="text-[11px] font-bold text-slate-400 uppercase">Daftar Query</span>
                  <button
                    type="button"
                    onClick={handleAddNewQueryForm}
                    className="p-1 rounded bg-sky-500/20 hover:bg-sky-500/30 text-sky-300 text-[10px] flex items-center gap-1 font-semibold"
                  >
                    <Plus className="w-3 h-3" />
                    <span>Tambah</span>
                  </button>
                </div>
                <div className="flex-1 overflow-y-auto space-y-1">
                  {savedQueries.map((q) => (
                    <div
                      key={q.id}
                      onClick={() => handleSelectQueryToEdit(q)}
                      className={`p-2 rounded text-xs cursor-pointer border transition-colors ${
                        editingQueryId === q.id
                          ? 'bg-sky-950/60 border-sky-500/80 text-sky-200'
                          : 'bg-[#10141d] border-transparent hover:border-slate-700 text-slate-300'
                      }`}
                    >
                      <div className="font-semibold truncate">{q.name}</div>
                      <div className="text-[10px] text-slate-400 font-mono truncate mt-0.5 opacity-75">
                        {q.dsl}
                      </div>
                    </div>
                  ))}
                </div>
              </div>

              {/* Right Column: Query Editor */}
              <div className="flex-1 flex flex-col space-y-3">
                <div>
                  <label className="block text-xs font-medium text-slate-300 mb-1">
                    Nama / Judul Query:
                  </label>
                  <input
                    type="text"
                    value={queryFormName}
                    onChange={(e) => setQueryFormName(e.target.value)}
                    placeholder="Contoh: Dinding Luas per Lantai"
                    className="w-full bg-[#0d1117] border border-slate-700 px-3 py-1.5 rounded text-xs text-slate-200 focus:outline-none focus:border-sky-500"
                  />
                </div>

                <div className="flex-1 flex flex-col min-h-0">
                  <div className="flex items-center justify-between mb-1">
                    <label className="text-xs font-medium text-slate-300">
                      Query Value DSL:
                    </label>
                    <div className="flex items-center gap-1">
                      <button
                        type="button"
                        onClick={() =>
                          setQueryFormDsl(
                            "{e.data.props.floor : {all: {qty: += e.l * (e?.props?.types?.wall?.[0]?.height ?? 0), unit: 'm²'}}}"
                          )
                        }
                        className="text-[10px] font-mono text-sky-400 hover:text-sky-300"
                      >
                        [Template Wall]
                      </button>
                      <button
                        type="button"
                        onClick={() =>
                          setQueryFormDsl(
                            "{e.code : {qty: += e.a/1e6 || += e.l/1e3 || ++, unit: e.a ? 'm²' : (e.l ? 'm' : 'unit')}}"
                          )
                        }
                        className="text-[10px] font-mono text-sky-400 hover:text-sky-300"
                      >
                        [Template Code]
                      </button>
                    </div>
                  </div>
                  <textarea
                    value={queryFormDsl}
                    onChange={(e) => setQueryFormDsl(e.target.value)}
                    placeholder="{e.data.props.floor : {all: {qty: += e.l * (e?.props?.types?.wall?.[0]?.height ?? 0), unit: 'm²'}}}"
                    rows={8}
                    className="w-full flex-1 p-2.5 bg-[#0d1117] border border-slate-700 font-mono text-xs text-sky-300 rounded focus:outline-none focus:border-sky-500 resize-none leading-relaxed"
                    spellCheck={false}
                  />
                </div>
              </div>
            </div>

            {/* Footer */}
            <div className="px-4 py-3 border-t border-slate-700 flex items-center justify-between bg-[#12161f]">
              {editingQueryId && (
                <button
                  type="button"
                  onClick={() => handleDeleteQueryForm(editingQueryId)}
                  className="flex items-center gap-1 px-3 py-1.5 rounded bg-rose-950/40 hover:bg-rose-900/60 text-rose-300 border border-rose-800/40 text-xs transition-colors"
                >
                  <Trash2 className="w-3.5 h-3.5" />
                  <span>Hapus</span>
                </button>
              )}

              <div className="flex items-center gap-2 ml-auto">
                <button
                  type="button"
                  onClick={() => setShowCrudModal(false)}
                  className="px-3 py-1.5 rounded bg-[#1c2333] hover:bg-[#252f44] text-xs text-slate-300 transition-colors"
                >
                  Batal
                </button>
                <button
                  type="button"
                  onClick={handleSaveQueryForm}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-[#1e2533] hover:bg-[#293245] border border-slate-600 text-white font-medium text-xs transition-colors"
                >
                  <Save className="w-3.5 h-3.5" />
                  <span>Simpan</span>
                </button>
                <button
                  type="button"
                  onClick={handleApplyAndExecuteFromModal}
                  className="flex items-center gap-1.5 px-4 py-1.5 rounded bg-sky-500 hover:bg-sky-600 text-white font-semibold text-xs shadow transition-colors"
                >
                  <Check className="w-3.5 h-3.5" />
                  <span>Jalankan di CAD (Terapkan)</span>
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* ====================================================================
          6. MODAL: GAMBAR 5 KELOLA DESKRIPSI (SQLite)
      ==================================================================== */}
      <DescriptionModal
        isOpen={descModalOpen}
        onClose={() => setDescModalOpen(false)}
        initialCode={descModalCode}
        descriptions={descriptions}
        onSaveDescription={onSaveDescription}
        onDeleteDescription={onDeleteDescription}
        cadConnected={cadConnected}
      />
    </div>
  );
}
