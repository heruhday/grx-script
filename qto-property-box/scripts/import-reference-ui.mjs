import { readFileSync, writeFileSync } from 'node:fs';

// Keep the supplied UI intact. Adapt only its service boundaries for desktop use.
const root = new URL('../', import.meta.url);
let html = readFileSync(new URL('reference/qto_property_box.html', root), 'utf8').replace(/\r\n/g, '\n');
function replace(before, after) {
  if (!html.includes(before)) throw new Error(`Reference integration point missing: ${before.slice(0, 90)}`);
  html = html.replace(before, after);
}
replace('<script src="https://cdn.tailwindcss.com/3.4.17"></script>', '<link rel="stylesheet" href="/src/reference-tailwind.css" />');

replace('      function renderCadSimulator() {', '      function renderCadSimulator() {\n        if (window.qtoDesktop) return;');

html = html.replaceAll(
  "body.querySelector('.pick-sim-btn').addEventListener('click', () => {",
  "body.querySelector('.pick-sim-btn').addEventListener('click', () => { if(window.qtoDesktop){window.qtoDesktop.pickSelection();return;}"
);

replace(
  `      function parseUSNum(v) {
        const num = parseFloat(String(v ?? '').replace(/[\\s,]+/g, '').trim());
        return Number.isFinite(num) ? num : 0;
      }`,
  `      function parseUSNum(v) {
        if (v == null) return 0;
        let str = String(v).trim();
        if (str.includes(',') && !str.includes('.')) str = str.replace(',', '.');
        else str = str.replace(/[\\s,]+/g, '');
        const num = parseFloat(str);
        return Number.isFinite(num) ? num : 0;
      }
      window.parseUSNum = parseUSNum;`
);

const injectedUIState = `      window.qtoUIState = {
        state,
        exportData: () => JSON.parse(JSON.stringify({categories:state.categories,customFunctions,evalScripts})),
        importData: data => {
          if(!data || !Array.isArray(data.categories)) throw Error('Invalid measurement data');
          state.categories.forEach(cat=>{const saved=data.categories.find(c=>c.id===cat.id);cat.items=Array.isArray(saved?.items)?saved.items:[];});
          if(Array.isArray(data.customFunctions))customFunctions=data.customFunctions;
          if(Array.isArray(data.evalScripts))evalScripts=data.evalScripts;
          render();
        },
        clearData: () => {state.categories.forEach(cat=>cat.items=[]);render();},
        getActiveItem: () => getActiveItem(),
        render: () => render(),
        syncCadSelection: entities => {
          const res = getActiveItem();
          if(!res || !res.item) return;
          const cat = res.cat;
          const item = res.item;
          const count = entities ? entities.length : 0;
          const totalL = entities ? entities.reduce((sum, ent) => sum + (Number(ent.l ?? ent.length) || 0), 0) : 0;
          const totalA = entities ? entities.reduce((sum, ent) => sum + (Number(ent.a ?? ent.area) || 0), 0) : 0;
          const totalV = entities ? entities.reduce((sum, ent) => sum + (Number(ent.v ?? ent.vol ?? ent.volume) || 0), 0) : 0;

          if (!item.rows) item.rows = [];
          let pickRow = item.rows.find(r => r.mode === 'pick');
          if(!pickRow && item.rows.length === 1 && (pickRow = item.rows[0])) {
            pickRow.mode = 'pick';
          }
          if(!pickRow && item.rows.length === 0) {
            pickRow = { id: uid('row'), value: 0, mode: 'pick', source: cat.sourceDefault || 'e.l', note: '' };
            item.rows.push(pickRow);
          }
          if(!pickRow && item.rows.length > 0) {
            pickRow = item.rows[0];
          }
          if(!pickRow) return;

          ensureRowModeState(pickRow, cat);
          if(pickRow.params) {
            pickRow.params.cadL = totalL;
            pickRow.params.cadA = totalA;
            pickRow.params.cadV = totalV;
          }

          if(count === 0) {
            pickRow.value = 0;
            pickRow.note = '';
          } else {
            const isAreaWall = cat.id === 'area' || cat.id === 'wall';
            const isVolume = cat.id === 'solid';
            const isWeight = cat.id === 'weight';
            const curPm = pickRow.params ? pickRow.params.pickMode : '';

            if(cat.id === 'length') {
              pickRow.value = Math.round(totalL * 10000) / 10000;
              pickRow.source = 'e.l';
            } else if(cat.id === 'no') {
              pickRow.value = count;
              pickRow.source = 'e.n';
            } else if(isAreaWall) {
              const hM = (pickRow.params && pickRow.params.dimUnitH === 'mm') ? (pickRow.params.cadH / 1000) : (pickRow.params ? pickRow.params.cadH : 1);
              const dedM = (pickRow.params && pickRow.params.deductDimUnit === 'mm') ? (pickRow.params.deductDim / 1000) : (pickRow.params ? pickRow.params.deductDim : 0);
              if(curPm === 'e.a') {
                pickRow.value = Math.round(Math.max(0, totalA - (pickRow.params ? pickRow.params.deductArea : 0)) * 10000) / 10000;
                pickRow.source = 'e.a';
              } else {
                pickRow.value = Math.round((totalL * Math.max(0, hM - dedM)) * 10000) / 10000;
                pickRow.source = 'e.l x h';
              }
            } else if(isVolume) {
              const bM = (pickRow.params && pickRow.params.dimUnitB === 'mm') ? (pickRow.params.cadB / 1000) : (pickRow.params ? pickRow.params.cadB : 1);
              const hM = (pickRow.params && pickRow.params.dimUnitH === 'mm') ? (pickRow.params.cadH / 1000) : (pickRow.params ? pickRow.params.cadH : 1);
              if(curPm === 'e.v') {
                pickRow.value = Math.round(totalV * 10000) / 10000;
                pickRow.source = 'e.v';
              } else if(curPm === 'e.a x h|t') {
                pickRow.value = Math.round((totalA * hM) * 10000) / 10000;
                pickRow.source = 'e.a x h|t';
              } else {
                pickRow.value = Math.round((totalL * bM * hM) * 10000) / 10000;
                pickRow.source = 'e.l x b x h';
              }
            } else if(isWeight) {
              const bM = (pickRow.params && pickRow.params.dimUnitB === 'mm') ? (pickRow.params.cadB / 1000) : (pickRow.params ? pickRow.params.cadB : 1);
              const hM = (pickRow.params && pickRow.params.dimUnitH === 'mm') ? (pickRow.params.cadH / 1000) : (pickRow.params ? pickRow.params.cadH : 1);
              if(curPm === 'e.l x w/m') {
                pickRow.value = Math.round((totalL * (pickRow.params ? pickRow.params.unitWeightLen : 1)) * 10000) / 10000;
                pickRow.source = 'e.l x w/m';
              } else if(curPm === 'e.v x w/m3') {
                pickRow.value = Math.round((totalV * (pickRow.params ? pickRow.params.unitWeightVol : 1)) * 10000) / 10000;
                pickRow.source = 'e.v x w/m3';
              } else if(curPm === 'e.a x h|t x w/m3') {
                pickRow.value = Math.round((totalA * hM * (pickRow.params ? pickRow.params.unitWeightVol : 1)) * 10000) / 10000;
                pickRow.source = 'e.a x h|t x w/m3';
              } else {
                pickRow.value = Math.round((totalL * bM * hM * (pickRow.params ? pickRow.params.unitWeightVol : 1)) * 10000) / 10000;
                pickRow.source = 'e.l x b x h x w/m3';
              }
            } else {
              pickRow.value = Math.round(totalL * 10000) / 10000;
            }
            pickRow.note = count === 1 ? ('CAD ' + (entities[0].hand || entities[0].handle || 'obj')) : ('CAD ' + count + ' objects');
            pickRow.entity = entities[0];
          }

          const isFocusedInput = document.activeElement && document.activeElement.matches('input, textarea, select, [contenteditable="true"]');
          if (isFocusedInput) {
            const totEl = document.getElementById('detail-item-total');
            if (totEl) totEl.textContent = fmtNum(itemTotal(item)) + ' ' + item.unit;
            const rowValInp = document.querySelector('.measurement-row-panel .row-val-input');
            if (rowValInp && !rowValInp.matches(':focus')) rowValInp.value = fmtNum(pickRow.value);
            refreshCategoryTotals();
            return;
          }
          render();
        },
        acceptCadSelection: entities => {
          const res = getActiveItem();
          if(!res || !res.item) throw Error('Select or add a QTO item first.');
          const cat = res.cat;
          const item = res.item;
          if(!entities || entities.length === 0) throw Error('No objects selected in CAD');
          const count = entities.length;
          const totalL = entities.reduce((sum, ent) => sum + (Number(ent.l ?? ent.length) || 0), 0);
          const totalA = entities.reduce((sum, ent) => sum + (Number(ent.a ?? ent.area) || 0), 0);
          const totalV = entities.reduce((sum, ent) => sum + (Number(ent.v ?? ent.vol ?? ent.volume) || 0), 0);

          let pickRow = item.rows.find(r => r.mode === 'pick');
          if(!pickRow && (item.rows.length === 0 || (item.rows.length === 1 && (item.rows[0].value === 0 || !item.rows[0].value)))) {
            if(item.rows.length === 0) {
              pickRow = { id: uid('row'), value: 0, mode: 'pick', source: cat.sourceDefault, note: '' };
              item.rows.push(pickRow);
            } else {
              pickRow = item.rows[0];
              pickRow.mode = 'pick';
            }
          }

          let targetRow = pickRow;
          if(!targetRow || (targetRow.value > 0 && item.rows.length > 1)) {
            targetRow = { id: uid('cad'), value: 0, mode: 'pick', source: cat.sourceDefault, note: '' };
            item.rows.push(targetRow);
          }

          ensureRowModeState(targetRow, cat);
          if(targetRow.params) {
            targetRow.params.cadL = totalL;
            targetRow.params.cadA = totalA;
            targetRow.params.cadV = totalV;
          }

          const isAreaWall = cat.id === 'area' || cat.id === 'wall';
          const isVolume = cat.id === 'solid';
          const isWeight = cat.id === 'weight';
          const curPm = targetRow.params ? targetRow.params.pickMode : '';

          if(cat.id === 'length') {
            targetRow.value = Math.round(totalL * 10000) / 10000;
            targetRow.source = 'e.l';
          } else if(cat.id === 'no') {
            targetRow.value = count;
            targetRow.source = 'e.n';
          } else if(isAreaWall) {
            const hM = (targetRow.params && targetRow.params.dimUnitH === 'mm') ? (targetRow.params.cadH / 1000) : (targetRow.params ? targetRow.params.cadH : 1);
            const dedM = (targetRow.params && targetRow.params.deductDimUnit === 'mm') ? (targetRow.params.deductDim / 1000) : (targetRow.params ? targetRow.params.deductDim : 0);
            if(curPm === 'e.a') {
              targetRow.value = Math.round(Math.max(0, totalA - (targetRow.params ? targetRow.params.deductArea : 0)) * 10000) / 10000;
              targetRow.source = 'e.a';
            } else {
              targetRow.value = Math.round((totalL * Math.max(0, hM - dedM)) * 10000) / 10000;
              targetRow.source = 'e.l x h';
            }
          } else if(isVolume) {
            const bM = (targetRow.params && targetRow.params.dimUnitB === 'mm') ? (targetRow.params.cadB / 1000) : (targetRow.params ? targetRow.params.cadB : 1);
            const hM = (targetRow.params && targetRow.params.dimUnitH === 'mm') ? (targetRow.params.cadH / 1000) : (targetRow.params ? targetRow.params.cadH : 1);
            if(curPm === 'e.v') {
              targetRow.value = Math.round(totalV * 10000) / 10000;
              targetRow.source = 'e.v';
            } else if(curPm === 'e.a x h|t') {
              targetRow.value = Math.round((totalA * hM) * 10000) / 10000;
              targetRow.source = 'e.a x h|t';
            } else {
              targetRow.value = Math.round((totalL * bM * hM) * 10000) / 10000;
              targetRow.source = 'e.l x b x h';
            }
          } else if(isWeight) {
            const bM = (targetRow.params && targetRow.params.dimUnitB === 'mm') ? (targetRow.params.cadB / 1000) : (targetRow.params ? targetRow.params.cadB : 1);
            const hM = (targetRow.params && targetRow.params.dimUnitH === 'mm') ? (targetRow.params.cadH / 1000) : (targetRow.params ? targetRow.params.cadH : 1);
            if(curPm === 'e.l x w/m') {
              targetRow.value = Math.round((totalL * (targetRow.params ? targetRow.params.unitWeightLen : 1)) * 10000) / 10000;
              targetRow.source = 'e.l x w/m';
            } else if(curPm === 'e.v x w/m3') {
              targetRow.value = Math.round((totalV * (targetRow.params ? targetRow.params.unitWeightVol : 1)) * 10000) / 10000;
              targetRow.source = 'e.v x w/m3';
            } else if(curPm === 'e.a x h|t x w/m3') {
              targetRow.value = Math.round((totalA * hM * (targetRow.params ? targetRow.params.unitWeightVol : 1)) * 10000) / 10000;
              targetRow.source = 'e.a x h|t x w/m3';
            } else {
              targetRow.value = Math.round((totalL * bM * hM * (targetRow.params ? targetRow.params.unitWeightVol : 1)) * 10000) / 10000;
              targetRow.source = 'e.l x b x h x w/m3';
            }
          } else {
            targetRow.value = Math.round(totalL * 10000) / 10000;
          }
          targetRow.note = count === 1 ? ('CAD ' + (entities[0].hand || entities[0].handle || 'obj')) : ('CAD ' + count + ' objects');
          targetRow.entity = entities[0];
          render();
        },
        notify: showToast,`;

replace('      window.qtoUIState = {', injectedUIState);
replace('      function triggerFakeCadEvent() {', `      function triggerFakeCadEvent() {
        if(window.qtoDesktop){window.qtoDesktop.pickSelection();return;}`);
replace('      function getEntityContext(row) {', `      function getEntityContext(row) {
        if(row?.entity)return {...row.entity,n:1};`);
replace('function enter(p){document.body.classList.remove', 'function enter(p){if(window.qtoDesktop){window.qtoDesktop.openRecent(p);return;}document.body.classList.remove');
replace('function exportBeforeExit(){', 'function exportBeforeExit(){\n if(window.qtoDesktop){window.qtoDesktop.save();return;}');
replace('window.qtoSaveProjectUI=()=>{save();exportBeforeExit();};', 'window.qtoProjectUI={capture,apply};\nwindow.qtoSaveProjectUI=()=>{save();exportBeforeExit();};');
replace('let timer;button.onclick=()=>{try{window.qtoSaveProjectUI();', 'let timer;button.onclick=async()=>{try{await window.qtoSaveProjectUI();');
replace('function cad(){status.textContent=', 'function cad(){if(window.qtoDesktop){window.qtoDesktop.openDrawing([...selected]);return;}status.textContent=');
replace('row.ondblclick=cad;', "row.ondblclick=()=>{if(window.qtoDesktop)window.qtoDesktop.openDrawing([d.id]);else cad();};");
replace("dialog.querySelector('button').onclick=()=>dialog.close();dialog.querySelector('form').onsubmit=e=>{e.preventDefault();if(editing){", "dialog.querySelector('button').onclick=()=>dialog.close();dialog.querySelector('form').onsubmit=async e=>{e.preventDefault();if(window.qtoDesktop&&editing){try{await window.qtoDesktop.saveDrawingDescription(editing.id,dialog.querySelector('textarea').value);dialog.close();}catch(error){status.textContent=String(error);}return;}if(editing){");
replace("document.addEventListener('qto-project-open',load);load();\nconst bar=document.getElementById('category-filter-bar');const toggle=document.createElement('button');toggle.id='btn-category-drawings';", "window.qtoDrawingUI={restore:entries=>{drawings=entries.map(d=>({...d,note:d.short||'',addedAt:d.latest_entry?.created_at||new Date().toISOString(),size:0}));selected.clear();render();},selected:()=>[...selected]};\ndocument.addEventListener('qto-project-open',load);load();\nconst bar=document.getElementById('category-filter-bar');const toggle=document.createElement('button');toggle.id='btn-category-drawings';");
replace('</body>', '<script type="module" src="/src/main.jsx"></script></body>');
writeFileSync(new URL('index.html', root), html);
