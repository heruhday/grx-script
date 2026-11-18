import React, { useEffect, useRef, useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { File, FileText, FolderOpen, Plus, Trash2, X } from 'lucide-react';
import DrawingDescriptionModal from './DrawingDescriptionModal';
import './project.css';

const previewTime = new Intl.DateTimeFormat('en-US', {
  timeZone: 'Asia/Jakarta', year: 'numeric', month: 'short', day: 'numeric',
  hour: '2-digit', minute: '2-digit', hourCycle: 'h23'
});

function DrawingEntryTime({ value }) {
  const date = new Date(value);
  if (Number.isNaN(date.getTime())) return null;
  return <time dateTime={value} title={value}>{previewTime.format(date)} WIB</time>;
}

export default function DrawingManager({ project, onClose, onDrawingOpened }) {
  const [drawings, setDrawings] = useState([]);
  const [selected, setSelected] = useState(new Set());
  const [busy, setBusy] = useState('loading');
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const [descriptionDrawing, setDescriptionDrawing] = useState(null);
  const closeButtonRef = useRef(null);

  useEffect(() => {
    let cancelled = false;
    invoke('list_project_drawings', { projectPath: project.path }).then((entries) => {
      if (!cancelled) setDrawings(entries);
    }).catch((reason) => {
      if (!cancelled) setError(String(reason));
    }).finally(() => { if (!cancelled) setBusy(''); });
    closeButtonRef.current?.focus();
    return () => { cancelled = true; };
  }, [project.path]);

  useEffect(() => {
    const handleKey = (event) => {
      if (event.key === 'Escape' && !busy && !descriptionDrawing) { event.preventDefault(); onClose(); }
    };
    window.addEventListener('keydown', handleKey);
    return () => window.removeEventListener('keydown', handleKey);
  }, [busy, descriptionDrawing, onClose]);

  const toggleSelection = (id) => setSelected((current) => {
    const next = new Set(current);
    if (next.has(id)) next.delete(id); else next.add(id);
    return next;
  });

  const handleDrawingDetailsSaved = (details) => {
    setDrawings((current) => current.map((drawing) => drawing.id === details.drawing_id ? {
      ...drawing, short: details.short || '', latest_entry: details.entries?.[0] || null
    } : drawing));
    setStatus('Drawing description and activity updated.');
  };

  const addDrawings = async () => {
    if (busy) return;
    setBusy('add'); setError(''); setStatus('');
    try {
      const entries = await invoke('add_project_drawings', { projectPath: project.path });
      if (entries) {
        const added = entries.length - drawings.length;
        setDrawings(entries);
        setStatus(added > 0 ? `${added.toLocaleString('en-US')} drawing(s) added.` : 'These drawings are already in the project.');
      }
    } catch (reason) { setError(String(reason)); }
    finally { setBusy(''); }
  };

  const removeDrawings = async () => {
    if (busy || !selected.size) return;
    setBusy('remove'); setError(''); setStatus('');
    try {
      const entries = await invoke('remove_project_drawings', { projectPath: project.path, ids: [...selected] });
      setDrawings(entries);
      setSelected(new Set());
      setStatus('Selected drawings removed from this project.');
    } catch (reason) { setError(String(reason)); }
    finally { setBusy(''); }
  };

  const addCadDrawings = async (activeOnly) => {
    if (busy) return;
    setBusy('add-cad'); setError(''); setStatus('');
    try {
      const result = await invoke('add_open_cad_drawings', { projectPath: project.path, activeOnly });
      setDrawings(result.drawings);
      const summary = result.added > 0 ? `${result.added.toLocaleString('en-US')} drawing(s) added.` : 'These drawings are already in the project.';
      setStatus(summary + (result.skipped > 0 ? ` ${result.skipped.toLocaleString('en-US')} unsaved or unavailable drawing(s) skipped. Save them in CAD before adding.` : ''));
    } catch (reason) { setError(String(reason)); }
    finally { setBusy(''); }
  };

  const openDrawing = async (drawing) => {
    if (busy || !drawing) return;
    setBusy('open'); setError(''); setStatus(`Opening ${drawing.name} in CAD…`);
    try {
      const result = await invoke('open_project_drawing', { projectPath: project.path, drawingId: drawing.id });
      if (result?.ok === false || result?.error) throw new Error(result.error || 'CAD could not open this drawing');
      setStatus(`Opened ${drawing.name} in CAD.`);
      onDrawingOpened?.(drawing);
    } catch (reason) { setStatus(''); setError(String(reason)); }
    finally { setBusy(''); }
  };

  const selectedDrawing = selected.size === 1 ? drawings.find((drawing) => selected.has(drawing.id)) : null;
  const allSelected = drawings.length > 0 && selected.size === drawings.length;

  return (
    <div className="project-modal-backdrop">
      <section className="drawing-manager" role="dialog" aria-modal="true" aria-labelledby="drawing-manager-title"
        inert={descriptionDrawing ? '' : undefined}>
        <header className="drawing-manager-header">
          <div>
            <h2 id="drawing-manager-title"><FolderOpen aria-hidden="true" /> Drawing Manager</h2>
            <p title={project.path}>{project.name}</p>
          </div>
          <button ref={closeButtonRef} type="button" className="project-icon-button" aria-label="Close drawing manager"
            disabled={!!busy} onClick={onClose}><X aria-hidden="true" /></button>
        </header>

        <div className="drawing-manager-toolbar">
          <button type="button" className="project-button primary" disabled={!!busy} onClick={addDrawings}>
            <Plus aria-hidden="true" /> Add Files
          </button>
          <button type="button" className="project-button" disabled={!!busy}
            title="Add the active saved drawing from CAD" onClick={() => addCadDrawings(true)}>
            <Plus aria-hidden="true" /> Add Active DWG
          </button>
          <button type="button" className="project-button" disabled={!!busy}
            title="Add all saved drawings open in the connected CAD session" onClick={() => addCadDrawings(false)}>
            <Plus aria-hidden="true" /> Add All Open DWGs
          </button>
          <button type="button" className="project-button danger" disabled={!!busy || !selected.size} onClick={removeDrawings}>
            <Trash2 aria-hidden="true" /> Remove Selected
          </button>
          <button type="button" className="project-button" disabled={!!busy || !selectedDrawing}
            onClick={() => openDrawing(selectedDrawing)}><FolderOpen aria-hidden="true" /> Open in CAD</button>
        </div>

        <p className="drawing-manager-hint">Double-click a drawing to open it in CAD. Remove keeps its DWG file on disk and removes its project details.</p>
        <div className="drawing-list-header">
          <label>
            <input type="checkbox" checked={allSelected} disabled={!!busy || !drawings.length}
              ref={(node) => { if (node) node.indeterminate = selected.size > 0 && !allSelected; }}
              onChange={(event) => setSelected(event.target.checked ? new Set(drawings.map((drawing) => drawing.id)) : new Set())} />
            Select all
          </label>
          <span>{selected.size.toLocaleString('en-US')} / {drawings.length.toLocaleString('en-US')} selected</span>
        </div>
        <ul className="drawing-list" aria-label="Project drawings">
          {drawings.map((drawing) => (
            <li key={drawing.id}>
              <div className={`drawing-row ${selected.has(drawing.id) ? 'selected' : ''}`} tabIndex={busy ? -1 : 0}
                title={drawing.path} onDoubleClick={() => openDrawing(drawing)}
                onClick={(event) => {
                  if (busy) return;
                  if (event.ctrlKey || event.metaKey) toggleSelection(drawing.id);
                  else setSelected(new Set([drawing.id]));
                }}
                onKeyDown={(event) => {
                  if (event.target !== event.currentTarget) return;
                  if (event.key === 'Enter') { event.preventDefault(); openDrawing(drawing); }
                  if (event.key === ' ') { event.preventDefault(); if (!busy) toggleSelection(drawing.id); }
                }}>
                <input type="checkbox" checked={selected.has(drawing.id)} disabled={!!busy}
                  aria-label={`Select ${drawing.name}`} onClick={(event) => event.stopPropagation()}
                  onDoubleClick={(event) => event.stopPropagation()} onChange={() => toggleSelection(drawing.id)} />
                <File className="drawing-file-icon" aria-hidden="true" />
                <span className="drawing-row-text">
                  {drawing.short?.trim() && <strong className="drawing-short-description" title={drawing.short}>{drawing.short}</strong>}
                  <strong className={`drawing-filename ${drawing.short?.trim() ? 'is-secondary' : ''}`}>{drawing.name}</strong>
                  <span>{drawing.path}</span>
                  {drawing.latest_entry && <span className="drawing-latest-entry">
                    <span className="drawing-latest-body" title={drawing.latest_entry.body}>
                      <b>{drawing.latest_entry.kind === 'note' ? 'Note' : 'Comment'}:</b> {drawing.latest_entry.body}
                    </span>
                    <span className="drawing-latest-meta">
                      <span>{drawing.latest_entry.created_by}</span>
                      <DrawingEntryTime value={drawing.latest_entry.created_at} />
                    </span>
                  </span>}
                </span>
                <button type="button" className="project-button drawing-desc-button" disabled={!!busy}
                  aria-label={`Description for ${drawing.name}`} title={`Description, comments, and notes for ${drawing.name}`}
                  onClick={(event) => { event.stopPropagation(); setDescriptionDrawing(drawing); }}
                  onDoubleClick={(event) => event.stopPropagation()}>
                  <FileText aria-hidden="true" /> Desc
                </button>
              </div>
            </li>
          ))}
          {!drawings.length && !busy && <li className="project-empty">
            <File aria-hidden="true" /><strong>No drawings in this project</strong>
            <p>Use Add Files to choose one or more DWG files.</p>
          </li>}
        </ul>

        <footer className="drawing-manager-footer">
          {error && <p className="project-error" role="alert">{error}</p>}
          {(status || busy) && <p className="project-status" role="status">{status || (busy === 'loading' ? 'Loading drawings…' : 'Updating drawings…')}</p>}
          <button type="button" className="project-button" disabled={!!busy} onClick={onClose}>Done</button>
        </footer>
      </section>
      {descriptionDrawing && <DrawingDescriptionModal projectPath={project.path} drawing={descriptionDrawing}
        onClose={() => setDescriptionDrawing(null)}
        onSaved={handleDrawingDetailsSaved} />}
    </div>
  );
}
