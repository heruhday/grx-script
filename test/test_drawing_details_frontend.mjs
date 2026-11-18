import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const source = readFileSync(new URL('../qto-property-box/src/DrawingDescriptionModal.jsx', import.meta.url), 'utf8');
const bodyOf = (name) => source.match(new RegExp(`const ${name} = async \\(event\\) => \\{([\\s\\S]*?)\\n  \\};`))[1];
let savedCount = 0;
let closeCount = 0;
const context = vm.createContext({
  projectPath: 'C:/Projects/Hotel', drawingId: 7,
  busyRef: { current: '' }, requestRef: { current: 1 },
  details: { short: 'Old', full: 'Saved full', entries: [] },
  short: 'New description', full: 'New full', kind: 'note', body: '  Check wall layout  ',
  changeBusy(value) { context.busyRef.current = value; },
  setError(value) { context.error = value; }, setStatus(value) { context.status = value; },
  setDetails(value) { context.details = value; }, setShort(value) { context.short = value; },
  setFull(value) { context.full = value; }, setBody(value) { context.body = value; },
  onSaved() { savedCount++; }, onClose() { closeCount++; }
});
const save = vm.runInContext(`(async (event) => {${bodyOf('saveDescription')}})`, context);
const add = vm.runInContext(`(async (event) => {${bodyOf('addEntry')}})`, context);
const event = { preventDefault() {} };
context.invoke = async (command, args) => {
  assert.equal(command, 'save_drawing_details');
  assert.equal(args.projectPath, context.projectPath);
  assert.equal(args.drawingId, 7);
  assert.equal(args.short, 'New description');
  assert.equal('author' in args, false);
  assert.equal('timestamp' in args, false);
  return { short: args.short, full: args.full, entries: [], updated_by: 'HOST/user', updated_at: '2026-11-13T09:00:00.000Z' };
};
await save(event);
assert.equal(savedCount, 1);
assert.equal(closeCount, 0);
assert.equal(context.details.updated_by, 'HOST/user');
context.short = 'Unsaved short';
context.full = 'Unsaved full';
context.invoke = async (command, args) => {
  assert.equal(command, 'add_drawing_entry');
  assert.equal(args.kind, 'note');
  assert.equal(args.body, 'Check wall layout');
  assert.equal(args.drawingId, 7);
  return { short: 'New description', full: 'New full', entries: [{ id: 1, kind: 'note', body: args.body }] };
};
await add(event);
assert.equal(context.short, 'Unsaved short');
assert.equal(context.full, 'Unsaved full');
assert.equal(context.body, '');
assert.equal(context.details.entries.length, 1);
context.body = 'Keep failed note';
context.invoke = async () => { throw Error('Database locked'); };
await add(event);
assert.equal(context.body, 'Keep failed note');
assert.match(context.error, /Database locked/);
await save(event);
assert.equal(context.short, 'Unsaved short');
assert.equal(context.full, 'Unsaved full');
assert.equal(savedCount, 2);
assert.equal(closeCount, 0);
assert.equal(context.busyRef.current, '');
console.log('Drawing description save, server audit, notes, draft preservation, and error checks passed');
// F5 must not refresh the webview or apply entity data while editing a drawing.
const keyBody = source.match(/const handleKey = \(event\) => \{([\s\S]*?)\n    \};/)[1];
context.isTopDialog = () => true;
const keyHandler = vm.runInContext(`((event) => {${keyBody}})`, context);
let prevented = false;
let stopped = false;
keyHandler({ key: 'F5', preventDefault() { prevented = true; }, stopImmediatePropagation() { stopped = true; } });
assert.equal(prevented, true);
assert.equal(stopped, true);
