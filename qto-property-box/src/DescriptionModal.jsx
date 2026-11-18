import React, { useState, useEffect } from 'react';
import { X, Tag, FileText, Check, Trash2, Database, Layers } from 'lucide-react';
import { invoke } from '@tauri-apps/api/core';

export default function DescriptionModal({
  isOpen,
  onClose,
  initialCode = '',
  descriptions = {},
  onSaveDescription,
  onDeleteDescription,
  projectPath
}) {
  const [code, setCode] = useState(initialCode);
  const [itemsStr, setItemsStr] = useState('');
  const [shortDesc, setShortDesc] = useState('');
  const [fullDesc, setFullDesc] = useState('');
  const [statusMsg, setStatusMsg] = useState('');
  const [error, setError] = useState('');
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    if (isOpen) {
      const activeCode = initialCode.trim();
      setCode(activeCode);
      const existing = descriptions[activeCode] || {};
      const arr = Array.isArray(existing.items) ? existing.items : (existing.items ? [existing.items] : []);
      setItemsStr(arr.join(', '));
      setShortDesc(existing.short || '');
      setFullDesc(existing.full || '');
      setStatusMsg('');
      setError('');
    }
  }, [isOpen, initialCode, descriptions]);

  if (!isOpen) return null;

  const parsedItems = itemsStr
    .split(',')
    .map((s) => s.trim())
    .filter(Boolean);

  const handleSave = async () => {
    if (busy) return;
    const trimmedCode = code.trim();
    if (!trimmedCode) {
      setError('The item code cannot be empty.');
      return;
    }
    if (!projectPath) {
      setError('Open a project before saving descriptions.');
      return;
    }

    const payload = {
      items: parsedItems,
      short: shortDesc.trim(),
      full: fullDesc.trim()
    };

    setBusy(true);
    setError('');
    setStatusMsg('Saving description…');
    try {
      await invoke('save_project_description', { projectPath, code: trimmedCode, description: payload });
      onSaveDescription?.(trimmedCode, payload);
      onClose();
    } catch (err) {
      setError(`Could not save description: ${String(err)}`);
    } finally { setBusy(false); setStatusMsg(''); }
  };

  const handleDelete = async () => {
    if (busy) return;
    const trimmedCode = code.trim();
    if (!trimmedCode || !descriptions[trimmedCode]) return;
    if (!window.confirm(`Delete the description for code "${trimmedCode}"?`)) return;

    if (!projectPath) { setError('Open a project before deleting descriptions.'); return; }
    setBusy(true);
    setError('');
    setStatusMsg('Deleting description…');
    try {
      await invoke('delete_project_description', { projectPath, code: trimmedCode });
      onDeleteDescription?.(trimmedCode);
      onClose();
    } catch (err) {
      setError(`Could not delete description: ${String(err)}`);
    } finally { setBusy(false); setStatusMsg(''); }
  };

  return (
    <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-sm flex items-center justify-center p-4">
      <div className="bg-[#151a24] border border-slate-700 rounded-lg w-full max-w-lg max-h-[90vh] flex flex-col shadow-2xl">
        {/* Header */}
        <div className="px-4 py-3 border-b border-slate-700 flex items-center justify-between">
          <div className="flex items-center gap-2">
            <Database className="w-4 h-4 text-emerald-400" />
            <h3 className="text-sm font-bold text-white tracking-wide">
              Manage Code Description
            </h3>
          </div>
          <button
            onClick={onClose}
            disabled={busy}
            className="p-1 rounded text-slate-400 hover:text-white transition-colors"
          >
            <X className="w-4 h-4" />
          </button>
        </div>

        {/* Content */}
        <div className="p-4 space-y-3.5 overflow-y-auto flex-1">
          <div className="bg-emerald-950/20 border border-emerald-500/30 rounded p-2.5 text-[11px] text-emerald-300 leading-relaxed">
            Descriptions are saved with this project and available across its drawings.
          </div>

          {/* Code Input */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1">
              Item Code (Key):
            </label>
            <input
              type="text"
              disabled={busy}
              value={code}
              onChange={(e) => setCode(e.target.value)}
              placeholder="e.g. FloorFin/CT1 or CPT1"
              className="w-full bg-[#0d1117] border border-slate-700 px-3 py-1.5 rounded text-xs font-mono text-amber-300 focus:outline-none focus:border-emerald-500"
            />
          </div>

          {/* Items / Hierarchy Breadcrumb */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1">
              Item Hierarchy (comma-separated):
            </label>
            <input
              type="text"
              disabled={busy}
              value={itemsStr}
              onChange={(e) => setItemsStr(e.target.value)}
              placeholder="e.g. Architecture, Flooring, Ceramic"
              className="w-full bg-[#0d1117] border border-slate-700 px-3 py-1.5 rounded text-xs text-slate-200 focus:outline-none focus:border-emerald-500"
            />
            {parsedItems.length > 0 && (
              <div className="flex flex-wrap gap-1 mt-1.5">
                {parsedItems.map((item, idx) => (
                  <span
                    key={idx}
                    className="inline-flex items-center gap-1 px-2 py-0.5 rounded text-[10px] bg-slate-800 text-slate-300 border border-slate-700"
                  >
                    <span>{idx + 1}.</span>
                    <span>{item}</span>
                  </span>
                ))}
              </div>
            )}
          </div>

          {/* Short Description */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1">
              Short Description:
            </label>
            <input
              type="text"
              disabled={busy}
              value={shortDesc}
              onChange={(e) => setShortDesc(e.target.value)}
              placeholder="e.g. Roman ceramic tile, size 60x60"
              className="w-full bg-[#0d1117] border border-slate-700 px-3 py-1.5 rounded text-xs text-slate-100 focus:outline-none focus:border-emerald-500"
            />
          </div>

          {/* Full Description */}
          <div>
            <label className="block text-xs font-medium text-slate-300 mb-1">
              Full Description / Specification:
            </label>
            <textarea
              disabled={busy}
              value={fullDesc}
              onChange={(e) => setFullDesc(e.target.value)}
              placeholder="Technical specifications, brand, type, installation method..."
              rows={4}
              className="w-full bg-[#0d1117] border border-slate-700 p-2.5 rounded text-xs text-slate-200 focus:outline-none focus:border-emerald-500 resize-none font-sans leading-relaxed"
            />
          </div>

          {statusMsg && (
            <p className="text-xs text-emerald-400 font-medium animate-pulse">{statusMsg}</p>
          )}
          {error && <p role="alert" className="text-xs text-rose-300 font-medium">{error}</p>}
        </div>

        {/* Footer */}
        <div className="px-4 py-3 border-t border-slate-700 flex items-center justify-between bg-[#12161f]">
          {descriptions[code.trim()] ? (
            <button
              onClick={handleDelete}
              disabled={busy}
              className="flex items-center gap-1 px-3 py-1.5 rounded bg-rose-950/40 hover:bg-rose-900/60 text-rose-300 border border-rose-800/40 text-xs transition-colors"
            >
              <Trash2 className="w-3.5 h-3.5" />
              <span>Delete</span>
            </button>
          ) : (
            <div />
          )}

          <div className="flex items-center gap-2">
            <button
              onClick={onClose}
              disabled={busy}
              className="px-3 py-1.5 rounded bg-[#1c2333] hover:bg-[#252f44] text-xs text-slate-300 transition-colors"
            >
              Cancel
            </button>
            <button
              onClick={handleSave}
              disabled={busy}
              className="flex items-center gap-1.5 px-4 py-1.5 rounded bg-emerald-600 hover:bg-emerald-500 text-white font-semibold text-xs shadow transition-colors"
            >
              <Check className="w-3.5 h-3.5" />
              <span>Save Description</span>
            </button>
          </div>
        </div>
      </div>
    </div>
  );
}
