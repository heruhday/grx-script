import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { JSDOM, VirtualConsole } from '../qto-property-box/node_modules/jsdom/lib/api.js';

const html = readFileSync(new URL('../qto-property-box/index.html', import.meta.url), 'utf8');
const errors = [];
const virtualConsole = new VirtualConsole();
virtualConsole.on('jsdomError', (error) => { if (error.type !== 'css-parsing') errors.push(error); });

const dom = new JSDOM(html, {
  url: 'http://qto.local/',
  runScripts: 'dangerously',
  pretendToBeVisual: true,
  virtualConsole,
  beforeParse(window) {
    window.scrollTo = () => {};
    window.HTMLDialogElement.prototype.showModal = function () { this.open = true; };
    window.HTMLDialogElement.prototype.close = function () { this.open = false; };
  },
});

const window = dom.window;
const document = window.document;

try {
  assert.equal(errors.length, 0, errors.map(String).join('\n'));
  assert.ok(window.qtoUIState, 'qtoUIState exists');
  assert.equal(typeof window.qtoUIState.syncCadSelection, 'function', 'syncCadSelection is a function');
  assert.equal(typeof window.qtoUIState.acceptCadSelection, 'function', 'acceptCadSelection is a function');

  // Initial active item is s01 (in Area)
  const initialRow = document.querySelector('[data-qto-item-id="s01"]');
  assert.ok(initialRow, 's01 item row exists');

  // Test live sync with Area: 2 CAD entities (a = 8.0 and 4.0 -> totalA = 12.0)
  const testEntities = [
    { hand: '2C4', ty: 'AcDbPolyline', l: 12.5, a: 8.0 },
    { hand: '2C5', ty: 'AcDbPolyline', l: 7.5, a: 4.0 }
  ];

  window.qtoUIState.syncCadSelection(testEntities);

  let dataAfterSync = window.qtoUIState.exportData();
  let currentArea = dataAfterSync.categories.find(c => c.id === 'area');
  let activeItem = currentArea.items.find(it => it.id === 's01');
  assert.ok(activeItem, 'Active area item s01 exists');
  let pickRow = activeItem.rows.find(r => r.mode === 'pick');
  assert.ok(pickRow, 'Pick row exists');
  assert.equal(pickRow.value, 12.0, `Expected area 12.0, got ${pickRow.value}`);
  assert.equal(pickRow.note, 'CAD 2 objects');

  // Test deselect (0 entities) -> value resets to 0
  window.qtoUIState.syncCadSelection([]);
  let dataAfterDeselect = window.qtoUIState.exportData();
  let rowAfterDeselect = dataAfterDeselect.categories.find(c => c.id === 'area').items.find(it => it.id === 's01').rows.find(r => r.mode === 'pick');
  assert.equal(rowAfterDeselect.value, 0, 'Deselecting resets pick row to 0');

  // Test acceptCadSelection (Pick CAD)
  window.qtoUIState.acceptCadSelection(testEntities);
  let dataAfterAccept = window.qtoUIState.exportData();
  let rowAfterAccept = dataAfterAccept.categories.find(c => c.id === 'area').items.find(it => it.id === 's01').rows.find(r => r.mode === 'pick');
  assert.equal(rowAfterAccept.value, 12.0, 'Pick CAD records measurement value 12.0');
  assert.equal(rowAfterAccept.note, 'CAD 2 objects');

  // Test with another item if added or imported
  const testData = window.qtoUIState.exportData();
  testData.categories.find(c => c.id === 'length').items.push({
    id: 'test-len-1',
    code: 'TL01',
    name: 'Test Length',
    unit: 'm',
    rows: [{ id: 'r1', value: 0, mode: 'pick', source: 'e.l', note: '' }]
  });
  window.qtoUIState.importData(testData);

  // Activate the newly imported item
  const tlRow = document.querySelector('[data-qto-item-id="test-len-1"]');
  assert.ok(tlRow, 'test-len-1 item exists in DOM');
  tlRow.querySelector('.item-list-code')?.parentElement?.click();

  window.qtoUIState.syncCadSelection(testEntities);
  const dataLen = window.qtoUIState.exportData();
  const lenItem = dataLen.categories.find(c => c.id === 'length').items.find(it => it.id === 'test-len-1');
  const lenPickRow = lenItem.rows.find(r => r.mode === 'pick');
  assert.equal(lenPickRow.value, 20.0, `Expected length sum 20.0, got ${lenPickRow.value}`);
  assert.equal(lenPickRow.source, 'e.l');
  assert.equal(lenPickRow.note, 'CAD 2 objects');

  console.log('Live CAD selection sync and Pick CAD tests passed successfully for both Area and Length!');
} finally {
  window.close();
}
