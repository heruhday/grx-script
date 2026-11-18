import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { JSDOM, VirtualConsole } from '../qto-property-box/node_modules/jsdom/lib/api.js';

const html = readFileSync(new URL('../qto-property-box/index.html', import.meta.url), 'utf8');
const bridge = readFileSync(new URL('../qto-property-box/src/desktopBridge.js', import.meta.url), 'utf8').replace(/^import .*;\n/gm, '');

const errors = [];
const virtualConsole = new VirtualConsole();
virtualConsole.on('jsdomError', (error) => { if (error.type !== 'css-parsing') errors.push(error); });

const calls = [];
const testEntity = { hand: '2C4', ty: 'AcDbLine', l: 5024.938, a: 0, clr: 256, trp: -2, visible: true, lyr: '0', data: null };

const invoke = async (command, args) => {
  calls.push({ command, args });
  switch (command) {
    case 'list_recent_projects': return [];
    case 'list_project_drawings': return [];
    case 'list_general_states': return [];
    case 'cad_poll_selection': return { entities: [testEntity], dwg: 'TEST.dwg', version: 1, platform: 'AutoCAD', changed: true };
    case 'cad_apply_qto_xdata': return { ok: true, count: args.patches.length };
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

  await new Promise((resolve) => setTimeout(resolve, 50));

  // Initial item s01 (in Area) is already active and displayed in detail!
  // Poll CAD selection and pick it
  await window.qtoDesktop.pickSelection();

  // Now simulate user typing "CT01" in #input-item-code and pressing Enter
  const codeInput = document.getElementById('input-item-code');
  assert.ok(codeInput, '#input-item-code exists');
  codeInput.value = 'CT01';
  codeInput.dispatchEvent(new window.Event('input', { bubbles: true }));

  // Clear calls
  calls.length = 0;

  // Dispatch Enter keydown on #input-item-code
  codeInput.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));

  await new Promise((resolve) => setTimeout(resolve, 50));

  // Verify cad_apply_qto_xdata was called with CT01
  const enterApplyCall = calls.find(c => c.command === 'cad_apply_qto_xdata');
  assert.ok(enterApplyCall, 'cad_apply_qto_xdata called on pressing Enter in #input-item-code');
  assert.equal(enterApplyCall.args.patches.length, 1);
  const patch = enterApplyCall.args.patches[0];
  assert.equal(patch.handle, '2C4');
  assert.equal(patch.data.code, 'CT01');
  assert.equal(patch.data.props.code, 'CT01');
  assert.equal(patch.data.props.category, 'area');
  assert.ok(patch.data.props.types.area);
  assert.equal(patch.data.props.types.area[0].code, 'CT01');

  // Verify code in QTO is updated
  const activeItem = window.qtoUIState.exportData().categories.find(c => c.id === 'area').items.find(i => i.id === 's01');
  assert.equal(activeItem.code, 'CT01');

  console.log('Enter key on item code updates CAD e.data and len successfully!');
} finally {
  window.close();
}
