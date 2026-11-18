import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
const source = readFileSync(new URL('../qto-property-box/src/App.jsx', import.meta.url), 'utf8');
const handler = (name, args, async = true) => {
  const match = source.match(new RegExp(`const ${name} = ${async ? 'async ' : ''}\\(${args}\\) => \\{([\\s\\S]*?)\\n  \\};`));
  assert.ok(match, name);
  return `(${async ? 'async ' : ''}(${args}) => {${match[1]}})`;
};
const state = { name: 'Walls', values: { clr: '1', lyr: 'Walls', trp: '50', visible: 'false' },
  actions: { clr: 'update', lyr: 'update', trp: 'nothing', visible: 'update' } };
let saved;
let calls = [];
const context = vm.createContext({
  project: { path: "C:/test/project-a" },
  generalStates: [state], selectedGeneralState: '', generalStateName: '',
  general: {}, generalActions: {},
  setSelectedGeneralState(value) { context.selectedGeneralState = value; },
  setGeneralStateName(value) { context.generalStateName = value; },
  setGeneral(value) { context.general = value; },
  setGeneralActions(value) { context.generalActions = value; },
  setGeneralStates(value) { context.generalStates = value; },
  setGeneralStatesBusy(value) { context.busy = value; },
  setGeneralStatesError(value) { context.error = value; },
  showToast() {},
  invoke: async (command, args) => {
    calls.push(command);
    assert.equal(args.projectPath, "C:/test/project-a");
    if (command === 'save_general_state') { saved = args.state; return saved; }
    if (command === 'list_general_states') return saved ? [saved] : [];
    if (command === 'delete_general_state') { assert.equal(args.name, 'Doors'); saved = null; return; }
    throw new Error('Loading a state must not write to CAD');
  }
});
const load = vm.runInContext(handler('handleLoadGeneralState', 'name', false), context);
const save = vm.runInContext(handler('handleSaveGeneralState', ''), context);
const remove = vm.runInContext(handler('handleDeleteGeneralState', ''), context);
load('Walls');
assert.equal(context.general.visible, 'false');
assert.equal(context.generalActions.trp, 'nothing');
assert.notEqual(context.general, state.values);
assert.equal(calls.length, 0);
context.generalStateName = ' Doors ';
await save();
assert.equal(saved.name, 'Doors');
assert.equal(saved.values.lyr, 'Walls');
assert.equal(context.selectedGeneralState, 'Doors');
assert.equal(context.busy, false);
await remove();
assert.equal(context.generalStates.length, 0);
assert.equal(context.general.lyr, 'Walls'); // Deleting a preset does not change the form or CAD.
context.generalStateName = ' ';
calls = [];
await save();
assert.equal(calls.length, 0);
console.log('General named-state load, save, delete, and no-CAD-write checks passed');
