# AutoCAD ObjectARX JavaScript Runtime — Comprehensive Module Documentation

This document provides a complete reference and code examples for all JavaScript modules available inside the **`arx01`** AutoCAD ObjectARX plugin.

The environment runs on an embedded **QuickJS (ES2020+)** runtime with direct native hooks into AutoCAD's C++ database (`AcDbDatabase`), editor (`AcEditor`), transaction manager, and Windows platform APIs.

---

## Table of Contents

1. [Runtime Overview & Commands](#1-runtime-overview--commands)
2. [Global Utilities & Built-ins](#2-global-utilities--built-ins)
3. [AutoCAD CAD Modules](#3-autocad-cad-modules)
   - [`entity` — Entity Query, Selection & Modification](#entity-module)
   - [`editor` — Selection Prompts & Layer Automation](#editor-module)
   - [`layer` — Layer Creation & Filter Management](#layer-module)
   - [`color` — Color Conversion & Color Spaces](#color-module)
   - [`tm` / `transaction` — Database Transaction Manager](#tm-module)
4. [Data & Storage Modules](#4-data--storage-modules)
   - [`sqlite` — Synchronous SQLite Database Engine](#sqlite-module)
   - [`excel` — COM Automation with Microsoft Excel](#excel-module)
   - [`msgpack` — High-Performance Binary Serialization](#msgpack-module)
5. [System, I/O & Networking Modules](#5-system-io--networking-modules)
   - [`fs` & `fs/promises` — File System](#fs-module)
   - [`path` — File Path Utilities](#path-module)
   - [`process` — Process Environment & Clocks](#process-module)
   - [`net` — TCP Sockets & Networking](#net-module)
   - [`dialog` — WebView2 HTML Dialogs](#dialog-module)
   - [`console` — AutoCAD Console Logging & Interactive Input](#console-module)
6. [Complete Workflow Examples](#6-complete-workflow-examples)
7. [Safe Transaction Commands (SS-SET, SS-GET, E-SET, E-GET, E-SHOW, SS-QUERY)](#7-safe-transaction-commands)

---

## 1. Runtime Overview & Commands

Once `arx01.arx` is loaded into AutoCAD via `APPLOAD`, the following AutoCAD commands are registered:

| Command | Description |
|---|---|
| `js:repl` | Launches an interactive read-eval-print loop inside the AutoCAD text window. |
| `js:eval` | Prompts for a single line of JavaScript to evaluate directly. |
| `js:load` | Prompts for a `.js` file path and executes it. |
| `js:server` | Starts/stops the background TCP IPC JSON/Msgpack socket server (default port `8123`). |
| `HELLOARX` | Verification command printing test output. |
| `SS-SET` | Transaction-safe batch mutation across AutoCAD selection set with atomic rollback. |
| `E-SET` | Transaction-safe single entity mutation with atomic rollback. |
| `E-GET` | Safe entity query returning detached JSON snapshot. |
| `SS-GET` | Safe selection set query returning detached snapshots array. |
| `SS-QUERY` | Safe selection set query aggregation returning evaluated result. |
| `E-SHOW` | Zooms viewport to entity geometric extents and sets implied selection with grips. |

### AutoLISP Integration

The plugin registers native AutoLISP functions and provides [`lisp/runjs.lsp`](../lisp/runjs.lsp):

| AutoLISP Function / Command | Description |
|---|---|
| `(js-eval "exprOrFile")` | Evaluates JS expression or script file and **returns the JSON value** string. |
| `(js-execute "codeOrFile")` | Executes JS code or script file with **no return** (`acedRetVoid()`). |
| `(ss-set "<options-or-patch-json>")` | Batch mutates entities within owned database transaction; full atomic rollback on any failure. Returns JSON status string. |
| `(e-set "<target-handle-or-ename>" "<patch-json>")` | Mutates single entity within owned database transaction. Returns JSON snapshot. |
| `(e-get ["<target-handle-or-ename>"])` | Queries single entity properties and XData. Prompts if omitted. Returns JSON snapshot string. |
| `(ss-get ["<options-or-filter-json>"])` | Queries selection set entities (detached read snapshots). Returns JSON array string. |
| `(ss-query "<query-options-json>")` | Aggregates and queries selection set. Returns JSON result. |
| `(e-show ["<target-handle-or-ename>"])` | Zooms to entity bounding extents and activates implied selection. Returns `T` on success, `nil` on failure. |
| `COMMAND: JS-EVAL` | Prompts for JS expression/file in command line and prints returned JSON value. |
| `COMMAND: JS-EXECUTE` / `RUNJS` | Prompts for file/code to execute quietly without return. |
| `COMMAND: SS-SET` / `E-SET` / `E-GET` / `SS-GET` / `SS-QUERY` / `E-SHOW` | Interactive AutoCAD CLI commands for safe database access. |

Example in AutoLISP:
```lisp
;; Evaluate expression: returns JSON string
(setq jsonRes (js-eval "({ handle: '2B1A', width: 250, height: 500 })"))
;; jsonRes => "{\"handle\":\"2B1A\",\"width\":250,\"height\":500}"

;; Evaluate query directly:
(setq layer (js-eval "entity.pick().lyr"))
;; layer => "\"STR_BEAM\""

;; Execute code or script file: NO RETURN (void)
(js-execute "entity.set({ clr: 1 }, 12345)")
(js-execute "C:/scripts/setup_layers.js")

;; Safe Transaction Commands in AutoLISP:
;; 1. Query single entity:
(setq entData (e-get))

;; 2. Zoom & highlight entity:
(e-show "2B1A")

;; 3. Single entity mutation:
(e-set "2B1A" "{ clr: 1, visible: true }")

;; 4. Safe atomic batch mutation across selection:
(ss-set "{ filter: e => e.ty == 'AcDbLine', set: e => ({ clr: 2 }) }")
```

### Module Loading

All native modules are pre-registered globally as well as accessible via CommonJS `require()` and ES dynamic `import()`:

```javascript
// Globals are available directly:
console.log(entity, layer, fs, path, sqlite);

// Or via CommonJS require:
const fs = require('fs');
const sqlite = require('sqlite');
const { DatabaseSync } = require('sqlite');

// Or loading external JS files:
await load('C:/scripts/my_tool.js');            // cached
await load('C:/scripts/my_tool.js', false);     // reloads without cache
```

---

## 2. Global Utilities & Built-ins

Global utility functions injected into `globalThis`:

### `load(path, options?)`
Asynchronously loads and executes a JavaScript file.
- `path` *(string)*: Absolute or relative file path.
- `options` *(boolean | { cache: boolean })*: Defaults to `true`. If `false`, executes fresh without module caching.

```javascript
// Run external script
await load('./generate_grids.js');

// Force reload after edits
await load('./generate_grids.js', false);
```

### `round(value, decimals = 0)`
Accurate mathematical rounding.
```javascript
round(123.4567, 2); // 123.46
round(123.4567, 0); // 123
```

### `clone(value)`
Performs a deep copy of arrays and objects.
```javascript
const original = { name: 'Beam', dims: { w: 200, h: 400 } };
const copy = clone(original);
copy.dims.w = 250;
console.log(original.dims.w); // 200
```

### `merge(target, source)`
Deeply merges the source object properties into target.
```javascript
const target = { a: 1, b: { c: 2 } };
merge(target, { b: { d: 3 } });
// target becomes: { a: 1, b: { c: 2, d: 3 } }
```

### `inspect(value, formatOrOptions?)`
Stringifies any value with formatting: `'pretty'`, `'compact'`, or `'yaml'`.
```javascript
inspect({ x: 10, y: 20 }, 'yaml');
```

---

## 3. AutoCAD CAD Modules

### `entity` Module
The core CAD entity module. Used to select, query, read, and mutate drawing objects in real time.

#### Methods

#### `entity.pick(options?)`
Prompts user to pick a single entity on screen (`acedEntSel`).
- `options` *(string | object)*:
  - If string: used as prompt (e.g. `"\nPick a beam: "`).
  - If object:
    - `prompt` *(string)*: Prompt message.
    - `filter` *(function(e): boolean)*: Predicate to filter acceptable entities.
    - `out` *(function(e): any)*: Transform function mapping the entity object before returning.
    - `set` *(function(e): object)*: Mutation patch applied immediately to the entity upon picking.
- **Returns**: The entity object (or transformed result), or `null` if canceled.

```javascript
// Simple pick
const ent = entity.pick('\nSelect any curve: ');
console.log('Class:', ent.ty, 'Length:', ent.l);

// Pick with immediate color highlighting and custom return
const selectedId = entity.pick('\nPick target column: ', {
  filter: e => e.ty === 'AcDbPolyline' || e.ty === 'AcDbLine',
  set: e => ({ clr: 1 }), // change color to red immediately
  out: e => e.id
});
```

#### `entity.select(options?)`
Prompts the user to make a standard AutoCAD window/crossing/fence selection set (`acedSSGet`). Automatically supports implied selection (PickFirst) if objects are pre-selected in the drawing.
- `options` *(string | object)*:
  - `prompt` *(string)*: Display prompt before selection.
  - `filter` *(function(e): boolean)*: Filter matching entities.
  - `out` *(function(e): any)*: Transform function applied to each item.
  - `set` *(function(e): object)*: Mutation patch applied to each item in the selection.
  - `implied` *(boolean)*: Whether to check PickFirst implied selection first (default: `true`).
  - `onlyImplied` *(boolean)*: Only capture pre-selected objects without prompt (default: `false`).
- **Returns**: Array of entity descriptors.

```javascript
// Select all entities on layer "BEAM", change color to Green (3), and return IDs
const beamIds = entity.select('\nSelect beams: ', {
  filter: e => e.lyr === 'BEAM',
  set: e => ({ clr: 3 }),
  out: e => e.id
});
console.log('Selected beam count:', beamIds.length);
```

#### `entity.get(idOrHandle | array, filter?, out?)`
Retrieves entity descriptor by numeric ID or hex handle string, or an array of them.
```javascript
// Single lookup by ID or Handle
const e1 = entity.get(beamIds[0]);
const e2 = entity.get('2B4A'); // Hex handle string

// Batch lookup with filtering and mapping
const texts = entity.get(allIds, e => e.ty === 'AcDbText', e => e.txt);
```

#### `entity.set(patch, targetIds | targetEntities)`
Applies property modifications to entities inside an automatic write transaction.
- `patch` *(object | function(e): object)*:
  - `clr`: Color index (0-256), color name, or `{ r, g, b }`.
  - `lyr`: Target layer name string.
  - `visible`: `true` or `false`.
  - `trp`: Transparency (0.0 to 100.0).
  - `txt`: New text string (for `AcDbText`, `AcDbMText`, or block attributes).
  - `data`: Custom JavaScript object/array stored transparently into entity **XData** via MsgPack!
- `targetIds`: Single ID/handle, entity object, or array of them.

```javascript
// Hide entities and attach custom data
entity.set(
  {
    visible: false,
    clr: 6, // Magenta
    data: { mark: 'B-101', rebar: '4-D16', verified: true }
  },
  beamIds
);

// Read attached XData back anytime:
const updated = entity.get(beamIds[0]);
console.log(updated.data.mark); // "B-101"
```

#### Entity Object Schema
When queried, an entity returns a comprehensive plain JavaScript descriptor:

| Field | Type | Description |
|---|---|---|
| `id` | `number` | Numeric entity identifier. |
| `hand` | `string` | Hex handle string (e.g. `"1C9"`). |
| `ty` | `string` | AutoCAD C++ class name (e.g. `AcDbLine`, `AcDbPolyline`, `AcDbText`, `AcDbBlockReference`, `AcDbHatch`). |
| `lyr` | `string` | Layer name. |
| `clr` | `object` / `number` | Color descriptor (`{ r, g, b }` or index). |
| `trp` | `number` | Transparency percentage (0.0 - 100.0). |
| `visible` | `boolean` | Visibility state. |
| `data` | `any` | Decoded custom XData object. |
| `l` | `number` | Curve length (for Lines, Polylines, Arcs, Splines). |
| `sp` | `[x, y, z]` | Curve start point coordinates. |
| `ep` | `[x, y, z]` | Curve end point coordinates. |
| `area` | `number` | Enclosed area (for closed Polylines, Hatches, Regions). |
| `txt` | `string` | Text content (for `AcDbText`, `AcDbMText`). |
| `h` | `number` | Text height. |
| `pos` | `[x, y, z]` | Insertion position point. |
| `blk` | `string` | Block name (for `AcDbBlockReference`). |
| `rot` | `number` | Block rotation angle in radians. |
| `scale` | `[sx, sy, sz]` | Block reference scale vector. |
| `attrs` | `object` | Key-value dictionary of block attribute tags and values. |

---

### `editor` Module
High-level AutoCAD editor utilities.

#### Methods

#### `editor.getString(prompt?, options?)`
Prompts user to enter a text string at the AutoCAD command line (`acedGetString`).
- `prompt` *(string)*: Command line prompt message.
- `options` *(object | boolean)*:
  - If boolean: sets `allowSpaces` (default `true`).
  - If object:
    - `prompt` *(string)*: Prompt text (default `"\nEnter string: "`).
    - `allowSpaces` *(boolean)*: If `true` (default), spaces are allowed and Enter confirms input; if `false`, spaces terminate input.
    - `default` *(string)*: Fallback value if the user presses Enter without typing.
- **Returns**: `string` with entered text, or `null` if canceled with ESC.

```javascript
// Simple prompt
const name = editor.getString('\nEnter project name: ');

// With default and options
const layerName = editor.getString('\nEnter target layer <BEAM>: ', {
  allowSpaces: false,
  default: 'BEAM'
});
```

#### `editor.getPoint(prompt?, basePoint?, options?)`
Prompts the user to pick a 3D point in the drawing viewport (`acedGetPoint`).
- `prompt` *(string)*: Command line prompt message (default `"\nSpecify point: "`).
- `basePoint` *(Array\<number\> | object)*: Optional base reference point for rubber-band line (e.g. `[x, y, z]` or `{ x, y, z }`).
- `options` *(object)*: `{ prompt: "...", basePoint: [...] }`.
- **Returns**: A 3D point array `[x, y, z]` with convenient `.x`, `.y`, and `.z` property getters, or `null` if canceled.

```javascript
// Pick single point
const pt1 = editor.getPoint('\nSpecify start point: ');
if (pt1) {
  console.log(`Picked: X=${pt1.x}, Y=${pt1.y}, Z=${pt1.z}`);

  // Pick second point with rubber-band line from pt1
  const pt2 = editor.getPoint('\nSpecify end point: ', pt1);
  if (pt2) {
    const dist = Math.hypot(pt2.x - pt1.x, pt2.y - pt1.y);
    console.log('Distance:', dist);
  }
}
```

#### `editor.getNumber(prompt?, options?)`
Prompts user to enter a number in the command line (`acedGetReal` or `acedGetInt`).
- `prompt` *(string)*: Command line prompt message.
- `options` *(object | number)*:
  - If number: sets `default` number.
  - If object:
    - `prompt` *(string)*: Prompt text (default `"\nEnter number: "`).
    - `default` *(number)*: Default value if user presses Enter without typing.
    - `integer` *(boolean)*: If `true`, prompts for an integer (`acedGetInt`); if `false` (default), prompts for a floating-point real (`acedGetReal`).
- **Returns**: `number` (or `null` if canceled).

```javascript
// Floating-point real
const width = editor.getNumber('\nEnter width <250.0>: ', 250.0);

// Integer
const count = editor.getNumber('\nEnter item count: ', {
  integer: true,
  default: 1
});
```

#### `editor.getValue(prompt?, options?)`
Prompts user for input in the console and evaluates it directly into a **JavaScript value** (objects, arrays, numbers, booleans, strings).
- `prompt` *(string)*: Command line prompt message.
- `options` *(object)*:
  - `prompt` *(string)*: Prompt message (default `"\nEnter value: "`).
  - `default` *(any)*: Fallback JavaScript value if user presses Enter.
- **Returns**: Evaluated JavaScript value (e.g. Object, Array, Number, Boolean, or String), or `null` if canceled.

```javascript
// User types: { width: 300, height: 600 }
const dims = editor.getValue('\nEnter dimensions object: ', {
  default: { width: 250, height: 500 }
});
console.log('Width:', dims.width, 'Height:', dims.height);

// User types: [100, 200, 300]
const arr = editor.getValue('\nEnter coordinates array: ');

// User types: 45 * 2
const angle = editor.getValue('\nEnter angle: '); // 90
```

#### `editor.select(prompt?, selectionMode = 'single', filterFn?, implied = true)`
Selects one or more drawing entities. Supports AutoCAD **implied selection** (PickFirst noun-verb selection) out of the box.
- `prompt` *(string)*: Console prompt text (default `"Select an entity: "`).
- `selectionMode` *(string)*:
  - `'single'`: Selects a single entity (`acedEntSel` or first match from implied selection).
  - `'multiple'`: Selects multiple entities (`acedSSGet`).
  - `'implied'`: Only grabs entities pre-selected before running the command; does not prompt if nothing is selected.
- `filterFn` *(function(e): boolean)*: Optional predicate filter.
- `implied` *(boolean)*: Whether to check implied (PickFirst) selection first (default: `true`). Set to `false` to force interactive prompt.

Alternatively, pass a configuration object:
`editor.select({ prompt, mode, filter, implied, onlyImplied })`

**Implied Selection (PickFirst) Behavior:**
If objects are already selected in AutoCAD before calling `editor.select`, it automatically captures the selection set (`acedSSGet("I")`), clears the pickfirst grips, and processes them immediately without prompting. If nothing is pre-selected, it interactively prompts the user.

```javascript
// 1. Multiple selection with implied selection + type filter:
const lines = editor.select('Select boundaries: ', 'multiple', e => e.ty === 'AcDbLine');

// 2. Single selection:
const poly = editor.select('Select polyline: ', 'single', e => e.ty === 'AcDbPolyline');

// 3. Options object format:
const beams = editor.select({
  prompt: 'Select beams: ',
  mode: 'multiple',
  filter: e => e.lyr === 'BEAM',
  implied: true // default true
});

// 4. Implied only (grab currently selected items, return [] without prompt if none):
const preselected = editor.select('', 'implied');
```

#### `editor.createLayers(layerSpecs?)`
Creates or updates layers from an array of layer specification objects. If omitted, creates default workspace layers.
```javascript
const result = editor.createLayers([
  { lyr: 'STR_BEAM', clr: 3, description: 'Structural Beams' },
  { lyr: 'STR_COL', clr: 1, description: 'Structural Columns' }
]);
console.log('Created layers:', result.createdLayers);
```

#### `editor.createQsLayers()`
Installs the built-in standard Quantity Survey / Engineering layer scheme.
```javascript
editor.createQsLayers();
```

---

### Transaction-Safe Commands (`editor` & `entity`)

These methods strictly implement the **Section 7 Transaction Safety Overrides** for safe AutoCAD database transactions:
- **RAII Transaction Guard**: Transitions strictly through `NOT_STARTED -> ACTIVE -> COMMITTED / ABORTED`. Only aborts transactions owned by the active command; never corrupts caller transaction stacks.
- **Input Before Transaction**: All interactive selection prompts (`acedSSGet`, `acedEntSel`, implied pickfirst) run and normalize into candidate `AcDbObjectId`s **before** starting the database transaction. Database objects are never held open while waiting for user input. Selection failure or user cancellation exits without starting transactions or invoking `init`/`post`.
- **Database Atomicity (All-or-Nothing)**: In `ssSet` and `eSet`, either all candidate entity and XData updates commit cleanly, or all updates abort and roll back completely. Never commits per entity.
- **Automatic Layer Creation**: If a target layer does not exist in the drawing layer table, it is automatically created inside the same database transaction (`addNewlyCreatedDBRObject`) using the last used color (or patch `clr`, entity color, or entity's current layer color), ensuring atomic rollback if the operation fails.
- **RegApp Transaction Isolation**: If custom data (`data`) is written, `kEntityDataRegApp` (`"G"`) is registered inside the same database transaction via `addNewlyCreatedDBRObject`.
- **Callback Isolation**: Callbacks (`filter`, `set`, `init`, `post`) receive detached immutable snapshot objects. Processing counters (`processedCount`) are tracked strictly in native C++ memory.
- **Safe Viewport & Implied Selection**: In `eShow`, database access is closed *before* viewport zooming (`acedCommandS`) and setting implied selection (`acedSSSetFirst`).

Available under both `editor.*` and `entity.*` namespaces:

#### `editor.ssSet(options)` / `entity.ssSet(options)`
Batch mutates multiple entities across a selection set within a single owned transaction with atomic rollback.

- `options` *(object)*:
  - `set` *(function(e): patch | object)*: Required. Returns property patch or static patch object.
  - `filter` *(function(e): boolean)*: Optional predicate filter applied to each entity snapshot.
  - `init` *(function({ count }))*: Optional callback executed once inside the transaction before candidates are iterated.
  - `post` *(function({ processedCount, matchedCount }))*: Optional callback executed once inside transaction after all updates succeed.
  - `prompt` *(string)*: Optional console prompt when interactive selection is needed.
  - `entities` *(array)*: Optional pre-defined array of handles or IDs. If omitted, checks implied (pickfirst) selection or prompts user.

```javascript
// Example: Safe atomic batch update of beam lines
const result = editor.ssSet({
  prompt: 'Select lines to update: ',
  filter: e => e.ty === 'AcDbLine',
  set: e => ({
    clr: 2, // Yellow
    visible: true,
    data: { inspected: true, batchId: "B-2026-10" }
  }),
  init: ctx => console.log(`Starting update on ${ctx.count} candidates...`),
  post: ctx => console.log(`Finished: ${ctx.processedCount} entities updated.`)
});

console.log(result);
// => { status: "ok", committed: true, processedCount: 14, matchedCount: 14, total: 14 }
```

#### `editor.eSet(target, patchOrFunction)` / `entity.eSet(target, patchOrFunction)`
Mutates a single entity within an owned database transaction with atomic rollback.

- `target` *(string | number | object)*: Entity handle, ID, or snapshot.
- `patchOrFunction` *(object | function(e): patch)*: Update dictionary or function returning update dictionary.

Supported patch properties:
- `clr`: AutoCAD color index (`0..256`) or `{ r, g, b }` (`0..255`).
- `trp`: Transparency (`-2` ByLayer, `-1` ByBlock, `0..100` percentage).
- `visible`: `true` or `false`.
- `lyr`: Target layer name (must exist in drawing; missing layer fails validation).
- `text`: Text string (for `AcDbText` or `AcDbMText`).
- `attr`: Attribute dictionary `{ TAG: "value" }` (for `AcDbBlockReference`).
- `data`: Arbitrary JavaScript object/primitive encoded as MessagePack into XData (`null` removes XData).

```javascript
const updated = editor.eSet('2B1A', {
  clr: 1, // Red
  lyr: '0',
  data: { status: 'approved', reviewer: 'Heru' }
});
```

#### `editor.eGet(target?, outFn?)` / `entity.eGet(target?, outFn?)`
Queries single entity properties and XData (detached read snapshot).
- `target` *(string | number | object, optional)*: Handle or ID. If omitted, interactively prompts user (`acedEntSel`).
- `outFn` *(function(snapshot): any, optional)*: Optional projection transform.

```javascript
// Interactive pick:
const snap = editor.eGet();

// Specific handle:
const line = editor.eGet('2B1A', e => ({
  hand: e.hand,
  length: e.length,
  color: e.clr
}));
```

#### `editor.ssGet(options?)` / `entity.ssGet(options?)`
Queries selection set entities as detached read snapshots.
- `options` *(object)*:
  - `filter` *(function(e): boolean)*: Optional predicate filter.
  - `out` *(function(e): any)*: Optional projection mapping.
  - `prompt` *(string)*: Optional selection prompt.
  - `entities` *(array)*: Optional list of handles/IDs.

```javascript
const lines = editor.ssGet({
  filter: e => e.ty === 'AcDbLine' && e.lyr === 'WALLS',
  out: e => ({ hand: e.hand, length: e.length })
});
```

#### `editor.ssQuery(options)` / `entity.ssQuery(options)`
Evaluates a read-only query over a selection set.
- `options` *(object)*:
  - `query` *(function(snapshots[]): any)*: Required query/aggregation function.
  - `filter` *(function(e): boolean)*: Optional filter.

```javascript
const summary = editor.ssQuery({
  query: entities => {
    let totalLength = 0;
    for (const e of entities) {
      if (e.length) totalLength += e.length;
    }
    return { count: entities.length, totalLength };
  }
});
```

#### `editor.eShow(target?)` / `entity.show(target?)`
Queries target entity, closes database access, zooms the viewport to the entity geometric bounding extents (with margin padding), and safely activates implied selection with grips.

- `target` *(string | number | object, optional)*: Handle or ID. Prompts with `acedEntSel` if omitted.

```javascript
// Zooms viewport directly to entity and highlights with grips:
editor.eShow('2B1A');
```

---

### `layer` Module
Direct database layer table manipulation and AutoCAD Layer Filter Manager integration.

#### Methods

#### `layer.createLayer(layersArray)`
Creates or updates AutoCAD layers.
- `layersArray` *(Array\<LayerObject\>)*:
  - `lyr` *(string, required)*: Layer name.
  - `clr` *(string | number | object)*: Color name (e.g. `'red'`), hex (`'#00ff00'`), ACI number (1-255), or RGB object `{ r: 255, g: 0, b: 0 }`.
  - `trp` *(number)*: Transparency (0.0 = fully opaque, 100.0 = fully transparent).
  - `frozen` *(boolean)*: Layer frozen state.
  - `locked` *(boolean)*: Layer locked state.

```javascript
layer.createLayer([
  { lyr: 'WALLS', clr: '#ff5500', locked: false },
  { lyr: 'DOORS', clr: 'cyan', trp: 20.0 },
  { lyr: 'HIDDEN_REF', clr: 8, frozen: true }
]);
```

#### `layer.updateLayerFilter(layersList)`
Updates or creates AutoCAD layer filters matching the provided layer specifications.
```javascript
layer.updateLayerFilter(['WALLS', 'DOORS']);
```

---

### `color` Module
Comprehensive color conversion routines supporting standard CAD, CSS, and Material Design color spaces.

#### Methods

```javascript
// Parsing:
const rgb = color.parse('#3498db');      // { r: 52, g: 152, b: 219 }
const aci = color.parse('red');          // { r: 255, g: 0, b: 0 }
const fromIndex = color.parse(1);        // AutoCAD Color Index 1 (Red)

// Conversions between color models:
// HSL <-> RGB
const rgbFromHsl = color.hslToRgb({ h: 200, s: 70, l: 50 });
const hsl = color.rgbToHsl(rgbFromHsl);

// HSV <-> RGB
const hsv = color.rgbToHsv({ r: 255, g: 128, b: 0 });
const rgbFromHsv = color.hsvToRgb(hsv);

// CMYK <-> RGB
const cmyk = color.rgbToCmyk({ r: 100, g: 200, b: 150 });
const rgbFromCmyk = color.cmykToRgb(cmyk);

// Material Design HCT (Hue, Chroma, Tone) <-> RGB
const hct = color.rgbToHct({ r: 64, g: 128, b: 255 });
const rgbFromHct = color.hctToRgb({ h: 250, c: 45, t: 60 });

// Alpha Blending
const composite = color.rgbaToRgb({ r: 255, g: 0, b: 0, a: 0.5 }, { r: 255, g: 255, b: 255 });
```

---

### `tm` Module
Controls explicit AutoCAD database transactions (`AcDbTransactionManager`).

When using `entity.set()`, transactions are managed automatically. Use `tm` when grouping complex multi-step database mutations into a single atomic undo/redo step.

#### Methods

```javascript
tm.start();
try {
  // Database operations
  const ent = entity.pick();
  entity.set({ clr: 1 }, ent.id);
  
  tm.update(); // Flushes graphics update to screen
  tm.commit(); // Commits changes to the database
} catch (err) {
  tm.abort();  // Rolls back all changes
  throw err;
}
```

---

## 4. Data & Storage Modules

### `sqlite` Module
A full SQLite database engine embedded into AutoCAD, implementing Node.js standard `DatabaseSync` API.

#### Methods

#### `new sqlite.DatabaseSync(filename, options?)`
Opens an SQLite database file. Use `':memory:'` for an in-memory database.
- `filename` *(string)*: Path to `.db` / `.sqlite` file, or `':memory:'`.
- `options` *(object)*: `{ open: boolean, readOnly: boolean }`.

```javascript
const { DatabaseSync } = require('sqlite');

const db = new DatabaseSync('C:/temp/cad_project.db');

// Run DDL scripts
db.exec(`
  CREATE TABLE IF NOT EXISTS elements (
    id INTEGER PRIMARY KEY,
    handle TEXT UNIQUE,
    category TEXT,
    length REAL
  );
`);

// Prepared statement: run (INSERT / UPDATE / DELETE)
const insertStmt = db.prepare('INSERT INTO elements (handle, category, length) VALUES (?, ?, ?)');
const info = insertStmt.run('1A2F', 'Beam', 4250.5);
console.log('Inserted row id:', info.lastInsertRowid, 'Changes:', info.changes);

// Prepared statement: get (single record)
const queryStmt = db.prepare('SELECT * FROM elements WHERE handle = ?');
const record = queryStmt.get('1A2F');
console.log('Record:', record.category, record.length);

// Prepared statement: all (batch records)
const allStmt = db.prepare('SELECT * FROM elements WHERE length > ?');
const rows = allStmt.all(3000.0);
console.log('Found rows:', rows.length);

// Prepared statement: iterate (cursor generator)
const iterStmt = db.prepare('SELECT handle, length FROM elements');
for (const row of iterStmt.iterate()) {
  console.log(`Handle: ${row.handle}, Length: ${row.length}`);
}

db.close();
```

---

### `excel` Module
Direct COM automation integration with Microsoft Excel (`Excel.Application`).

#### Methods

#### `excel.writeTable(rows, headers?)`
Writes an array of JavaScript objects as a formatted table into the active worksheet in Excel (or opens a new workbook if none is open).
- `rows` *(Array\<object\>)*: Array of row objects.
- `headers` *(Array\<string\>, optional)*: Explicit column order and headers.

```javascript
const beams = [
  { Mark: 'B1', Story: 'Level 1', Width: 250, Height: 500, Length: 6000 },
  { Mark: 'B2', Story: 'Level 1', Width: 300, Height: 600, Length: 7500 },
  { Mark: 'B3', Story: 'Level 2', Width: 250, Height: 450, Length: 6000 }
];

// Writes directly into Excel
excel.writeTable(beams);

// With custom column ordering:
excel.writeTable(beams, ['Story', 'Mark', 'Length', 'Width', 'Height']);
```

#### `excel.readTable()`
Reads the currently highlighted selection in the active Excel worksheet and parses it into an array of JavaScript objects. First selected row is automatically used as property keys.
```javascript
// User highlights cells in Excel, then runs:
const data = excel.readTable();
for (const row of data) {
  console.log(`Processing: ${row.Mark} on ${row.Story}`);
}
```

---

### `msgpack` Module
High-performance MessagePack encoder/decoder for compact binary serialization.

```javascript
const data = {
  project: 'Building A',
  floors: 12,
  coordinates: [120.5, 450.2, 0.0],
  tags: ['structural', 'reinforced']
};

// Encode JS object to Uint8Array binary
const binary = msgpack.encode(data);
console.log('Encoded byte length:', binary.length);

// Decode binary back to JS object
const decoded = msgpack.decode(binary);
console.log('Decoded project:', decoded.project);
```

---

## 5. System, I/O & Networking Modules

### `fs` Module
Node.js-compatible file system supporting synchronous methods and `fs.promises`.

```javascript
const fs = require('fs');

// Synchronous operations:
fs.writeFileSync('C:/temp/notes.txt', 'AutoCAD ARX Session Export\n', 'utf8');
fs.appendFileSync('C:/temp/notes.txt', 'Second entry\n');

if (fs.existsSync('C:/temp/notes.txt')) {
  const content = fs.readFileSync('C:/temp/notes.txt', 'utf8');
  console.log('File Content:\n', content);
}

// Directory operations:
fs.mkdirSync('C:/temp/cad_output', { recursive: true });
const files = fs.readdirSync('C:/temp');
console.log('Files count:', files.length);

// Asynchronous Promises API:
const fsp = require('fs/promises'); // or fs.promises
await fsp.writeFile('C:/temp/async.json', JSON.stringify({ cad: true }));
const data = await fsp.readFile('C:/temp/async.json', 'utf8');
```

---

### `path` Module
Node.js-compatible path manipulation. Both Windows (`path.win32`) and POSIX (`path.posix`) paths are supported.

```javascript
const path = require('path');

const full = path.join('C:', 'Projects', 'CAD', 'drawings', 'dwg1.dwg');
console.log(full); // "C:\Projects\CAD\drawings\dwg1.dwg"

console.log(path.basename(full));          // "dwg1.dwg"
console.log(path.basename(full, '.dwg'));   // "dwg1"
console.log(path.dirname(full));           // "C:\Projects\CAD\drawings"
console.log(path.extname(full));           // ".dwg"

const parsed = path.parse(full);
// { root: 'C:\', dir: 'C:\Projects\CAD\drawings', base: 'dwg1.dwg', ext: '.dwg', name: 'dwg1' }
```

---

### `process` Module
Inspects runtime process state and timing.

```javascript
const process = require('process');

console.log('Process PID:', process.pid);
console.log('Platform:', process.platform); // "win32"
console.log('Current working directory:', process.cwd());

// High-resolution timer for benchmarking CAD algorithms:
const start = process.hrtime.bigint();
// ... run CAD entity analysis ...
const end = process.hrtime.bigint();
console.log(`Elapsed: ${Number(end - start) / 1e6} ms`);

// Write directly to stdout/stderr:
process.stdout.write('Direct stdout stream message\n');
```

---

### `net` Module
Synchronous/asynchronous TCP client and server networking for IPC with external tools, Python scripts, or dashboards.

```javascript
const net = require('net');

// Create a TCP Client:
const client = net.connect({ host: '127.0.0.1', port: 9000 });
client.write('PING from AutoCAD\n');
const response = client.read();
console.log('Server reply:', response);
client.close();

// Create a TCP Server inside AutoCAD:
const server = net.listen(8124, '127.0.0.1');
console.log('AutoCAD TCP server listening on port 8124');

// Accept incoming client:
const incomingSocket = server.accept();
if (incomingSocket) {
  const req = incomingSocket.read();
  console.log('Received:', req);
  incomingSocket.write('ACK\n');
  incomingSocket.close();
}
server.close();
```

---

### `dialog` Module
Renders responsive HTML/CSS/JS user interfaces inside AutoCAD using Microsoft Edge **WebView2**.

#### Methods

- `dialog.input(htmlFileName)`: Opens `<htmlFileName>.html` located in the script working directory.
- `dialog.input_json(initialData, htmlFileName?)`: Passes `initialData` into the dialog via `window.chrome.webview` and returns user submission.

```javascript
// Launch custom HTML UI and wait for user submission
const result = dialog.input_json(
  { defaultLayer: 'STR_BEAM', defaultPrefix: 'BM-' },
  'beam_generator' // loads beam_generator.html
);

if (result) {
  console.log('User entered:', result.beamName, result.width, result.height);
}
```

---

### `console` Module
Manages output to AutoCAD command line and interactive text prompt input.

#### Methods

- `console.log(...args)`: Prints arguments to AutoCAD's command line history.
- `console.inspect(val, format)`: Formats objects with colors/YAML/compact layout.
- `console.read(options)`: Interactive prompt input from the command line.

```javascript
console.log('Standard log message');

// Prompt user for input from command line:
const prefix = console.read({ prompt: '\nEnter beam prefix <B>: ' });
const count = console.read({ prompt: '\nEnter count: ', type: 'json' });
```

---

## 6. Complete Workflow Examples

### Example 1: Extract Beam Quantities & Export to Excel

This script prompts the user to select beam polylines/lines, extracts their geometric length, attaches quantity surveyor metadata into AutoCAD's database, and writes an Excel schedule:

```javascript
// extract_beams.js
async function exportBeamSchedule() {
  console.log('\n--- Beam Quantity Surveyor Extractor ---');
  
  // 1. Interactive selection with live color feedback
  const beams = entity.select('\nSelect beam entities: ', {
    filter: e => e.ty === 'AcDbLine' || e.ty === 'AcDbPolyline',
    set: e => ({ clr: 3 }), // Highlight selected in Green
    out: e => ({
      Handle: e.hand,
      Layer: e.lyr,
      Length_mm: round(e.l, 1),
      Length_m: round(e.l / 1000.0, 2),
      Start_X: round(e.sp[0], 2),
      Start_Y: round(e.sp[1], 2),
      End_X: round(e.ep[0], 2),
      End_Y: round(e.ep[1], 2)
    })
  });

  if (!beams || beams.length === 0) {
    console.log('\nNo beams selected.');
    return;
  }

  console.log(`\nExtracted ${beams.length} beams.`);

  // 2. Export table directly into Microsoft Excel
  try {
    excel.writeTable(beams, [
      'Handle',
      'Layer',
      'Length_mm',
      'Length_m',
      'Start_X',
      'Start_Y',
      'End_X',
      'End_Y'
    ]);
    console.log('\nSuccessfully exported to Microsoft Excel!');
  } catch (err) {
    console.log('\nExcel export failed:', err.message);
  }
}

exportBeamSchedule();
```

---

### Example 2: SQLite-Backed Entity Tracking

Stores drawing entity handles, layers, and areas into an embedded project database, allowing SQL analytics:

```javascript
// audit_drawing.js
const { DatabaseSync } = require('sqlite');

function auditDrawing() {
  const db = new DatabaseSync('C:/temp/drawing_audit.db');

  db.exec(`
    CREATE TABLE IF NOT EXISTS elements (
      handle TEXT PRIMARY KEY,
      class_name TEXT,
      layer TEXT,
      area REAL,
      length REAL,
      last_audit TEXT
    );
  `);

  const insert = db.prepare(`
    INSERT OR REPLACE INTO elements (handle, class_name, layer, area, length, last_audit)
    VALUES (?, ?, ?, ?, ?, datetime('now'))
  `);

  const entities = entity.select('\nSelect elements to audit: ', {
    out: e => ({
      handle: e.hand,
      ty: e.ty,
      layer: e.lyr,
      area: e.area || 0,
      length: e.l || 0
    })
  });

  tm.start();
  for (const ent of entities) {
    insert.run(ent.handle, ent.ty, ent.layer, ent.area, ent.length);
  }
  tm.commit();

  // Run SQL summary query:
  const summary = db.prepare(`
    SELECT layer, count(*) as count, sum(length)/1000.0 as total_len_m, sum(area) as total_area
    FROM elements
    GROUP BY layer
  `).all();

  console.log('\nAudit Summary by Layer:');
  console.log(inspect(summary, 'yaml'));

  db.close();
}

auditDrawing();
```

---

### Example 3: TCP Remote Control Server

Allows an external Python script, Node.js process, or web app to send JSON commands over TCP to control AutoCAD:

```javascript
// remote_listener.js
const net = require('net');

function startControlServer(port = 9876) {
  const server = net.listen(port, '127.0.0.1');
  console.log(`\nAutoCAD Remote Control listening on port ${port}...`);

  while (true) {
    const socket = server.accept();
    if (!socket) break;

    const rawMessage = socket.read();
    if (rawMessage) {
      try {
        const command = JSON.parse(rawMessage);
        
        if (command.action === 'highlight') {
          // Highlight entity by handle:
          entity.set({ clr: command.color || 1 }, command.handle);
          socket.write(JSON.stringify({ status: 'ok', updated: command.handle }));
        } else if (command.action === 'get') {
          const data = entity.get(command.handle);
          socket.write(JSON.stringify({ status: 'ok', data }));
        } else {
          socket.write(JSON.stringify({ status: 'error', message: 'Unknown action' }));
        }
      } catch (e) {
        socket.write(JSON.stringify({ status: 'error', message: e.message }));
      }
    }
    socket.close();
  }
  server.close();
}
```


---

## 7. Safe Transaction Commands

The safe transaction commands (`SS-SET`, `SS-GET`, `E-SET`, `E-GET`, `E-SHOW`, `SS-QUERY`) provide robust, transaction-safe database inspection and modification with guaranteed atomicity.

### Transaction Safety Guarantees
- **Database Atomicity**: Database operations are enclosed in an isolated, command-owned `AcTransaction`. If any error occurs or layer creation fails, all changes roll back completely without partial mutations.
- **Callback Isolation**: Callbacks evaluate against detached, read-only JavaScript snapshot objects. Callbacks cannot mutate database pointers or invoke raw CAD commands.
- **Interactive Multi-line Input & Query History**: Prompts with `Query <Last>:`. Supports multi-line bracket-balanced JavaScript arrow functions and closures. Pressing **Enter** reuses the last successful query.
- **REPL Inspection Output**: Results are rendered using native QuickJS REPL formatting (identical to `entity.pick()` output) rather than raw escaped JSON strings.
- **Auto Layer Creation**: If a target layer in `set` does not exist in the drawing, it is automatically created inside the owned transaction using the last active color.

### Detached Entity Snapshot Format
```javascript
{
  id: 3283,
  hand: 'CD3',        // alias: handle
  ty: 'AcDbPolyline', // alias: type
  l: 1600,            // alias: length
  a: 160000,          // alias: area
  clr: 1,             // alias: color
  trp: -2,            // alias: transparency
  visible: true,
  lyr: '0',           // alias: layer
  data: null          // XData object or null
}
```

### Commands and Query Examples

#### 1. `SS-SET` — Update Selected Entities
Select multiple entities, move matching lines to layer `0`, and store a sequential counter in their XData.
```javascript
(() => {
  let counter = 0;

  return {
    init: () => {
      counter = 0;
    },

    filter: e => e.ty === 'AcDbLine',

    set: e => ({
      layer: '0',
      data: {
        ...e.data,
        counter: counter++
      }
    })
  };
})()
```

#### 2. `SS-GET` — Read and Select Matching Blocks
Use the existing selection or select entities interactively. Return block information and leave matching blocks selected with native grips.
```javascript
({
  filter: e => e.ty === 'AcDbBlockReference',

  get: e => ({
    handle: e.handle,
    layer: e.layer,
    data: e.data
  }),

  implied: true
})
```

#### 3. `E-SET` — Update One Picked Entity
Pick one entity, move it to layer `0`, and mark its custom data as reviewed.
```javascript
({
  set: e => ({
    layer: '0',
    data: {
      ...e.data,
      reviewed: true,
      status: 'approved'
    }
  })
})
```

#### 4. `E-GET` — Inspect One Picked Entity
Pick one entity and return its properties without leaving it selected.
```javascript
({
  get: e => ({
    handle: e.handle,
    type: e.ty,
    layer: e.layer,
    data: e.data
  }),

  implied: false
})
```

#### 5. `E-SHOW` — Show an Entity by Handle
Replace `'1A2B'` with a handle obtained from a read command. Zoom to the entity and select it with native grips.
```javascript
({
  handle: '1A2B',
  zoom: true
})
```

To pick the entity instead:
```javascript
({
  zoom: true
})
```

#### 6. `SS-QUERY` — Search the Current Editing Space
Find all lines on layer `0`, return their handles and custom data, and select the matches. No interactive selection is required.
```javascript
({
  filter: e =>
    e.ty === 'AcDbLine' &&
    e.layer === '0',

  get: e => ({
    handle: e.handle,
    data: e.data
  }),

  implied: true
})
```

For any command, press **Enter** at `Query <Last>:` to reuse its last successful query.


### Selection Set Query DSL (Domain-Specific Language)

For aggregating and grouping entities, `SS-QUERY` supports a concise JSON-like DSL directly at `Query:` without requiring full manual `reduce` code. The DSL is compiled by the `query_dsl` C++ static library into optimized JavaScript.

#### Grammar (EBNF)

```ebnf
query          = [ "Query", [ options ], ":" ], group, EOF ;
options        = "<", "(", "{", "implied", ":", boolean, "}", ")", ">" ;
boolean        = "true" | "false" ;
group          = "{", expression, ":", ( group | selection ), [ "," ], "}" ;
selection      = "{", aggregate, { ",", aggregate }, [ "," ], "}" ;
aggregate      = field, ":", "+=", expression ;
field          = identifierName | stringLiteral ;
expression     = jsAssignmentExpression ;
```

#### Example 1: Group by Code and Sum Area & Length
```javascript
Query: {e.code: {a: +=e.a, l: +=e.l}}
```
Compiles automatically to:
```javascript
"use strict";
const toNumber = (value) => {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
};

const result = entries.reduce((acc, e) => {
  const group = (acc[e.code] ??= Object.assign(Object.create(null), { "a": 0, "l": 0 }));

  group["a"] += toNumber(e.a);
  group["l"] += toNumber(e.l);

  return acc;
}, Object.create(null));
```

#### Example 2: Multi-level Nested Grouping (Building -> Floor -> Code)
```javascript
Query: {e.building: {e.floor: {e.code: {a: +=e.a, l: +=e.l}}}}
```
Compiles automatically to:
```javascript
"use strict";
const toNumber = (value) => {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
};

const result = entries.reduce((acc, e) => {
  const group0 = (acc[e.building] ??= Object.create(null));
  const group1 = (group0[e.floor] ??= Object.create(null));
  const group2 = (group1[e.code] ??= Object.assign(Object.create(null), { "a": 0, "l": 0 }));

  group2["a"] += toNumber(e.a);
  group2["l"] += toNumber(e.l);

  return acc;
}, Object.create(null));
```

#### Example 3: Complex JS Expressions in Grouping & Aggregates
```javascript
Query: {
  e.building ?? "unknown": {
    e.floor > 5 ? "high" : "low": {
      area: +=(e.a ?? 0) * (e.quantity || 1),
      total: +=e.enabled ? pricing.total(e.l) : 0,
      flags: +=(e.flags & 3) << 1,
      value: +=e.metrics?.["area"] ?? 0
    }
  }
}
```

#### Example 4: Command Header & Options
```javascript
Query <({ implied: true })>: {e.info.code: {a: +=e.values?.a,},}
```

#### Example 5: Hierarchical Subtotals alongside Nested Groups (Rollup)
```javascript
Query: {
  e.building: {
    total: {a: +=e.a, l: +=e.l},
    e.floor: {
      total: {a: +=e.a, l: +=e.l},
      e.code: {
        e.room: {a: +=e.a, l: +=e.l}
      }
    }
  }
}
```
Compiles automatically to:
```javascript
"use strict";
const toNumber = (value) => {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
};

const result = entries.reduce((acc, e) => {
  const g0 = (acc[e.building] ??= Object.create(null));
  const g0_total = (g0["total"] ??= Object.assign(Object.create(null), { "a": 0, "l": 0 }));
  g0_total["a"] += toNumber(e.a);
  g0_total["l"] += toNumber(e.l);

  const g1 = (g0[e.floor] ??= Object.create(null));
  const g1_total = (g1["total"] ??= Object.assign(Object.create(null), { "a": 0, "l": 0 }));
  g1_total["a"] += toNumber(e.a);
  g1_total["l"] += toNumber(e.l);

  const g2 = (g1[e.code] ??= Object.create(null));
  const g3 = (g2[e.room] ??= Object.assign(Object.create(null), { "a": 0, "l": 0 }));
  g3["a"] += toNumber(e.a);
  g3["l"] += toNumber(e.l);

  return acc;
}, Object.create(null));
```

#### Example 6: Flat Array Grand Total (No Grouping)
```javascript
Query: [+=e.a, +=e.l, ++]
```
Compiles automatically to:
```javascript
"use strict";
const toNumber = (value) => {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
};

const result = entries.reduce((acc, e) => {
  acc[0] += toNumber(e.a);
  acc[1] += toNumber(e.l);
  acc[2] += 1;

  return acc;
}, [0, 0, 0]);
```
Returns a compact array: `[ totalArea, totalLength, entityCount ]`.

#### Example 7: Grouped Array Values
```javascript
Query: {e.code: [+=e.a, +=e.l]}
```
Compiles automatically to:
```javascript
"use strict";
const toNumber = (value) => {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
};

const result = entries.reduce((acc, e) => {
  const g0 = (acc[e.code] ??= [0, 0]);
  g0[0] += toNumber(e.a);
  g0[1] += toNumber(e.l);

  return acc;
}, Object.create(null));
```
Returns compact grouped arrays: `{ "WALL": [1200.5, 340.0], "DOOR": [45.0, 15.0] }`.

#### Example 8: Arithmetic Units Conversion & `where` Filtering
Unit conversions (e.g. $mm^2 \to m^2$, $mm \to m$) and entity filtering:
```javascript
Query: {e.code : [+=e.a/1e6, +=e.l/1e3]} where e.code == 'CT1'
```
Supported `where` positions:
1. **Trailing clause**: `{e.code: [+=e.a/1e6, +=e.l/1e3]} where e.a > 0`
2. **Prefix clause**: `where e.lyr == '0': {e.code: [+=e.a/1e6, +=e.l/1e3]}`
3. **Command options**: `Query <({ where: e.code != null })>: {e.code: [+=e.a/1e6, +=e.l/1e3]}`
4. **Interactive JavaScript**: `editor.ssQuery({ where: e => e.code === 'CT1', query: '{e.code: [+=e.a/1e6, +=e.l/1e3]}' })`

Compiles automatically to:
```javascript
"use strict";
const toNumber = (value) => {
  const number = Number(value);
  return Number.isFinite(number) ? number : 0;
};

const result = entries.reduce((acc, e) => {
  if (!(e.code == 'CT1')) return acc;
  const g0 = (acc[e.code] ??= [0, 0]);
  g0[0] += toNumber(e.a/1e6);
  g0[1] += toNumber(e.l/1e3);

  return acc;
}, Object.create(null));
```

| Category | Operators | Initial Accumulator Value | Generated JS Action |
| :--- | :--- | :--- | :--- |
| **Arithmetic** | `+=`, `-=` | `0` | `group["x"] += toNumber(expr);` |
| **Multiplication / Exp** | `*=`, `/=`, `**=` | `1` | `group["x"] *= toNumber(expr);` |
| **Modulo** | `%=` | `0` | `group["x"] %= toNumber(expr);` |
| **Bitwise Shifts** | `<<=`, `>>=`, `>>>=` | `0` | `group["x"] <<= toNumber(expr);` |
| **Bitwise Logic** | `&=`, `\|=`, `^=` | `-1` for `&=`, `0` for others | `group["x"] &= toNumber(expr);` |
| **Logical Assignment** | `&&=`, `\|\|=`, `??=` | `true` for `&&=`, `null` for others | `group["x"] &&= Boolean(expr);` / `group["x"] \|\|= (expr);` |
| **Counter / Increment** | `++`, `--` | `0` | `group["x"] += 1;` / `group["x"] -= 1;` |
| **Last Value / Assignment** | `=` | `null` | `group["x"] = (expr);` |
| **List Aggregation** | `push(...)` | `[]` | `group["x"].push(expr);` |
| **Extrema (Min / Max)** | `Math.min(...)`, `Math.max(...)` | `Infinity` / `-Infinity` | `group["x"] = Math.min(group["x"], toNumber(expr));` |


