import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { JSDOM, VirtualConsole } from '../qto-property-box/node_modules/jsdom/lib/api.js';

const html = readFileSync(new URL('../qto-property-box/index.html', import.meta.url), 'utf8');
const bridge = readFileSync(new URL('../qto-property-box/src/desktopBridge.js', import.meta.url), 'utf8').replace(/^import .*;\n/gm, '');

const errors = [];
const virtualConsole = new VirtualConsole();
virtualConsole.on('jsdomError', (error) => { if (error.type !== 'css-parsing') errors.push(error); });

const calls = [];
let currentCadVersion = 10;
const testPolyline = {
  hand: '2C3',
  ty: 'AcDbPolyline',
  l: 2.405,
  a: 0.319,
  clr: 256,
  trp: -2,
  visible: true,
  lyr: '0',
  data: {}
};

const invoke = async (command, args) => {
  calls.push({ command, args });
  switch (command) {
    case 'list_recent_projects': return [];
    case 'list_project_drawings': return [];
    case 'list_general_states': return [];
    case 'cad_poll_selection': {
      return {
        entities: [testPolyline],
        dwg: 'TEST.dwg',
        version: currentCadVersion,
        platform: 'AutoCAD',
        changed: args.lastVersion !== currentCadVersion
      };
    }
    case 'cad_apply_qto_xdata': {
      if (args.patches && args.patches.length) {
        testPolyline.data = args.patches[0].data;
      }
      return { ok: true, count: args.patches.length };
    }
    default: return {};
  }
};

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
  window.invoke = invoke;
  window.isTauri = () => true;
  window.eval(bridge);

  await new Promise((resolve) => setTimeout(resolve, 60));

  // 1. Verify live CAD selection was synced to active item
  const activeItem = window.qtoUIState.getActiveItem();
  assert.ok(activeItem?.item, 'Active item exists');
  const pickRow = activeItem.item.rows.find(r => r.mode === 'pick');
  assert.ok(pickRow, 'Active item has pick row');
  assert.equal(pickRow.value, 0.319, 'Area category initial item picked area geometry 0.319 m²');

  // 2. Verify #btn-apply-xdata exists
  const applyBtn = document.getElementById('btn-apply-xdata');
  assert.ok(applyBtn, '#btn-apply-xdata button is created next to + Row');

  // 3. Test clicking #btn-apply-xdata
  calls.length = 0;
  applyBtn.click();
  await new Promise((resolve) => setTimeout(resolve, 50));
  assert.ok(calls.some(c => c.command === 'cad_apply_qto_xdata'), 'Clicking #btn-apply-xdata applied to CAD');

  // 4. Test pressing F5 shortcut
  calls.length = 0;
  window.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'F5', bubbles: true, cancelable: true }));
  await new Promise((resolve) => setTimeout(resolve, 50));
  assert.ok(calls.some(c => c.command === 'cad_apply_qto_xdata'), 'Pressing F5 applied to CAD');

  // 5. Test typing "CT01" and pressing Enter
  const codeInput = document.getElementById('input-item-code');
  assert.ok(codeInput, '#input-item-code exists');
  codeInput.value = 'CT01';
  codeInput.dispatchEvent(new window.Event('input', { bubbles: true }));

  calls.length = 0;
  codeInput.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  await new Promise((resolve) => setTimeout(resolve, 50));

  const applyCall = calls.find(c => c.command === 'cad_apply_qto_xdata');
  assert.ok(applyCall, 'Enter on #input-item-code triggered cad_apply_qto_xdata');
  assert.equal(testPolyline.data.code, 'CT01', 'testPolyline.data.code updated to CT01');
  assert.equal(testPolyline.data.props.code, 'CT01', 'testPolyline.data.props.code updated to CT01');

  // 6. Test US number input parsing
  assert.equal(window.parseUSNum('3,1'), 3.1, 'parseUSNum converts 3,1 to 3.1');
  assert.equal(window.parseUSNum('0,95'), 0.95, 'parseUSNum converts 0,95 to 0.95');
  assert.equal(window.parseUSNum('1,234.56'), 1234.56, 'parseUSNum parses 1,234.56 correctly');

  console.log('ALL QTO FULL SYNC & ENTER TESTS PASSED!');
} finally {
  window.close();
}
