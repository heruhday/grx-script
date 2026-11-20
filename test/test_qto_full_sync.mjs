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
        const patch = args.patches[0];
        if (patch.data !== undefined && patch.data !== null) {
          testPolyline.data = {
            ...(testPolyline.data || {}),
            ...patch.data,
            props: {
              ...((testPolyline.data && testPolyline.data.props) || {}),
              ...((patch.data && patch.data.props) || {})
            }
          };
        }
        if (patch.properties && typeof patch.properties === 'object') {
          Object.assign(testPolyline, patch.properties);
        }
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

  document.body.classList.remove('projects-home');
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

  // 5. Test typing "CT01" and pressing Enter -> ONLY updates item XData, does not touch general properties
  const codeInput = document.getElementById('input-item-code');
  assert.ok(codeInput, '#input-item-code exists');
  codeInput.value = 'CT01';
  codeInput.dispatchEvent(new window.Event('input', { bubbles: true }));

  calls.length = 0;
  codeInput.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  await new Promise((resolve) => setTimeout(resolve, 50));

  const applyCallCode = calls.find(c => c.command === 'cad_apply_qto_xdata');
  assert.ok(applyCallCode, 'Enter on #input-item-code triggered cad_apply_qto_xdata');
  assert.equal(testPolyline.data.code, 'CT01', 'testPolyline.data.code updated to CT01');
  assert.equal(testPolyline.data.props.code, 'CT01', 'testPolyline.data.props.code updated to CT01');
  assert.equal(applyCallCode.args.patches[0].properties, undefined, 'Item code update did NOT send properties');
  assert.equal(testPolyline.clr, 256, 'Color is still 256 ByLayer');
  assert.equal(testPolyline.lyr, '0', 'Layer is still 0');

  // 5b. Verify that active category remains expanded and detail remains visible
  const activeInfoAfterEnter = window.qtoUIState.getActiveItem();
  assert.equal(activeInfoAfterEnter.cat.expanded, true, 'Active category remains expanded after Enter');
  assert.equal(window.qtoUIState.state.detailExpanded, true, 'Detail panel remains expanded after Enter');
  assert.ok(window.qtoUIState.state.visibleCategoryIds.includes(activeInfoAfterEnter.cat.id), 'Category remains visible');

  // 5c. Test focus preservation: when codeInput is focused, syncCadSelection does NOT destroy the DOM
  const codeInputRef = document.getElementById('input-item-code');
  codeInputRef.focus();
  assert.equal(document.activeElement, codeInputRef, 'codeInput has focus');
  window.qtoUIState.syncCadSelection([testPolyline]);
  assert.equal(document.getElementById('input-item-code'), codeInputRef, 'codeInput DOM element was NOT destroyed while focused');
  assert.equal(document.activeElement, codeInputRef, 'Focus was preserved on codeInput during CAD selection sync');

  // 6. Test updating Color alone -> ONLY updates color, does NOT touch XData or layer
  const colorInput = document.getElementById('general-color');
  assert.ok(colorInput, '#general-color exists');
  colorInput.value = '1';
  colorInput.dispatchEvent(new window.Event('input', { bubbles: true }));

  calls.length = 0;
  colorInput.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  await new Promise((resolve) => setTimeout(resolve, 50));

  const applyCallColor = calls.find(c => c.command === 'cad_apply_qto_xdata');
  assert.ok(applyCallColor, 'Enter on #general-color triggered cad_apply_qto_xdata');
  assert.equal(applyCallColor.args.patches[0].properties?.clr, 1, 'Color patch sent clr = 1');
  assert.equal(applyCallColor.args.patches[0].data, undefined, 'Color patch did NOT send or wipe data');
  assert.equal(testPolyline.clr, 1, 'testPolyline.clr updated to 1');
  assert.equal(testPolyline.data.code, 'CT01', 'testPolyline.data.code preserved as CT01');
  assert.equal(testPolyline.data.props.code, 'CT01', 'testPolyline.data.props.code preserved as CT01');
  assert.equal(testPolyline.lyr, '0', 'Layer is still 0');

  // 7. Test updating Layer alone -> ONLY updates layer, does NOT touch color or XData
  const layerInput = document.getElementById('general-layer');
  assert.ok(layerInput, '#general-layer exists');
  layerInput.value = 'Qs.Floor';
  layerInput.dispatchEvent(new window.Event('input', { bubbles: true }));

  calls.length = 0;
  layerInput.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  await new Promise((resolve) => setTimeout(resolve, 50));

  const applyCallLayer = calls.find(c => c.command === 'cad_apply_qto_xdata');
  assert.ok(applyCallLayer, 'Enter on #general-layer triggered cad_apply_qto_xdata');
  assert.equal(applyCallLayer.args.patches[0].properties?.lyr, 'Qs.Floor', 'Layer patch sent lyr = Qs.Floor');
  assert.equal(applyCallLayer.args.patches[0].data, undefined, 'Layer patch did NOT send or wipe data');
  assert.equal(testPolyline.lyr, 'Qs.Floor', 'testPolyline.lyr updated to Qs.Floor');
  assert.equal(testPolyline.clr, 1, 'testPolyline.clr preserved as 1');
  assert.equal(testPolyline.data.code, 'CT01', 'testPolyline.data.code preserved as CT01');

  // 8. Test updating Location building alone -> ONLY updates location building in props, does NOT touch color or layer
  const buildingInput = document.getElementById('location-building');
  assert.ok(buildingInput, '#location-building exists');
  buildingInput.value = 'Tower A';
  buildingInput.dispatchEvent(new window.Event('input', { bubbles: true }));

  calls.length = 0;
  buildingInput.dispatchEvent(new window.KeyboardEvent('keydown', { key: 'Enter', bubbles: true, cancelable: true }));
  await new Promise((resolve) => setTimeout(resolve, 50));

  const applyCallBuilding = calls.find(c => c.command === 'cad_apply_qto_xdata');
  assert.ok(applyCallBuilding, 'Enter on #location-building triggered cad_apply_qto_xdata');
  assert.equal(applyCallBuilding.args.patches[0].properties, undefined, 'Location patch did NOT send CAD properties');
  assert.equal(applyCallBuilding.args.patches[0].data?.props?.building, 'Tower A', 'Location patch sent building = Tower A');
  assert.equal(testPolyline.data.props.building, 'Tower A', 'testPolyline.data.props.building updated to Tower A');
  assert.equal(testPolyline.data.code, 'CT01', 'testPolyline.data.code preserved as CT01');
  assert.equal(testPolyline.clr, 1, 'testPolyline.clr preserved as 1');
  assert.equal(testPolyline.lyr, 'Qs.Floor', 'testPolyline.lyr preserved as Qs.Floor');

  // 9. Test US number input parsing
  assert.equal(window.parseUSNum('3,1'), 3.1, 'parseUSNum converts 3,1 to 3.1');
  assert.equal(window.parseUSNum('0,95'), 0.95, 'parseUSNum converts 0,95 to 0.95');
  assert.equal(window.parseUSNum('1,234.56'), 1234.56, 'parseUSNum parses 1,234.56 correctly');

  console.log('ALL GRANULAR EDIT, QTO FULL SYNC & ENTER TESTS PASSED!');
} finally {
  window.close();
}
