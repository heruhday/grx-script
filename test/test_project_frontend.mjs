import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const source = readFileSync(new URL('../qto-property-box/src/DrawingManager.jsx', import.meta.url), 'utf8');
const body = (name, parameter = '') => source.match(new RegExp(`const ${name} = async \\(${parameter}\\) => \\{([\\s\\S]*?)\\n  \\};`))[1];
const context = vm.createContext({
  project: { path: 'C:/Projects/Hotel' }, busy: '', drawings: [], selected: new Set(),
  setBusy(value) { context.busy = value; }, setError(value) { context.error = value; },
  setStatus(value) { context.status = value; }, setDrawings(value) { context.drawings = typeof value === 'function' ? value(context.drawings) : value; },
  setSelected(value) { context.selected = value; },
  onDrawingOpened(value) { context.opened = value; }
});
const add = vm.runInContext(`(async () => {${body('addDrawings')}})`, context);
const remove = vm.runInContext(`(async () => {${body('removeDrawings')}})`, context);
const open = vm.runInContext(`(async (drawing) => {${body('openDrawing', 'drawing')}})`, context);
const drawings = [{ id: 1, name: 'A.dwg', path: 'C:/A.dwg' }, { id: 2, name: 'B.dwg', path: 'C:/B.dwg' }];
context.invoke = async (command, args) => {
  assert.equal(command, 'add_project_drawings'); assert.equal(args.projectPath, context.project.path); return drawings;
};
await add();
assert.equal(context.drawings.length, 2);
context.invoke = async () => null; // Cancelling file picker preserves the drawing list.
await add();
assert.equal(context.drawings.length, 2);
context.selected = new Set([1, 2]);
context.invoke = async (command, args) => {
  assert.equal(command, 'remove_project_drawings'); assert.deepEqual([...args.ids], [1, 2]); return [];
};
await remove();
assert.equal(context.drawings.length, 0);
assert.equal(context.selected.size, 0);
context.invoke = async (command, args) => {
  assert.equal(command, 'open_project_drawing'); assert.equal(args.drawingId, 2); return { ok: true };
};
await open(drawings[1]);
assert.equal(context.opened.id, 2);
context.opened = null;
context.invoke = async () => { throw Error('Drawing file not found'); };
await open(drawings[0]);
assert.equal(context.opened, null);
assert.match(context.error, /not found/);
assert.equal(context.busy, '');
const addCad = vm.runInContext(`(async (activeOnly) => {${body('addCadDrawings', 'activeOnly')}})`, context);
context.invoke = async (command, args) => {
  assert.equal(command, 'add_open_cad_drawings');
  assert.equal(args.projectPath, context.project.path);
  assert.equal(args.activeOnly, true);
  return { drawings: [drawings[0]], added: 1, skipped: 0 };
};
await addCad(true);
assert.equal(context.drawings.length, 1);
assert.match(context.status, /1 drawing\(s\) added/);
context.invoke = async (_, args) => {
  assert.equal(args.activeOnly, false);
  return { drawings, added: 1, skipped: 2 };
};
await addCad(false);
assert.equal(context.drawings.length, 2);
assert.match(context.status, /2 unsaved or unavailable/);
context.invoke = async () => { throw Error('CAD IPC not connected'); };
await addCad(false);
assert.equal(context.drawings.length, 2);
assert.match(context.error, /not connected/);
// ProjectShell mounts the QTO application only after opening a project.
const shell = readFileSync(new URL('../qto-property-box/src/ProjectShell.jsx', import.meta.url), 'utf8');
const openProjectBody = shell.match(/const openProject = async \(command, args, action\) => \{([\s\S]*?)\n  \};/)[1];
context.setProject = (value) => { context.activeProject = value; };
context.setCreating = () => {};
context.setName = () => {};
context.invoke = async () => null;
const openProject = vm.runInContext(`(async (command,args,action) => {${openProjectBody}})`, context);
await openProject('open_project', undefined, 'open');
assert.equal(context.activeProject, undefined);
context.invoke = async () => context.project;
await openProject('load_project', { projectPath: context.project.path }, 'open');
assert.equal(context.activeProject.path, context.project.path);
console.log('Project opening, multi-DWG add/remove, cancellation, and CAD-open error checks passed');

const savedBody = source.match(/const handleDrawingDetailsSaved = \(details\) => \{([\s\S]*?)\n  \};/)[1];
const update = vm.runInContext(`((details) => {${savedBody}})`, context);
context.drawings = [{ id: 1, name: 'A.dwg' }, { id: 2, name: 'B.dwg' }];
const latest = { id: 9, kind: 'note', body: 'Latest note', created_at: '2026-11-13T10:00:00.000Z', created_by: 'tester' };
update({ drawing_id: 1, short: 'Ground floor plan', entries: [latest, { id: 8, body: 'Older comment' }] });
assert.equal(context.drawings[0].short, 'Ground floor plan');
assert.equal(context.drawings[0].latest_entry.body, 'Latest note');
assert.equal(context.drawings[1].short, undefined);
update({ drawing_id: 1, short: '', entries: [] });
assert.equal(context.drawings[0].short, '');
assert.equal(context.drawings[0].latest_entry, null);
console.log('Drawing summary updates and per-row isolation checks passed');
