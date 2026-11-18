import React, { useEffect, useState } from 'react';
import { invoke } from '@tauri-apps/api/core';
import { ArrowRight, FolderKanban, FolderOpen, Plus, RefreshCw } from 'lucide-react';
import App from './App';
import ThemeControl from './ThemeControl';
import './project.css';

export default function ProjectShell() {
  const [project, setProject] = useState(null);
  const [projects, setProjects] = useState([]);
  const [creating, setCreating] = useState(false);
  const [name, setName] = useState('');
  const [busy, setBusy] = useState('loading');
  const [error, setError] = useState('');

  useEffect(() => {
    let cancelled = false;
    invoke('list_recent_projects').then((entries) => {
      if (!cancelled) setProjects(entries);
    }).catch((reason) => {
      if (!cancelled) setError(String(reason));
    }).finally(() => { if (!cancelled) setBusy(''); });
    return () => { cancelled = true; };
  }, []);

  const refreshProjects = async () => {
    setBusy('loading');
    setError('');
    try { setProjects(await invoke('list_recent_projects')); }
    catch (reason) { setError(String(reason)); }
    finally { setBusy(''); }
  };

  const openProject = async (command, args, action) => {
    if (busy) return;
    setBusy(action);
    setError('');
    try {
      const opened = await invoke(command, args);
      if (opened) {
        setProject(opened);
        setCreating(false);
        setName('');
      }
    } catch (reason) { setError(String(reason)); }
    finally { setBusy(''); }
  };

  if (project) {
    return <App key={project.path} project={project} onCloseProject={() => {
      setProject(null);
      refreshProjects();
    }} />;
  }

  return (
    <main className="project-start">
      <section className="project-home" aria-labelledby="projects-title">
        <header className="project-home-header">
          <span className="project-home-icon"><FolderKanban aria-hidden="true" /></span>
          <div>
            <span className="project-eyebrow">QTO workspace</span>
            <h1 id="projects-title">Projects</h1>
          </div>
        </header>
        <p className="project-intro">Open a project to work with its drawings, states, and QTO data.</p>

        <div className="project-start-actions">
          <button type="button" className="project-button primary" disabled={!!busy}
            onClick={() => openProject('open_project', undefined, 'open')}>
            <FolderOpen aria-hidden="true" /> Open Project Folder
          </button>
          <button type="button" className="project-button" disabled={!!busy}
            onClick={() => { setCreating((value) => !value); setError(''); }}>
            <Plus aria-hidden="true" /> New Project
          </button>
        </div>

        {creating && (
          <div className="project-modal-backdrop"><form className="project-create-form project-create-dialog" role="dialog" aria-modal="true" aria-labelledby="create-project-title" onSubmit={(event) => {
            event.preventDefault();
            if (name.trim()) openProject('create_project', { name: name.trim() }, 'create');
          }}>
            <h2 id="create-project-title">New Project</h2>
            <label htmlFor="new-project-name">Project name</label>
            <input id="new-project-name" type="text" className="project-text-input" value={name}
              maxLength={100} autoFocus disabled={!!busy} placeholder="e.g. Hotel Renovation"
              onChange={(event) => setName(event.target.value)} />
            <button type="submit" className="project-button primary" disabled={!!busy || !name.trim()}>
              <FolderOpen aria-hidden="true" /> Choose Folder & Create
            </button>
            <button type="button" className="project-button" onClick={() => setCreating(false)}>Cancel</button>
          </form></div>
        )}

        {error && <p className="project-error" role="alert">{error}</p>}
        {busy && <p className="project-status" role="status">
          {busy === 'loading' ? 'Loading projects…' : busy === 'create' ? 'Creating project…' : 'Opening project…'}
        </p>}

        <div className="project-list-header">
          <h2>Recent projects</h2>
          <button type="button" className="project-icon-button" aria-label="Refresh recent projects"
            title="Refresh recent projects" disabled={!!busy} onClick={refreshProjects}>
            <RefreshCw aria-hidden="true" />
          </button>
        </div>
        <div className="project-list">
          {projects.map((entry) => (
            <button type="button" className="project-card" key={entry.path} disabled={!!busy}
              title={entry.path} onClick={() => openProject('load_project', { projectPath: entry.path }, entry.path)}>
              <FolderKanban className="project-card-icon" aria-hidden="true" />
              <span className="project-card-text">
                <strong>{entry.name}</strong>
                <span>{entry.path}</span>
              </span>
              <span className="project-card-open">Open <ArrowRight aria-hidden="true" /></span>
            </button>
          ))}
          {!projects.length && !busy && (
            <div className="project-empty">
              <FolderOpen aria-hidden="true" />
              <strong>No projects yet</strong>
              <p>Create a project or open an existing project folder.</p>
            </div>
          )}
        </div>
        <footer className="project-appearance"><ThemeControl /></footer>
      </section>
    </main>
  );
}
