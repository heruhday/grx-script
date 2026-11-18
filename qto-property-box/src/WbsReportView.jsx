import React, { useState, useMemo, useCallback, useEffect, useRef } from 'react';
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
// 1. DEFAULT SAVED DSL QUERIES
// ============================================================================
export const DEFAULT_SAVED_DSL_QUERIES = [
  {
    id: 'q-wall-floor',
    name: 'Wall Area by Floor (Floor & Wall Height) [m²]',
    dsl: "{e.data.props.floor : {all: {qty: += e.l * (e?.props?.types?.wall?.[0]?.height ?? 0), unit: 'm²'}}}"
  },
  {
    id: 'q-code-summary',
    name: 'By Code: Area / Length / Count Summary [m², m, units]',
    dsl: "{e.code : {qty: += e.a/1e6 || += e.l/1e3 || ++, unit: e.a ? 'm²' : (e.l ? 'm' : 'unit')}}"
  },
  {
    id: 'q-floor-code',
    name: 'By Floor & Code: Length by Floor [m]',
    dsl: "{e.data.props.floor : {e.code : {qty: += e.l, unit: 'm'}}}"
  },
  {
    id: 'q-layer-length',
    name: 'By Layer: Total Length per Layer [m]',
    dsl: "{e.lyr : {all: {qty: += e.l, unit: 'm'}}}"
  },
  {
    id: 'q-layer-area-len',
    name: 'By Layer: Total Area & Length per Layer [m², m]',
    dsl: "{e.lyr : {total: {length: {qty: += e.l, unit: 'm'}, area: {qty: += e.a, unit: 'm²'}}}}"
  }
];

// Keep report writes ordered per project, including while switching tabs.
const reportWriteQueues = new Map();
function queueProjectReportSave(projectPath, value) {
  const previous = reportWriteQueues.get(projectPath) || Promise.resolve();
  const write = previous.catch(() => {}).then(() => invoke('save_project_setting', {
    projectPath, key: 'report', value
  }));
  reportWriteQueues.set(projectPath, write);
  return write;
}

// Convert any nested CAD SS-QUERY result object into WBS Tree Hierarchy
export function convertQueryResultToWbsNodes(obj, prefix = 'cad-node', descriptions = {}) {
  if (!obj || typeof obj !== 'object' || Array.isArray(obj)) return [];

  return Object.entries(obj).map(([key, val], idx) => {
    const id = `${prefix}-${idx}`;

    // Leaf item with quantity and unit
    if (val && typeof val === 'object' && (val.qty !== undefined || val.unit !== undefined)) {
      const explicitCode = val.code || (key !== 'all' ? key : '');
      const descInfo = descriptions[explicitCode] || descriptions[key];
      let displayName = descInfo?.short ? descInfo.short : (key === 'all' ? 'Total Quantity' : key);
      if (!descInfo?.short) {
        if (key === 'l' || key === 'panjang') displayName = 'Length (l)';
        else if (key === 'a' || key === 'luas') displayName = 'Area (a)';
      }

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
      let branchName = key === 'undefined' ? 'Unassigned / No Floor' : key;
      if (key === 'total') branchName = 'Total Quantity';
      return {
        id,
        name: branchName,
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
  project,
  cadConnected,
  cadPlatform,
  cadDwgName,
  ipcPort,
  selectedEntities = [],
  selectionHash = null,
  descriptions = {},
  onSaveDescription,
  onDeleteDescription
}) {
  const [savedQueries, setSavedQueries] = useState(DEFAULT_SAVED_DSL_QUERIES);
  const [activeQueryId, setActiveQueryIdState] = useState(DEFAULT_SAVED_DSL_QUERIES[0].id);
  const [queryScope, setQueryScopeState] = useState('all');
  const [settingsReady, setSettingsReady] = useState(false);
  const [settingsLoadAttempt, setSettingsLoadAttempt] = useState(0);
  const [persistenceError, setPersistenceError] = useState('');
  const settingsReadyRef = useRef(false);
  const reportSettingsRef = useRef(null);

  useEffect(() => {
    let cancelled = false;
    settingsReadyRef.current = false;
    setSettingsReady(false);
    setPersistenceError('');
    const load = async () => {
      try {
        // A previous report tab may still be finishing its last queued write.
        await (reportWriteQueues.get(project.path) || Promise.resolve()).catch(() => {});
        let saved = await invoke('get_project_setting', { projectPath: project.path, key: 'report' });
        if (!saved && await invoke('get_project_setting', { projectPath: project.path, key: 'legacy_import_available' })) {
          // One-time import into the first project; all subsequent reads/writes
          // use the project database. Leave the old browser copy untouched.
          let legacy;
          try { legacy = JSON.parse(localStorage.getItem('grx_saved_dsl_queries') || 'null'); } catch { legacy = null; }
          if (Array.isArray(legacy) && legacy.length && legacy.every((query) =>
            query && typeof query.id === 'string' && typeof query.name === 'string' && typeof query.dsl === 'string')) {
            saved = { savedQueries: legacy, activeQueryId: legacy[0].id, queryScope: 'all' };
            await queueProjectReportSave(project.path, saved);
          }
        }
        if (cancelled) return;
        const validQueries = Array.isArray(saved?.savedQueries) && saved.savedQueries.length > 0 &&
          saved.savedQueries.every((query) => query && typeof query.id === 'string' && typeof query.name === 'string' && typeof query.dsl === 'string');
        const queries = validQueries ? saved.savedQueries : DEFAULT_SAVED_DSL_QUERIES;
        const active = queries.some((query) => query.id === saved?.activeQueryId) ? saved.activeQueryId : queries[0].id;
        const scope = saved?.queryScope === 'selection' ? 'selection' : 'all';
        reportSettingsRef.current = { savedQueries: queries, activeQueryId: active, queryScope: scope };
        setSavedQueries(queries);
        setActiveQueryIdState(active);
        setQueryScopeState(scope);
        settingsReadyRef.current = true;
        setSettingsReady(true);
      } catch (error) {
        if (!cancelled) setPersistenceError(String(error));
      }
    };
    load();
    return () => { cancelled = true; settingsReadyRef.current = false; };
  }, [project.path, settingsLoadAttempt]);

  const persistReportSettings = (changes) => {
    if (!settingsReadyRef.current) return;
    const next = { ...reportSettingsRef.current, ...changes };
    reportSettingsRef.current = next;
    queueProjectReportSave(project.path, next).then(() => setPersistenceError(''))
      .catch((error) => setPersistenceError(`Could not save report settings: ${String(error)}`));
  };
  const persistQueries = (queries) => {
    setSavedQueries(queries);
    persistReportSettings({ savedQueries: queries });
  };
  const setActiveQueryId = (id) => {
    setActiveQueryIdState(id);
    persistReportSettings({ activeQueryId: id });
  };
  const setQueryScope = (scope) => {
    setQueryScopeState(scope);
    persistReportSettings({ queryScope: scope });
  };

  // 3. TreeView Data (100% generated from DSL Query)
  const [wbsData, setWbsData] = useState([]);
  const [expandedNodes, setExpandedNodes] = useState(new Set());
  const [searchQuery, setSearchQuery] = useState('');
  const [statusMsg, setStatusMsg] = useState('');
  const [isLoading, setIsLoading] = useState(false);
  const queryRequestRef = useRef(0);
  const executeQueryRef = useRef(null);

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

  // Active DSL Query String
  const currentDslQuery = useMemo(() => {
    const q = savedQueries.find((item) => item.id === activeQueryId);
    return q ? q.dsl : DEFAULT_SAVED_DSL_QUERIES[0].dsl;
  }, [savedQueries, activeQueryId]);

  // Execute SS-QUERY for manual requests and selection-hash changes.
  const executeQueryAndBuildTree = useCallback(
    async (dslOverride, scopeOverride, handlesOverride) => {
      if (!settingsReadyRef.current) return;
      const requestId = ++queryRequestRef.current;
      const isStale = () => requestId !== queryRequestRef.current;
      let rawDsl = (dslOverride || currentDslQuery).trim();
      // Auto-strip accidental arrow function wrapper "e => ({...})"
      if (/^\(?e\)?\s*=>\s*\(?/.test(rawDsl)) {
        rawDsl = rawDsl.replace(/^\(?e\)?\s*=>\s*\(?/, '').replace(/\)+$/, '').trim();
      }
      // Fix common typos like ",{a:" inside object literal
      rawDsl = rawDsl.replace(/,\s*\{([a-zA-Z0-9_$]+)\s*:/g, ', $1:');
      const dsl = rawDsl;
      const scope = scopeOverride || queryScope;
      if (!dsl) return;

      setIsLoading(true);
      setStatusMsg('Evaluating SS-QUERY in CAD...');

      // Automatic refresh uses the same selection snapshot as the QTO view.
      let targetHandles = null;
      if (scope === 'selection') {
        if (handlesOverride !== undefined) {
          targetHandles = handlesOverride;
        } else if (cadConnected) {
          try {
            const pollRes = await invoke('cad_poll_selection', {
              lastVersion: 0,
              port: ipcPort
            });
            if (isStale()) return;
            if (Array.isArray(pollRes?.handles)) {
              targetHandles = pollRes.handles;
            }
          } catch (e) {
            console.warn('Failed to poll selection on demand:', e);
          }
        }
        if (targetHandles === null) {
          targetHandles = selectedEntities.map((e) => e.handle).filter(Boolean);
        }
        if (!targetHandles || targetHandles.length === 0) {
          setWbsData([]);
          setExpandedNodes(new Set());
          setCheckedNodeIds(new Set());
          setStatusMsg('No CAD objects selected. Select objects in CAD, then click "Pick Selection" or "All".');
          setIsLoading(false);
          setTimeout(() => setStatusMsg(''), 4000);
          return;
        }
      }

      if (cadConnected) {
        try {
          // Automatically inject handles: push(e.handle) if not present to support visual filter
          let dslWithHandles = dsl;
          if (!dslWithHandles.includes('handles:')) {
            if (/(unit\s*:\s*['"][^'"]*['"])/.test(dslWithHandles)) {
              dslWithHandles = dslWithHandles.replace(/(unit\s*:\s*['"][^'"]*['"])/g, '$1, handles: push(e.handle)');
            } else if (/(qty\s*:\s*[^,}]+)/.test(dslWithHandles)) {
              dslWithHandles = dslWithHandles.replace(/(qty\s*:\s*[^,}]+)/g, '$1, handles: push(e.handle)');
            }
          }

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

          if (isStale()) return;
          if (res && typeof res === 'object') {
            if (res.error) {
              alert(`CAD SS-QUERY Error:\n${res.error}`);
              setStatusMsg(`Error: ${res.error}`);
              setIsLoading(false);
              return;
            }

            if (Array.isArray(res)) {
              alert(
                'The Query DSL syntax is invalid or does not produce an aggregation (CAD returned raw entities).\n\nUse a direct DSL object without an "e=>" prefix. Example:\n{e.lyr : {total: {length: {qty: += e.l, unit: \'m\'}, area: {qty: += e.a, unit: \'m²\'}}}}'
              );
              setStatusMsg('Invalid Query DSL syntax');
              setIsLoading(false);
              return;
            }

            const nodes = convertQueryResultToWbsNodes(res, 'wbs-cad', descriptions);
            setWbsData(nodes);
            const allIds = getAllNodeIds(nodes);
            setExpandedNodes(new Set(allIds));
            const qNodes = getQuantityNodes(nodes);
            setCheckedNodeIds(new Set(qNodes.map((n) => n.id)));
            const scopeLabel = scope === 'selection' ? `Selection (${targetHandles?.length || 0} objects)` : 'Entire Drawing';
            setStatusMsg(`Success: ${scopeLabel} • ${qNodes.length} quantity item(s)`);
          } else {
            setStatusMsg('Query returned no results');
          }
        } catch (err) {
          if (isStale()) return;
          alert(`Failed to run SS-QUERY in CAD:\n${String(err)}`);
          setStatusMsg(`Query failed: ${String(err)}`);
        }
      } else {
        setWbsData([]);
        setExpandedNodes(new Set());
        setCheckedNodeIds(new Set());
        setStatusMsg('Connect CAD to run this project report.');
      }

      setIsLoading(false);
      setTimeout(() => setStatusMsg(''), 3000);
    },
    [currentDslQuery, queryScope, cadConnected, ipcPort, descriptions, selectedEntities]
  );

  // Manual trigger for Pick Selection button
  const handlePickSelection = async () => {
    if (queryScope !== 'selection') setQueryScope('selection');
    else await executeQueryAndBuildTree(currentDslQuery, 'selection');
  };

  // Manual trigger for All Entities button
  const handleQueryAll = async () => {
    if (queryScope !== 'all') setQueryScope('all');
    else await executeQueryAndBuildTree(currentDslQuery, 'all');
  };

  executeQueryRef.current = executeQueryAndBuildTree;

  // The parent changes this SHA-256 hash only when the selected IDs change.
  useEffect(() => {
    if (!settingsReady) return;
    executeQueryRef.current(currentDslQuery, queryScope,
      queryScope === 'selection' ? selectedEntities.map((entity) => entity.hand ?? entity.handle).filter(Boolean) : undefined);
    return () => { queryRequestRef.current++; };
  }, [currentDslQuery, queryScope, selectionHash, cadDwgName, cadConnected, settingsReady]);

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
            setStatusMsg(`Filter: ${checkedHandles.length} normal, ${allHandles.length - checkedHandles.length} dark gray`);
          } else {
            setStatusMsg('CAD entity colors restored');
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
    setQueryFormName('New Query DSL');
    setQueryFormDsl("{e.data.props.floor : {all: {qty: += e.l, unit: 'm'}}}");
  };

  const handleSaveQueryForm = () => {
    if (!queryFormName.trim()) {
      alert('The query name cannot be empty.');
      return;
    }
    if (!queryFormDsl.trim()) {
      alert('The Query DSL expression cannot be empty.');
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
    setStatusMsg('Query updated.');
    setTimeout(() => setStatusMsg(''), 2000);
  };

  const handleDeleteQueryForm = (idToDelete) => {
    if (savedQueries.length <= 1) {
      alert('At least one saved query is required.');
      return;
    }
    if (!window.confirm('Delete this Query DSL?')) return;

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

            {/* Show checkboxes only for quantity items while the visual filter is on. */}
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
                    title={`Click to view or edit the description for code ${codeKey} (SQLite)`}
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

          {/* Right Columns: QUANTITY & UNIT */}
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

  if (!settingsReady) return (
    <div className="flex-1 min-h-0 p-4 bg-[#111620] text-slate-300 text-xs">
      {persistenceError ? <>
        <p role="alert" className="text-rose-300">Could not load this project's report settings: {persistenceError}</p>
        <button type="button" className="btn mt-3" onClick={() => setSettingsLoadAttempt((value) => value + 1)}>Retry</button>
      </> : <p role="status">Loading project report?</p>}
    </div>
  );

  return (
    <div className="flex-1 flex flex-col bg-[#111620] text-slate-200 min-h-0 overflow-hidden font-sans select-none">
      {persistenceError && <p role="alert" className="p-3 text-xs text-rose-300">{persistenceError}</p>}
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
            <p className="text-[11px] text-slate-400 px-1 py-0.5 truncate" title={project.path}>{project.name}</p>
          </div>
        </div>

        {/* Top-Right Buttons */}
        <div className="flex items-center gap-2 shrink-0">
          <button
            onClick={handleOpenCrudModal}
            title="Manage Query DSL (create, edit, delete, save)"
            className="flex items-center gap-1.5 px-3 py-1.5 rounded-md bg-white text-[#10141d] hover:bg-slate-100 font-semibold text-xs transition-colors shadow-sm"
          >
            <Database className="w-3.5 h-3.5 text-sky-600" />
            <span>Manage Query DSL</span>
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
          2. TOOLBAR: Query Dropdown + Scope Selection + Filter Button
      ==================================================================== */}
      <div className="bg-[#131822] border-b border-[#1c2333] px-4 py-2.5 flex flex-wrap items-center justify-between gap-2.5 shrink-0">
        <div className="flex items-center gap-2.5 flex-1 min-w-[320px]">
          {/* Query dropdown */}
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
              title="Get the current CAD selection and run the Query DSL"
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
              title="Run the Query DSL on the entire CAD drawing"
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
              placeholder="Search item description or code..."
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
                ? 'Filter active: unchecked entities are dark gray (click to turn off and restore colors)'
                : 'Click to enable selection and checkbox filtering (unchecked entities appear dark gray)'
            }
            className={`flex items-center gap-1.5 px-3 py-1 rounded-md text-xs font-semibold border transition-all ${
              filterModeActive
                ? 'bg-amber-500/20 text-amber-300 border-amber-500/60 shadow-sm shadow-amber-500/20'
                : 'bg-[#19202c] hover:bg-[#202938] border-slate-700 text-slate-300'
            }`}
          >
            <Filter className="w-3.5 h-3.5" />
            <span>{filterModeActive ? 'Visual Filter: ON' : 'Visual Filter'}</span>
          </button>
        </div>
      </div>

      {/* ====================================================================
          3. TREEVIEW TABLE HEADER
      ==================================================================== */}
      <div className="bg-[#151a24] border-b border-[#21293a] px-4 py-2 flex items-center justify-between text-[11px] font-bold text-slate-400 uppercase tracking-wider shrink-0">
        <div className="flex-1">ITEM (QUERY DSL RESULT)</div>
        <div className="flex items-center gap-4 pr-6">
          <div className="w-24 text-right">QUANTITY</div>
          <div className="w-16 text-left">UNIT</div>
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
              ? 'Evaluating SS-QUERY in CAD...'
              : 'No query results. Select CAD objects or adjust the Query DSL expression.'}
          </div>
        )}
      </div>

      {/* ====================================================================
          5. MODAL: QUERY DSL MANAGER
      ==================================================================== */}
      {showCrudModal && (
        <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-sm flex items-center justify-center p-4">
          <div className="bg-[#151a24] border border-slate-700 rounded-lg w-full max-w-2xl max-h-[85vh] flex flex-col shadow-2xl">
            {/* Header */}
            <div className="px-4 py-3 border-b border-slate-700 flex items-center justify-between">
              <div className="flex items-center gap-2">
                <Database className="w-4 h-4 text-sky-400" />
                <h3 className="text-sm font-bold text-white tracking-wide">
                  Manage Query DSL (SS-QUERY)
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
                  <span className="text-[11px] font-bold text-slate-400 uppercase">Saved Queries</span>
                  <button
                    type="button"
                    onClick={handleAddNewQueryForm}
                    className="p-1 rounded bg-sky-500/20 hover:bg-sky-500/30 text-sky-300 text-[10px] flex items-center gap-1 font-semibold"
                  >
                    <Plus className="w-3 h-3" />
                    <span>Add</span>
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
                    Query Name / Title:
                  </label>
                  <input
                    type="text"
                    value={queryFormName}
                    onChange={(e) => setQueryFormName(e.target.value)}
                    placeholder="e.g. Wall Area by Floor"
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
                      <button
                        type="button"
                        onClick={() =>
                          setQueryFormDsl(
                            "{e.lyr : {total: {length: {qty: += e.l, unit: 'm'}, area: {qty: += e.a, unit: 'm²'}}}}"
                          )
                        }
                        className="text-[10px] font-mono text-emerald-400 hover:text-emerald-300"
                        title="Area & Length by Layer template"
                      >
                        [Area & Length Template]
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
                  <span>Delete</span>
                </button>
              )}

              <div className="flex items-center gap-2 ml-auto">
                <button
                  type="button"
                  onClick={() => setShowCrudModal(false)}
                  className="px-3 py-1.5 rounded bg-[#1c2333] hover:bg-[#252f44] text-xs text-slate-300 transition-colors"
                >
                  Cancel
                </button>
                <button
                  type="button"
                  onClick={handleSaveQueryForm}
                  className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-[#1e2533] hover:bg-[#293245] border border-slate-600 text-white font-medium text-xs transition-colors"
                >
                  <Save className="w-3.5 h-3.5" />
                  <span>Save</span>
                </button>
                <button
                  type="button"
                  onClick={handleApplyAndExecuteFromModal}
                  className="flex items-center gap-1.5 px-4 py-1.5 rounded bg-sky-500 hover:bg-sky-600 text-white font-semibold text-xs shadow transition-colors"
                >
                  <Check className="w-3.5 h-3.5" />
                  <span>Run in CAD (Apply)</span>
                </button>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* ====================================================================
          6. MODAL: MANAGE DESCRIPTIONS (SQLite)
      ==================================================================== */}
      <DescriptionModal
        isOpen={descModalOpen}
        onClose={() => setDescModalOpen(false)}
        initialCode={descModalCode}
        descriptions={descriptions}
        onSaveDescription={onSaveDescription}
        onDeleteDescription={onDeleteDescription}
        projectPath={project.path}
      />
    </div>
  );
}
