import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';

const source = readFileSync(new URL('../qto-property-box/src/WbsReportView.jsx', import.meta.url), 'utf8');
const body = source.match(/async \(dslOverride, scopeOverride, handlesOverride\) => \{([\s\S]*?)\n    \},\r?\n    \[currentDslQuery/)[1];
const state = { tree: ['old'], errors: [] };
let calls = [];
const context = vm.createContext({
  settingsReadyRef: { current: true }, settingsReady: true,
  currentDslQuery: "{e.lyr : {all: {qty: += e.l, unit: 'm'}}}",
  queryScope: 'selection', cadConnected: true, ipcPort: 8123, descriptions: {},
  selectedEntities: [{ handle: 'STALE' }], queryRequestRef: { current: 0 },
  setIsLoading(value) { state.loading = value; },
  setStatusMsg(value) { state.status = value; },
  setWbsData(value) { state.tree = value; },
  setExpandedNodes() {}, setCheckedNodeIds() {},
  convertQueryResultToWbsNodes: (res) => res.nodes,
  getAllNodeIds: (nodes) => nodes.map((n) => n.id),
  getQuantityNodes: (nodes) => nodes,
  setTimeout() {}, console,
  alert(value) { state.errors.push(value); },
  invoke: async (command, args) => { calls.push({ command, args }); return { nodes: [{ id: 'new' }] }; }
});
const run = vm.runInContext(`(async (dslOverride, scopeOverride, handlesOverride) => {${body}})`, context);
await run(undefined, 'selection', []);
assert.equal(calls.length, 0); // Empty selection never falls back to querying the drawing.
assert.equal(state.tree.length, 0);
await run(undefined, 'selection', ['A1', 'A2']);
assert.equal(calls.length, 1);
assert.deepEqual(calls[0].args.handles, ['A1', 'A2']);
assert.equal(state.tree[0].id, 'new');
let releaseOld;
context.invoke = async () => new Promise((resolve) => { releaseOld = resolve; });
const oldRequest = run(undefined, 'selection', ['OLD']);
context.invoke = async () => ({ nodes: [{ id: 'latest' }] });
await run(undefined, 'selection', ['LATEST']);
releaseOld({ nodes: [{ id: 'outdated' }] });
await oldRequest;
assert.equal(state.tree[0].id, 'latest');
assert.equal(state.errors.length, 0);
// Exercise the actual effect with a dependency cache, as React does on rerenders.
const effect = source.match(/  useEffect\(\(\) => \{\r?\n    if \(\!settingsReady\) return;\r?\n    executeQueryRef\.current[\s\S]*?\}, \[currentDslQuery, queryScope, selectionHash, cadDwgName, cadConnected, settingsReady\]\);/)[0];
let previousDeps;
let cleanup;
let refreshes = 0;
context.selectionHash = 'sha256-first';
context.cadDwgName = 'A.dwg';
context.executeQueryRef = { current: () => { refreshes++; } };
context.useEffect = (callback, deps) => {
  if (!previousDeps || deps.some((value, i) => value !== previousDeps[i])) {
    cleanup?.(); cleanup = callback(); previousDeps = deps;
  }
};
vm.runInContext(effect, context);
vm.runInContext(effect, context);
assert.equal(refreshes, 1);
context.selectedEntities = [{ handle: 'same-id-new-object' }];
vm.runInContext(effect, context);
assert.equal(refreshes, 1);
context.selectionHash = 'sha256-second';
vm.runInContext(effect, context);
assert.equal(refreshes, 2);
context.cadDwgName = 'B.dwg';
vm.runInContext(effect, context);
assert.equal(refreshes, 3);
console.log('Report selection hash, empty scope, and stale query checks passed');
