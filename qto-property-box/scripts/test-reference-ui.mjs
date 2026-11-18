import { strict as assert } from 'node:assert';
import { readFileSync } from 'node:fs';
import { JSDOM, VirtualConsole } from 'jsdom';

const html = readFileSync(new URL('../index.html', import.meta.url), 'utf8');
const bridge = readFileSync(new URL('../src/desktopBridge.js', import.meta.url), 'utf8').replace(/^import .*;\n/, '');
const errors = [];
const virtualConsole = new VirtualConsole();
virtualConsole.on('jsdomError', (error) => { if (error.type !== 'css-parsing') errors.push(error); });
const saved = new Map();
const calls = [];
const project = { name: 'Native test', path: 'C:/test/qto' };
const invoke = async (command, args) => {
  calls.push({ command, args });
  switch (command) {
    case 'list_recent_projects': return [project];
    case 'load_project': return project;
    case 'open_project': return null;
    case 'get_project_setting': return saved.get(args.projectPath) || null;
    case 'save_project_setting': saved.set(args.projectPath, structuredClone(args.value)); return;
    case 'list_project_drawings': return [{ id: 7, name: 'Test.dwg', path: 'C:/test/Test.dwg', short: 'Existing short' }];
    case 'list_general_states': return [];
    case 'cad_poll_selection': return { entities: [], dwg: 'Test.dwg', version: 1, platform: 'AutoCAD' };
    case 'get_drawing_details': return { full: 'Preserve this full description' };
    case 'save_drawing_details': return {};
    default: throw Error('Unexpected command: ' + command);
  }
};
const dom = new JSDOM(html, {
  url: 'http://qto.local/', runScripts: 'dangerously', pretendToBeVisual: true, virtualConsole,
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
  assert.equal(document.querySelector('#root'), null, 'Legacy React root must not be present');
  assert.ok(window.qtoUIState, 'Reference UI must boot');
  assert.ok(window.qtoWBS, 'Reference WBS editor must boot');
  assert.ok(document.getElementById('drawing-manager-panel'));
  assert.ok(document.getElementById('selected-report-panel'));
  assert.ok(document.getElementById('wbs-dialog'));
  assert.ok(window.qtoUIState.exportData().categories.some((category) => category.items.length));
  const reference = new JSDOM(readFileSync(new URL('../reference/qto_property_box.html', import.meta.url), 'utf8'), {
    url: 'http://reference.local/', runScripts: 'dangerously', pretendToBeVisual: true, virtualConsole,
    beforeParse(window) { window.scrollTo = () => {}; },
  });
  try {
    const markup = (document) => {
      const body = document.body.cloneNode(true);
      body.querySelectorAll('script').forEach((script) => script.remove());
      return body.outerHTML.replace(/\r\n/g, '\n').trim();
    };
    assert.equal(markup(document), markup(reference.window.document), 'Rendered UI markup must match the supplied HTML, without legacy UI additions');
  } finally { reference.window.close(); }
  window.invoke = invoke;
  window.isTauri = () => true;
  window.eval(bridge);
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(document.querySelectorAll('.project-entry').length, 1, 'Show native projects, never demo recents');
  await window.qtoDesktop.openRecent(project);
  assert.equal(document.body.classList.contains('projects-home'), false);
  assert.ok(window.qtoUIState.exportData().categories.every((category) => category.items.length === 0), 'Native projects start without demo measurements');
  assert.equal(document.querySelector('.drawing-entry-name').textContent, 'Test.dwg');
  const data = window.qtoUIState.exportData();
  data.categories[0].items.push({ id: 'saved-item', code: 'L-TEST', name: 'Test measurement', unit: 'm', rows: [{ id: 'r-test', value: 7, mode: 'none', source: '7', note: '' }] });
  window.qtoUIState.importData(data);
  document.getElementById('general-color').value = '42';
  await window.qtoDesktop.save();
  assert.equal(saved.get(project.path).measurements.categories[0].items[0].code, 'L-TEST');
  assert.equal(JSON.parse(saved.get(project.path).storage['qto-ui-project-v1:C%3A%2Ftest%2Fqto']).values['general-color'].value, '42');
  await document.getElementById('project-back').onclick();
  window.qtoUIState.clearData();
  await window.qtoDesktop.openRecent(project);
  assert.equal(window.qtoUIState.exportData().categories[0].items[0].code, 'L-TEST', 'Measurements survive reopening');
  assert.equal(document.getElementById('general-color').value, '42', 'UI fields survive reopening');
  await window.qtoDesktop.saveDrawingDescription(7, 'Updated short');
  const descriptionCall = calls.find((call) => call.command === 'save_drawing_details');
  assert.equal(descriptionCall.args.full, 'Preserve this full description');
  assert.equal(descriptionCall.args.short, 'Updated short');
  document.getElementById('project-open').click();
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(document.body.classList.contains('projects-home'), false, 'Cancelling native folder picker keeps current project');
  assert.equal(errors.length, 0, errors.map(String).join('\n'));
  console.log('Reference UI boot, native projects, empty-data isolation, full measurement/UI persistence, drawing description preservation, and cancellation passed.');
} finally { window.close(); }
