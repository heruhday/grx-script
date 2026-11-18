import React, { useEffect, useId, useRef, useState } from 'react';
import { Check, FileText, MessageSquare, X } from 'lucide-react';
import { invoke } from '@tauri-apps/api/core';
import './drawingDetails.css';

const jakartaTime = new Intl.DateTimeFormat('en-US', {
  timeZone: 'Asia/Jakarta',
  year: 'numeric', month: 'short', day: '2-digit',
  hour: '2-digit', minute: '2-digit', second: '2-digit', hourCycle: 'h23',
});

function SavedTime({ value }) {
  if (!value) return null;
  const date = new Date(value);
  return (
    <time dateTime={value} title={value}>
      {Number.isNaN(date.getTime()) ? 'Unknown time' : `${jakartaTime.format(date)} WIB`}
    </time>
  );
}

export default function DrawingDescriptionModal({ projectPath, drawing, onClose, onSaved }) {
  const id = useId();
  const dialogRef = useRef(null);
  const shortRef = useRef(null);
  const busyRef = useRef('loading');
  const requestRef = useRef(0);
  const onCloseRef = useRef(onClose);
  const [details, setDetails] = useState(null);
  const [short, setShort] = useState('');
  const [full, setFull] = useState('');
  const [kind, setKind] = useState('comment');
  const [body, setBody] = useState('');
  const [busy, setBusy] = useState('loading');
  const [error, setError] = useState('');
  const [status, setStatus] = useState('');
  const [loadAttempt, setLoadAttempt] = useState(0);
  const drawingId = drawing.id;
  onCloseRef.current = onClose;

  const changeBusy = (value) => {
    busyRef.current = value;
    setBusy(value);
  };

  useEffect(() => {
    const previousFocus = document.activeElement;
    dialogRef.current?.focus();
    const isTopDialog = () => {
      const dialogs = [...document.querySelectorAll('[role="dialog"][aria-modal="true"]')]
        .filter((node) => node.getClientRects().length > 0);
      return dialogs[dialogs.length - 1] === dialogRef.current;
    };
    const handleKey = (event) => {
      if (!isTopDialog()) return;
      if (event.key === 'F5') {
        event.preventDefault();
        event.stopImmediatePropagation();
        return;
      }
      if (event.key === 'Escape') {
        event.preventDefault();
        event.stopImmediatePropagation();
        if (!busyRef.current) onCloseRef.current?.();
      }
      if (event.key === 'Tab') {
        const controls = [...dialogRef.current.querySelectorAll(
          'button:not(:disabled), input:not(:disabled), textarea:not(:disabled), select:not(:disabled), [tabindex="0"]',
        )].filter((node) => node.getClientRects().length > 0);
        const first = controls[0];
        const last = controls[controls.length - 1];
        const active = document.activeElement;
        if (!first) {
          event.preventDefault();
          dialogRef.current.focus();
        } else if (event.shiftKey && (active === first || !controls.includes(active))) {
          event.preventDefault();
          last.focus();
        } else if (!event.shiftKey && (active === last || !controls.includes(active))) {
          event.preventDefault();
          first.focus();
        }
      }
    };
    window.addEventListener('keydown', handleKey, true);
    return () => {
      window.removeEventListener('keydown', handleKey, true);
      if (previousFocus?.isConnected) previousFocus.focus();
    };
  }, []);

  useEffect(() => {
    const request = ++requestRef.current;
    changeBusy('loading');
    setError('');
    setStatus('');
    setDetails(null);
    setShort('');
    setFull('');
    setBody('');
    setKind('comment');
    invoke('get_drawing_details', { projectPath, drawingId })
      .then((result) => {
        if (request !== requestRef.current) return;
        setDetails(result);
        setShort(result.short || '');
        setFull(result.full || '');
      })
      .catch((reason) => {
        if (request === requestRef.current) setError(`Could not load drawing details: ${String(reason)}`);
      })
      .finally(() => {
        if (request === requestRef.current) changeBusy('');
      });
    return () => { requestRef.current += 1; };
  }, [projectPath, drawingId, loadAttempt]);

  useEffect(() => {
    if (!busy && details && document.activeElement === dialogRef.current) shortRef.current?.focus();
  }, [busy, details]);

  const dirty = !!details && (short !== (details.short || '') || full !== (details.full || ''));
  const hasDraft = dirty || !!body.trim();
  const saveDescription = async (event) => {
    event.preventDefault();
    if (busyRef.current || !details) return;
    const request = requestRef.current;
    changeBusy('save');
    setError('');
    setStatus('');
    try {
      const result = await invoke('save_drawing_details', { projectPath, drawingId, short, full });
      if (request !== requestRef.current) return;
      setDetails(result);
      setShort(result.short || '');
      setFull(result.full || '');
      setStatus('Description saved.');
      onSaved?.(result);
    } catch (reason) {
      if (request === requestRef.current) setError(`Could not save description: ${String(reason)}`);
    } finally {
      if (request === requestRef.current) changeBusy('');
    }
  };

  const addEntry = async (event) => {
    event.preventDefault();
    if (busyRef.current || !details || !body.trim()) return;
    const request = requestRef.current;
    changeBusy('entry');
    setError('');
    setStatus('');
    try {
      const result = await invoke('add_drawing_entry', { projectPath, drawingId, kind, body: body.trim() });
      if (request !== requestRef.current) return;
      // Entry writes refresh history without replacing unsaved description fields.
      setDetails(result);
      setBody('');
      setStatus(kind === 'note' ? 'Note added.' : 'Comment added.');
      onSaved?.(result);
    } catch (reason) {
      if (request === requestRef.current) setError(`Could not add ${kind}: ${String(reason)}`);
    } finally {
      if (request === requestRef.current) changeBusy('');
    }
  };

  const close = () => { if (!busyRef.current) onClose?.(); };

  return (
    <div className="drawing-details-backdrop" onClick={(event) => event.stopPropagation()}
      onDoubleClick={(event) => event.stopPropagation()} onKeyDown={(event) => event.stopPropagation()}>
      <section ref={dialogRef} tabIndex={-1} className="drawing-details-modal" role="dialog"
        aria-modal="true" aria-labelledby={`${id}-title`} aria-describedby={`${id}-drawing`} aria-busy={!!busy}>
        <header className="drawing-details-header">
          <h2 id={`${id}-title`}><FileText aria-hidden="true" /> Drawing Description</h2>
          <button type="button" className="drawing-details-close" disabled={!!busy} onClick={close}
            aria-label={hasDraft ? 'Cancel and close drawing description' : 'Close drawing description'}>
            <X aria-hidden="true" />
          </button>
        </header>

        <div className="drawing-details-content">
          <div id={`${id}-drawing`} className="drawing-details-file">
            <strong title={drawing.name}>{drawing.name}</strong>
            <span title={drawing.path}>{drawing.path}</span>
          </div>

          {busy === 'loading' && <p className="drawing-details-status" role="status">Loading drawing details...</p>}
          {!details && !busy && (
            <button type="button" className="drawing-details-button" onClick={() => setLoadAttempt((value) => value + 1)}>
              Retry loading
            </button>
          )}

          <form id={`${id}-description-form`} className="drawing-details-form" onSubmit={saveDescription}>
            <label htmlFor={`${id}-short`}>Short Description</label>
            <input ref={shortRef} id={`${id}-short`} value={short} maxLength={500} disabled={!!busy || !details}
              onChange={(event) => setShort(event.target.value)} placeholder="A brief description of this drawing" />
            <label htmlFor={`${id}-full`}>Description</label>
            <textarea id={`${id}-full`} value={full} maxLength={50000} rows={5} disabled={!!busy || !details}
              onChange={(event) => setFull(event.target.value)} placeholder="Drawing details and specifications" />
            <p className="drawing-details-saved">
              {details?.updated_at ? <>Last saved by <strong>{details.updated_by || 'Unknown user'}</strong> · <SavedTime value={details.updated_at} /></> : 'No description saved yet.'}
            </p>
          </form>

          <section className="drawing-details-entries" aria-labelledby={`${id}-history-title`}>
            <h3 id={`${id}-history-title`}><MessageSquare aria-hidden="true" /> Comments &amp; Notes</h3>
            {details && !details.entries?.length && <p className="drawing-details-empty">No comments or notes yet.</p>}
            {!!details?.entries?.length && <ol className="drawing-details-history">
              {details.entries.map((entry) => (
                <li key={entry.id} className="drawing-details-entry">
                  <div className="drawing-details-entry-meta">
                    <span className={`drawing-details-kind ${entry.kind === 'note' ? 'is-note' : ''}`}>{entry.kind === 'note' ? 'Note' : 'Comment'}</span>
                    <strong>{entry.created_by || 'Unknown user'}</strong>
                    <SavedTime value={entry.created_at} />
                  </div>
                  <p>{entry.body}</p>
                </li>
              ))}
            </ol>}
            <form className="drawing-details-entry-form" onSubmit={addEntry}>
              <label htmlFor={`${id}-kind`}>Entry type</label>
              <select id={`${id}-kind`} value={kind} disabled={!!busy || !details} onChange={(event) => setKind(event.target.value)}>
                <option value="comment">Comment</option>
                <option value="note">Note</option>
              </select>
              <label htmlFor={`${id}-body`}>{kind === 'note' ? 'New note' : 'New comment'}</label>
              <textarea id={`${id}-body`} rows={3} value={body} disabled={!!busy || !details}
                onChange={(event) => setBody(event.target.value)} placeholder={kind === 'note' ? 'Write a note...' : 'Write a comment...'} />
              <div className="drawing-details-entry-actions">
                <span>Author and time are recorded when added.</span>
                <button type="submit" className="drawing-details-button" disabled={!!busy || !details || !body.trim()}>
                  {busy === 'entry' ? 'Adding...' : kind === 'note' ? 'Add Note' : 'Add Comment'}
                </button>
              </div>
            </form>
          </section>
        </div>

        <footer className="drawing-details-footer">
          <div className="drawing-details-feedback" aria-live="polite">
            {error && <p className="drawing-details-error" role="alert">{error}</p>}
            {status && <p className="drawing-details-status" role="status">{status}</p>}
            {dirty && <p className="drawing-details-draft">Unsaved description changes</p>}
          </div>
          <div className="drawing-details-footer-actions">
            <button type="button" className="drawing-details-button" disabled={!!busy} onClick={close}>{hasDraft ? 'Cancel' : 'Done'}</button>
            <button type="submit" form={`${id}-description-form`} className="drawing-details-button primary"
              disabled={!!busy || !details || !dirty}>
              <Check aria-hidden="true" /> {busy === 'save' ? 'Saving...' : 'Save Description'}
            </button>
          </div>
        </footer>
      </section>
    </div>
  );
}
