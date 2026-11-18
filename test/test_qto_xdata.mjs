import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { createHash, webcrypto } from 'node:crypto';

const source = readFileSync(new URL('../qto-property-box/src/App.jsx', import.meta.url), 'utf8');
const generalFields = vm.runInNewContext(source.match(/const GENERAL_FIELDS = (\[[\s\S]*?\n\]);/)[1]);
const pick = source.match(/const handlePickSelection = async \(options = \{\}\) => \{([\s\S]*?)\n  \};/)[1];
const apply = source.match(/const handleApplyXdata = async \(\) => \{([\s\S]*?)\n  \};/)[1];
const selected = [{ hand: 'AB12', clr: 1, lyr: 'Walls', trp: 50, visible: false, data: { props: { floor: 'Old' }, custom: 42 }, l: 5 }];
let entities;
let message;
let failure;
const context = vm.createContext({
  crypto: webcrypto, TextEncoder, GENERAL_FIELDS: generalFields,
  general: { clr: '', lyr: '', trp: '', visible: '' },
  generalActions: { clr: 'nothing', lyr: 'nothing', trp: 'nothing', visible: 'nothing' },
  setGeneral(value) { context.general = value; },
  ipcPort: 8123, cadConnected: true, options: {},
  selectionHashRef: { current: null }, selectionVersionRef: { current: 0 }, selectionDwgRef: { current: null },
  selectionBusyRef: { current: false }, TYPE_PREFIXES: { Length: 'L', Area: 'A' },
  setItems(value) { context.items = value; },
  setEmptyArrayAction(value) { context.emptyArrayAction = value; },
  setSelectionHash(value) { context.selectionHash = value; },
  setSelectedGeneralState() {}, setGeneralStateName() {},
  emptyArrayAction: 'nothing',
  collapsedSections: { building: false, array: true },
  buildingActions: { building: 'nothing', floor: 'update', area: 'nothing' }, building: { floor: 'New' }, items: [],
  showToast: (value) => { message = value; },
  alert: (value) => { failure = value; },
  setCadConnected() {}, setCadPlatform() {}, setCadDwgName() {},
  setBuilding(value) { context.building = value; },
  setEntities(value) {
    entities = typeof value === 'function' ? value(entities) : value;
    context.entities = entities;
  },
  invoke: async () => ({ entities: selected })
});
context.selectionHashRef.current = null;
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(entities[0].handle, 'AB12');
assert.equal(entities[0].rawCadData.custom, 42);
assert.equal(context.building.floor, 'Old');
assert.equal(context.general.clr, '1');
assert.equal(context.general.visible, 'false');
context.invoke = async () => ({ entities: [
  { hand: 'AB12', data: { props: { building: 'House', floor: 'Level 2' } } },
  { hand: 'AB13', data: { props: { building: 'House', floor: 'Level 2' } } }
] });
context.selectionHashRef.current = null;
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, 'Level 2');
assert.equal(context.building.building, 'House');
assert.equal(context.building.area, '');
context.invoke = async () => ({ entities: [
  { hand: 'AB12', data: { props: { floor: 'Level 1' } } },
  { hand: 'AB13', data: { props: { floor: 'Level 2' } } }
] });
context.selectionHashRef.current = null;
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, '<Varies>');
context.invoke = async () => ({ entities: [] });
context.selectionHashRef.current = null;
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, '');
assert.equal(entities.length, 0);
context.invoke = async () => ({ entities: selected });
context.selectionHashRef.current = null;
await vm.runInContext(`(async () => {${pick}})()`, context);
context.building.floor = 'New';
context.invoke = async (_, { patches }) => {
  assert.equal(patches[0].handle, 'AB12');
  assert.equal(patches[0].data.props.floor, 'New');
  assert.equal(patches[0].data.custom, 42);
  return { ok: true, count: 1 };
};
await vm.runInContext(`(async () => {${apply}})()`, context);
assert.equal(entities[0].rawCadData.props.floor, 'New');
assert.match(message, /XData written to 1/);
context.buildingActions.floor = 'remove';
context.invoke = async (_, { patches }) => {
  assert.equal('floor' in patches[0].data.props, false);
  assert.equal(patches[0].data.custom, 42);
  return { ok: true, count: 1 };
};
await vm.runInContext(`(async () => {${apply}})()`, context);
assert.equal('floor' in entities[0].rawCadData.props, false);
context.invoke = async () => ({ error: 'Invalid handle' });
await vm.runInContext(`(async () => {${apply}})()`, context);
assert.match(failure, /Invalid handle/);
assert.match(message, /Error:/);
// Empty UI lists must still support deleting stored item data on every entity.
context.entities = ['AB12', 'AB13'].map((handle) => ({
  handle,
  rawCadData: { custom: 42, props: {
    building: 'House', types: { length: [{ code: 'L1', qty: 5 }] },
    array_items: [{ code: 'legacy' }]
  } }
}));
entities = context.entities;
context.buildingActions = { building: 'nothing', floor: 'nothing', area: 'nothing' };
context.collapsedSections.array = false;
context.emptyArrayAction = 'remove';
context.invoke = async (_, { patches }) => {
  assert.equal(patches.length, 2);
  for (const { data } of patches) {
    assert.equal('types' in data.props, false);
    assert.equal('array_items' in data.props, false);
    assert.equal(data.props.building, 'House');
    assert.equal(data.custom, 42);
  }
  return { ok: true, count: 2 };
};
await vm.runInContext(`(async () => {${apply}})()`, context);
assert.match(message, /all array item data removed/);
// Run the actual IPC script against the object-patch API used by older CAD plugins.
const backend = readFileSync(new URL('../qto-property-box/src-tauri/src/main.rs', import.meta.url), 'utf8');
const command = backend.slice(backend.indexOf('fn cad_apply_qto_xdata'), backend.indexOf('fn cad_filter_entities_color'));
const template = command.match(/"\(\(\) =>[\s\S]*?\}\}\)\(\)"/)[0];
const patches = [{ handle: 'AB12', properties: { clr: 1, lyr: 'Walls', trp: 50, visible: false }, data: { props: { building: 'House' } } }];
const calls = [];
const script = template.slice(1, -1).replace(/\\\r?\n\s*/g, '')
  .replace('{}', '__PATCHES__').replaceAll('{{', '{').replaceAll('}}', '}')
  .replace('__PATCHES__', JSON.stringify(patches));
const result = vm.runInNewContext(script, { entity: { set(patch) {
  assert.equal(typeof patch, 'object');
  assert.equal(patch.hand, 'AB12');
  calls.push(patch);
} } });
assert.equal(result.ok, true);
assert.equal(result.count, 1);
assert.equal(calls.length, 2);
assert.equal(calls[0].data, null);
assert.equal(calls[1].data.props.building, 'House');
assert.equal(calls[1].visible, false);
assert.equal(calls[1].clr, 1);
console.log('QTO XData selection, preservation, refresh, and error checks passed');

// Versioned selection refresh must leave in-progress edits alone until CAD changes selection.
context.options = { silent: true, lastVersion: 0 };
context.invoke = async (_, args) => {
  assert.equal(args.lastVersion, 0);
  return { version: 7, changed: true, dwg: 'A.dwg', entities: [
    { hand: 'A1', data: { props: { floor: 'Level 2', types: {
      length: [{ code: 'Trim', param: { height: 2 } }]
    } } } }
  ] };
};
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.selectionVersionRef.current, 7);
assert.equal(context.selectionHashRef.current,
  createHash('sha256').update(JSON.stringify(['A1'])).digest('hex'));
assert.equal(context.building.floor, 'Level 2');
assert.equal(context.items[0].code, 'Trim');
assert.equal(context.items[0].action, 'nothing');
context.building.floor = 'Draft edit';
const unchangedItems = context.items;
context.invoke = async () => ({ version: 8, changed: true, dwg: 'A.dwg',
  handles: ['a1', 'A1'], entities: [{ hand: 'A1', data: { props: { floor: 'Server value' } } }]
});
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, 'Draft edit');
assert.equal(context.items, unchangedItems);
assert.equal(context.selectionVersionRef.current, 8);
// A different ID set must refresh.
context.invoke = async () => ({ version: 9, changed: true, dwg: 'A.dwg',
  handles: ['A2', 'A3'], entities: [{ hand: 'A2', data: { props: { floor: 'New selection' } } }]
});
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, 'New selection');
context.building.floor = 'Draft edit';
context.invoke = async () => ({ version: 10, changed: true, dwg: 'A.dwg',
  handles: ['a3', 'a2'], entities: []
});
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, 'Draft edit');
context.options.lastVersion = 7;
context.invoke = async () => ({ version: 7, changed: false, dwg: 'A.dwg' });
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.building.floor, 'Draft edit');
context.invoke = async () => ({ version: 7, changed: false, dwg: 'B.dwg' });
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.selectionVersionRef.current, 0);
context.invoke = async () => ({ version: 8, changed: true, dwg: 'B.dwg', entities: [] });
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.entities.length, 0);
assert.equal(context.items.length, 0);
assert.equal(context.building.floor, '');
context.options.isCancelled = () => true;
context.invoke = async () => ({ version: 9, entities: selected });
await vm.runInContext(`(async () => {${pick}})()`, context);
assert.equal(context.entities.length, 0);
assert.equal(context.selectionBusyRef.current, false);
console.log('Selection version, draft preservation, drawing switch, empty selection, and cancellation checks passed');
const pollingEffect = source.match(/useEffect\(\(\) => \{\r?\n    let cancelled = false;(\r?\n    let timer;[\s\S]*?)\n  \}, \[ipcPort\]\);/)[1];
let scheduled;
let resolvePoll;
let pollCalls = 0;
let pollOptions;
const pollingContext = vm.createContext({
  selectionVersionRef: { current: 8 },
  selectionRefreshRef: { current: (options) => {
    pollCalls++;
    pollOptions = options;
    return new Promise((resolve) => { resolvePoll = resolve; });
  } },
  setTimeout(callback, delay) { assert.equal(delay, 500); scheduled = callback; return 1; },
  clearTimeout() { scheduled = undefined; }
});
const cleanup = vm.runInContext(`(() => { let cancelled = false; ${pollingEffect} })()`, pollingContext);
assert.equal(pollCalls, 1);
assert.equal(pollOptions.lastVersion, 8);
assert.equal(scheduled, undefined); // Never queue another poll while the current one is pending.
resolvePoll();
await new Promise((resolve) => setImmediate(resolve));
assert.equal(typeof scheduled, 'function');
scheduled();
assert.equal(pollCalls, 2);
cleanup();
assert.equal(pollOptions.isCancelled(), true);
resolvePoll();
await new Promise((resolve) => setImmediate(resolve));
assert.equal(scheduled, undefined);
console.log('Selection polling scheduling and cleanup checks passed');

// General properties apply even when both metadata sections are collapsed.
context.entities = [{ handle: 'AB12', rawCadData: { custom: 42, props: { floor: 'Keep' } } }];
entities = context.entities;
context.collapsedSections = { general: false, building: true, array: true };
context.general = { clr: '2', lyr: 'Walls', trp: '45', visible: 'false' };
context.generalActions = { clr: 'update', lyr: 'update', trp: 'update', visible: 'update' };
context.invoke = async (_, { patches }) => {
  assert.equal(patches[0].properties.clr, 2);
  assert.equal(patches[0].properties.lyr, 'Walls');
  assert.equal(patches[0].properties.trp, 45);
  assert.equal(patches[0].properties.visible, false);
  assert.equal(patches[0].data.props.floor, 'Keep');
  assert.equal(patches[0].data.custom, 42);
  return { ok: true, count: 1 };
};
await vm.runInContext(`(async () => {${apply}})()`, context);
assert.equal(entities[0].visible, false);
context.generalActions = { clr: 'remove', lyr: 'remove', trp: 'remove', visible: 'remove' };
context.invoke = async (_, { patches }) => {
  assert.equal(patches[0].properties.clr, 256);
  assert.equal(patches[0].properties.lyr, '0');
  assert.equal(patches[0].properties.trp, -2);
  assert.equal(patches[0].properties.visible, true);
  return { ok: true, count: 1 };
};
await vm.runInContext(`(async () => {${apply}})()`, context);
context.generalActions.trp = 'update';
context.general.trp = '101';
context.invoke = async () => { throw new Error('Invalid transparency reached CAD'); };
await vm.runInContext(`(async () => {${apply}})()`, context);
assert.match(message, /Transparency must/);
console.log('General selection, CAD properties, reset defaults, visibility, and validation checks passed');
