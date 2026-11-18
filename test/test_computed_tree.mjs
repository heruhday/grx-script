import assert from 'node:assert/strict';
import { buildComputedTree } from '../qto-property-box/src/computedTree.js';

const records = [
  { Building: 'B', Floor: '2', Area: 'X', Type: 'Length', Code: 'C2', unit: 'm', qty: 3, entityId: '1' },
  { Building: 'A', Floor: '10', Area: 'Y', Type: 'Length', Code: 'C1', unit: 'm', qty: 4, entityId: '2' },
  { Building: 'A', Floor: '2', Area: 'X', Type: 'Length', Code: 'C1', unit: 'm', qty: 5, entityId: '3' }
];
const level = (column, sortOn = 'Code', order = 'A to Z') => ({ column, sortOn, order });
let tree = buildComputedTree(records, [level('Building'), level('Floor'), level('Code')]);
assert.deepEqual(tree.map((n) => n.value), ['A', 'B']);
assert.deepEqual(tree[0].children.map((n) => n.value), ['2', '10']);
assert.equal(tree[0].entityCount, 2);
assert.equal(tree[0].children[0].children[0].qty, 5);
// Reordering/removing levels rebuilds the actual hierarchy, rather than just the hint.
tree = buildComputedTree(records, [level('Type'), level('Code')]);
assert.equal(tree[0].column, 'Type');
assert.equal(tree[0].children[0].column, 'Code');
assert.equal(tree[0].children[0].value, 'C1');
assert.equal(tree[0].children[0].qty, 9);
assert.equal(tree[0].children[1].qty, 3);
const descriptions = { 'floor:2': { short: 'Zulu' }, 'floor:10': { short: 'Alpha' } };
tree = buildComputedTree(records, [level('Floor', 'Short Description')], descriptions);
assert.deepEqual(tree.map((n) => n.value), ['10', '2']);
tree = buildComputedTree(records, [level('Floor', 'Short Description', 'Z to A')], descriptions);
assert.deepEqual(tree.map((n) => n.value), ['2', '10']);
tree = buildComputedTree(records, [level('Floor', 'Code', 'Largest to Smallest')]);
assert.deepEqual(tree.map((n) => n.value), ['10', '2']);
assert.deepEqual(buildComputedTree([], [level('Building')]), []);
// Repeated item rows on one entity count that entity once, and units stay separate.
tree = buildComputedTree([
  ...records,
  { ...records[0], Code: 'C1' },
  { ...records[0], Code: 'C1', unit: 'm²' }
], [level('Building'), level('Code')]);
assert.equal(tree[1].entityCount, 1);
assert.equal(tree[1].children[0].children.length, 2);
console.log('Computed tree level order, descriptions, sorting, grouping, quantities, and unit checks passed');
