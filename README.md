# qjs — AutoCAD & GstarCAD QuickJS & Safe Takeoff Plugin

An AutoCAD ObjectARX / GstarCAD GRX C++ plugin running an embedded **QuickJS** ECMAScript 2025 engine, **SQLite3**, **MessagePack**, **WebView2**, and a native **Query DSL** engine for safe transactions, entity modification, and high-performance quantity takeoff.

---

## Supported CAD Platforms & Output Binaries

| Platform | Supported Versions | SDK Location | Output Binary |
| :--- | :--- | :--- | :--- |
| **AutoCAD** | **2017** | `C:/Autodesk/Autodesk_ObjectARX_2017_Win_64_and_32_Bit` | `qjs_2017.arx` |
| **AutoCAD** | **2018** | `C:/Autodesk/Autodesk_ObjectARX_2018_Win_64_and_32_Bit` | `qjs_2018.arx` |
| **AutoCAD** | **2019** | `C:/Autodesk/Autodesk_ObjectARX_2019_Win_64_and_32_Bit` | `qjs_2019.arx` |
| **AutoCAD** | **2020** | `C:/Autodesk/ObjectARX_for_AutoCAD_2020_Win_64_bit` | `qjs_2020.arx` |
| **AutoCAD** | **2021** | `C:/Autodesk/ObjectARX_for_AutoCAD_2021_Win_64bit_dlm` | `qjs_2021.arx` |
| **AutoCAD** | **2022** | `C:/Autodesk/ObjectARX_for_AutoCAD_2022_Win_64bit_dlm` | `qjs_2022.arx` |
| **AutoCAD** | **2023** | `C:/Autodesk/ObjectARX_for_AutoCAD_2023_Win_64bit_dlm` | `qjs_2023.arx` |
| **AutoCAD** | **2024** | `C:/Autodesk/ObjectARX_for_AutoCAD_2024_Win_64bit_dlm` | `qjs_2024.arx` |
| **GstarCAD** | **2026** | `C:/GstarCAD/grxsdk` | `qjs_2026.grx` |

---

## Prerequisites

- **Visual Studio 2022** (MSVC toolset x64)
- **CMake 3.20+**
- **ObjectARX SDK** or **GstarCAD GRX SDK** (auto-detected from `C:/Autodesk` or `C:/GstarCAD`)
- **Microsoft WebView2 SDK** (`../script/packages/Microsoft.Web.WebView2.1.0.3650.58` or NuGet cache)

---

## Build Automation (`build.ps1`)

The included PowerShell build script handles configuration, compilation, and distribution packaging:

### 1. Default Build (AutoCAD 2018 & GstarCAD 2026)
```powershell
.\build.ps1
```
*Builds default targets: AutoCAD 2018 (`qjs_2018.arx`) and GstarCAD 2026 (`qjs_2026.grx`).*

### 2. Clean Build
```powershell
.\build.ps1 -Clean
```
*Cleans previous build folders and `dist/`, then builds default targets.*

### 3. Build All Targets (AutoCAD 2017–2024 & GstarCAD 2026)
```powershell
.\build.ps1 -All
```

### 4. Build Specific Version
```powershell
.\build.ps1 -Version 2018      # AutoCAD 2018
.\build.ps1 -Version 2024      # AutoCAD 2024
.\build.ps1 -GstarCAD          # GstarCAD 2026
```

### 5. Direct CMake Build
```powershell
# AutoCAD 2018
cmake -S . -B build_2018 -DAUTOCAD_VERSION=2018
cmake --build build_2018 --config Release

# GstarCAD 2026
cmake -S . -B build_gcad -DCAD_PLATFORM=GSTARCAD
cmake --build build_gcad --config Release
```

**Build Artifacts (`dist/`):**
- `dist/2018/qjs_2018.arx` + `WebView2Loader.dll`
- `dist/gstarcad_2026/qjs_2026.grx` + `WebView2Loader.dll`
- `dist/bin/qjs_*.arx` and `dist/bin/qjs_*.grx`
- `dist/arx-script.bundle/`: Ready-to-deploy Autoloader package for `%APPDATA%\Autodesk\ApplicationPlugins\`
- `dist/runjs.lsp`: Multi-version AutoLISP loader with auto-detection

### 5. Run Native QuickJS DSL Tests
A standalone executable tests the native C++ DSL library without requiring AutoCAD or GstarCAD to be open:
```powershell
.\build_2021\Release\test_query_dsl.exe
```
*(All 21 comprehensive test cases pass in native QuickJS)*

---

## Safe Transaction & Takeoff Commands

The plugin provides safe transaction commands with guaranteed atomicity (automatic rollback on unhandled error), highlight management, and native metadata persistence:

| Command | Type | Description |
| :--- | :--- | :--- |
| `XPROPS` / `QPROPS` | CLI / LISP | Launches and syncs the WinUI 3 XData & DSL Properties Palette window with live takeoff formulas. |
| `SS-QUERY` | CLI / LISP | Evaluates quantity takeoff and aggregation queries on selection sets using the Query DSL. |
| `SS-SET` | CLI / LISP | Atomically updates properties of selected entities. Non-CAD properties (e.g. `code: 'CT1'`) are automatically routed to entity XData and projected onto `e.code`. |
| `SS-GET` | CLI / LISP | Retrieves property snapshots of selected entities as JSON. |
| `E-SET` | CLI | Atomically updates a single selected entity. |
| `E-GET` | CLI | Retrieves snapshot of a single entity. |
| `E-SHOW` | CLI | Interactively highlights and logs a single entity's properties. |

---

## Selection Set Query DSL (`SS-QUERY`)

The native static library [`src/query_dsl/`](src/query_dsl/) compiles concise JSON-like DSL query strings into optimized JavaScript reduction loops evaluated inside QuickJS.

### Interactive Command Workflow (`Command: SS-QUERY`)

`SS-QUERY` provides an interactive, continuous prompt loop in the AutoCAD console:

```text
Command: SS-QUERY

Query [Query/Last]<Last>: {e.building: {
  total: {a: +=e.a/1e6, l: +=e.l/1e3},
  e.floor: {
    total: {a: +=e.a/1e6, l: +=e.l/1e3},
    e.code: {
      e.room: {a: +=e.a/1e6, l: +=e.l/1e3}
    }
  }
}} where e.a

select objects [Select/All] <All>: <Enter>
... return value of query for all objects

select objects [Select/All] <All>: <Select objects>
... return value of query for selected objects

select objects or Esc to cancel [Select/All] <Select>: <Esc>
... command canceled

Query [Query/Last]<Query>: L <Enter>
... for last query

select objects [Select/All] <All>: <Enter>
... return value of query for all objects

select objects [Select/All] <All>: <Select objects>
... return value of query for selected objects

select objects or Esc to cancel [Select/All] <Select>: <Esc>
... command canceled
```

**Key Prompt Features:**
- **`Query [Query/Last]<...>`**: Remembers the last query string across invocations. Pressing `<Enter>` accepts `<Last>`, typing `L` / `Last` reuses the previous query, typing `Q` / `Query` prompts for a new query, or you can paste / type a multi-line query directly (bracket depth is tracked across lines).
- **`select objects [Select/All] <All>`**: Pressing `<Enter>` or `All` runs the query across the entire current space. Entering `Select` launches native AutoCAD interactive selection (`acedSSGet`).
- **`select objects or Esc to cancel [Select/All] <Select>`**: After an interactive selection, `<Select>` becomes the default choice for rapid iterative selections. Pressing `<Esc>` cleanly cancels and exits the command loop.

---

### Query DSL Examples

#### 1. Bare Quick Total & Count (`+=a`, `+=l`, `++`)
No brackets or boilerplate required for rapid command-line takeoff:
- **`+=a`**: directly sums area as a single number (e.g. `39.553083`)
- **`+=a/1e6`**: sums area converted from $mm^2$ to $m^2$
- **`+=l/1e3`**: sums length converted from $mm$ to $m$
- **`++`**: counts total matching entities
- **`+=a, +=l`**: returns `[ totalArea, totalLength ]`
- **`+=a/1e6 where a`**: safely sums area excluding non-area entities (lines, points)

*(Entity properties like `a`, `l`, `area`, `length`, `lyr`, `code`, etc. automatically resolve to `e.<prop>` without needing explicit `e.` prefixes)*

#### 2. Arithmetic Unit Conversions ($mm^2 \to m^2$, $mm \to m$)
```text
Command: SS-QUERY
Query: {e.code: [+=e.a/1e6, +=e.l/1e3]}
```
**Output:**
```json
{
  "WALL": [ 39.553083, 48.87384 ],
  "DOOR": [ 0.45, 1.8 ]
}
```

### 2. Truthy & Error-Resilient `where` Filtering
Safely excludes zero-area lines, points, and non-area elements without runtime errors:
```text
Command: SS-QUERY
Query: {e.code: [+=e.a/1e6, +=e.l/1e3]} where e.a
```
*(Guarded by an automatic `try...catch` block; entities with missing deep properties or `0`/`null`/`undefined`/`NaN` are skipped safely without throwing exceptions)*

### 3. Flat Array Grand Total (No Grouping)
```text
Command: SS-QUERY
Query: [+=e.a/1e6, +=e.l/1e3, ++] where e.a
```
**Output:** `[ totalArea_m2, totalLength_m, count ]`
```json
[ 40.003083, 50.67384, 5 ]
```

### 4. Hierarchical Subtotals / Rollup Grouping
Calculates building totals, floor totals, and detailed room breakdowns in a single pass:
```text
Command: SS-QUERY
Query: {e.building: {
  total: {a: +=e.a/1e6, l: +=e.l/1e3},
  e.floor: {
    total: {a: +=e.a/1e6, l: +=e.l/1e3},
    e.code: {
      e.room: {a: +=e.a/1e6, l: +=e.l/1e3}
    }
  }
}} where e.a
```
**Output:**
```json
{
  "TowerA": {
    "total": { "a": 24.803083, "l": 33.17384 },
    "L1": {
      "total": { "a": 16.303083, "l": 23.67384 },
      "WALL": {
        "101": { "a": 11.665719, "l": 13.66559 },
        "102": { "a": 4.187364, "l": 8.20825 }
      },
      "DOOR": {
        "101": { "a": 0.45, "l": 1.8 }
      }
    },
    "L2": {
      "total": { "a": 8.5, "l": 9.5 },
      "WALL": { "201": { "a": 8.5, "l": 9.5 } }
    }
  }
}
```

### 5. Multiple Sibling Branching Groups (`byFloor` and `byTrade`)
```text
Command: SS-QUERY
Query: {e.building: {
  total: {a: +=e.a/1e6},
  byFloor: {e.floor: {a: +=e.a/1e6}},
  byTrade: {e.lyr: {a: +=e.a/1e6}}
}} where e.a
```

---

## Attribute Assignment (`SS-SET` & `E-SET`)

Custom user metadata is automatically persisted to native AutoCAD XData and exposed directly onto the entity snapshot:

```text
Command: SS-SET
Query <{code: 'CT1'}>: e => ({ code: 'CT1', building: 'TowerA', floor: 1 })
Select objects: Specify opposite corner: 2 found
Select objects: <Enter>
```
*(The command applies the modifications atomically without echoing return values. Use `SS-QUERY` or `SS-GET` to inspect updated values)*
*(Both arrow function forms `e => ({ ... })` and unparenthesized `e => { ... }` are automatically normalized)*

### Template Query Placeholders (`_0`, `_1`, ...) & Continuous Input Loop

For rapid repetitive tagging or querying in `SS-SET`, `SS-GET`, and `E-SET`, you can define dynamic query templates using `_<number>` placeholders (e.g. `_0`, `_1`, ...).

When placeholders are present, **`SS-SET` and `SS-GET` automatically enter a continuous loop** without exiting back to the `Command:` prompt:

#### 1. Continuous Tagging (`SS-SET`):
```text
Command: SS-SET
Query <{data: {room: 'Toilet.' + _0}, clr: _1}>: <Enter>
_0 <Delux>: <Enter>
_1 <8>: <Enter>
Select objects: 2 found
Select objects: <Enter>

_0 <Delux>: Standard
_1 <8>: 2
Select objects: 2 found
Select objects: <Enter>

_0 <Standard>: <Esc>   <-- Press Esc (or Esc at 'Select objects:') to finish
Command:
```

#### 2. Continuous Querying & Inspection (`SS-GET`):
```text
Command: SS-GET
Query <{data: {code: _0, .h > _1}}>: <Enter>
_0 <'CT1'>: <Enter>
_1 <1.5>: <Enter>
Select objects: 2 found
Select objects: <Enter>
[ { "id": 12, "ty": "AcDbLine", "data": { "code": "CT1", "h": 2.0 } } ]

_0 <CT1>: CT2
_1 <1.5>: 0.5
Select objects: 4 found
Select objects: <Enter>
[ ... ]

_0 <CT2>: <Esc>        <-- Press Esc to finish
Command:
```

- **Seamless Interactive Loop**: The command keeps prompting for the next set of values and selections until you press `<Esc>` at `_0` or `<Esc>` at `Select objects:`.
- **Persistent Defaults**: Last entered values are remembered and offered as `<defaults>` on subsequent iterations.
- **Smart Type Coercion**: Numbers (`1`, `2.5`) remain numbers; unquoted strings (`Basement`, `Standard`, `CT1`) are automatically quoted safely; booleans (`true`/`false`) and arrays (`[1, 2]`) are evaluated as expressions.

---

## Declarative Object Pattern & Filter DSL (`SS-GET`, `SS-SET`, `SS-QUERY`, `E-GET`)

`SS-GET`, `SS-QUERY`, `SS-SET`, and `E-GET` support a rich, declarative query DSL without requiring verbose JavaScript arrow boilerplate:

```text
Command: SS-GET
Query: {clr: except [1, {r: any, g:0, b:0}], data: {code: any, fl: 'Lt.*', code: in ['CT*,'*MR*'], ty: 'AcDbLine'}}
```
or with unkeyed conditions and regex patterns:
```text
Command: SS-GET
Query: {clr: any, e: {phone: /\d{3}-\d{4}/g, data: {.h > 1 && .h < 25, section: {.b > 200 && .w < 300}}}}
```
or standalone boolean expressions:
```text
Command: SS-GET
Query: e.data.h > 1 && e.data.h < 10
```

### Supported Pattern Syntaxes:
- **Exact & Multi-Property Match**:
  `{lyr: 'Qs.Room.Corridor', clr: 256}`
- **Unbraced Shorthand**:
  `lyr: 'Qs.Room.Corridor', clr: 256`
- **Array Inclusion (`in [...]`)**:
  `{clr: [1, 5, 256]}` or `{code: in ['CT*', '*MR*']}`
- **Exclusion Matching (`except [...]` / `not in [...]`)**:
  `{clr: except [1, {r: any, g: 0, b: 0}]}` *(matches anything except ACI 1 or RGB red)*
- **Any Defined Value (`any`)**:
  `{clr: any}` or `{code: any}` *(matches any value except `undefined` and `null`)*
- **Bidirectional Color Matching (ACI $\leftrightarrow$ RGB)**:
  `{clr: {r: any, g: 0, b: 0}}` matches both TrueColor Red and ACI color index `1` (AutoCAD calculates RGB for ACI colors automatically).
- **AutoCAD Wildcards & Regular Expressions**:
  - CAD wildcards: `{code: 'CT*'}` or `{code: in ['CT*', '*MR*']}` *(case-insensitive `acutWcMatch`: `*`, `?`, `#`, `@`, etc.)*
  - Regex literals: `{phone: /\d{3}-\d{4}/g}` *(automatically resets `lastIndex = 0` between entities)*
  - Regex strings: `{fl: 'Lt.*'}`
  - Typo recovery: `{code: in ['CT*,'*MR*']}` is automatically corrected to `['CT*', '*MR*']`
- **Unkeyed Condition Expressions (`.prop > val`)**:
  Conditions can be placed directly inside object literals without key prefixes:
  `{data: {.h > 1 && .h < 25, section: {.b > 200 && .w < 300}}}`
  *(Leading `.h`, `.b`, `.w` automatically resolve to fields on the scoped object)*
- **Root Entity Alias (`e:`)**:
  `{clr: any, e: {phone: /\d{3}-\d{4}/g, data: {...}}}`
  *(The `e:` block transparently unpacks properties against the root entity)*
- **Direct Boolean Expression Predicate**:
  `e.data.h > 1 && e.data.h < 10`
  *(Wrapped in a safe scoped lambda with try/catch; entities missing `data` or properties cleanly evaluate to `false` without throwing `TypeError`)*
- **CAD Type Matching**:
  `{ty: 'Line'}` *(matches `AcDbLine` automatically, case-insensitive)*
- **Nested XData Match & Root Property Fallback**:
  `{data: {floor: 'Lt1', ty: 'AcDbLine'}}` *(properties like `ty`, `lyr`, `clr`, `hand` fall back to the root entity if not defined in `data`)*
- **Predicate Function within Pattern**:
  `{clr: c => c !== 256, l: l => l > 1000}`

---

## Universal Color Spaces (`clr` / `color`)

The `clr` (or `color`) property across `SS-SET`, `E-SET`, `entity.set()`, and `layer.set()` natively supports multiple color representations and color spaces without needing external conversion calls:

### 1. HSL (Hue, Saturation, Lightness)
- Object: `{clr: { h: 200, s: 70, l: 50 }}` or `{clr: { hue: 200, sat: 70, light: 50 }}`
- String: `{clr: 'hsl(200, 70%, 50%)'}` or `{clr: 'hsl(200, 70, 50)'}`
- Dynamic template: `{clr: { h: _0, s: 70, l: 50 }}`

### 2. HSV / HSB (Hue, Saturation, Value / Brightness)
- Object: `{clr: { h: 200, s: 70, v: 50 }}` or `{clr: { h: 200, s: 70, b: 50 }}`
- String: `{clr: 'hsv(200, 70%, 50%)'}`

### 3. CMYK (Cyan, Magenta, Yellow, Key / Black)
- Object: `{clr: { c: 0, m: 50, y: 100, k: 0 }}`
- String: `{clr: 'cmyk(0, 50, 100, 0)'}`

### 4. HCT (Hue, Chroma, Tone - Material Design 3)
- Object: `{clr: { h: 200, c: 70, t: 50 }}`
- String: `{clr: 'hct(200, 70, 50)'}`

### 5. RGB, Hex & AutoCAD Color Index (ACI)
- RGB Object / Array: `{clr: { r: 38, g: 157, b: 217 }}` or `{clr: [38, 157, 217]}`
- Hex string: `{clr: '#269dd9'}` or `{clr: '#0cf'}`
- Named standard colors: `'red'`, `'yellow'`, `'green'`, `'cyan'`, `'blue'`, `'magenta'`, `'white'`, `'bylayer'`, `'byblock'`
- ACI number: `1`..`255` (ACI colors), `256` (ByLayer), `0` (ByBlock)

---

## AutoLISP & JavaScript APIs

### AutoLISP Integration
Load [`lisp/runjs.lsp`](lisp/runjs.lsp) into AutoCAD:
```lisp
;; Execute DSL query directly from AutoLISP:
(ss-query "{e.code: [+=e.a/1e6, +=e.l/1e3]} where e.a")

;; Query selection set using pattern DSL:
(ss-get "{clr: except [1, {r: any, g: 0, b: 0}], data: {.h > 1 && .h < 25}}")

;; Query selection set using boolean expression:
(ss-get "e.data.h > 1 && e.data.h < 10")

;; Batch-set entity metadata:
(ss-set "{code: 'CT1'}")

;; Evaluate arbitrary JS and return JSON value:
(js-eval "editor.ssQuery('{e.code: [+=e.a, +=e.l]}')")
```

### JavaScript API (`editor.ssQuery`)
Inside scripts or the interactive REPL:
```javascript
// Passing DSL string directly (queries all objects in current space):
editor.ssQuery("{e.code: [+=e.a/1e6, +=e.l/1e3]} where e.a");

// Interactive selection prompt:
editor.ssQuery({
  select: true,
  query: "{e.code: [+=e.a/1e6, +=e.l/1e3]} where e.a"
});

// Explicit entity handles or object IDs with custom JS filter:
editor.ssQuery({
  entities: ['2C4', '2C5'],
  where: e => e.a > 0 && e.lyr === 'WALL',
  query: "{e.code: [+=e.a/1e6, +=e.l/1e3]}"
});
```

---

## JavaScript Modules Reference

For full API signatures, options, and comprehensive guides on all embedded modules, refer to:
- [`docs/JS_MODULES.md`](docs/JS_MODULES.md): Detailed reference for `entity`, `editor`, `layer`, `color`, `tm`, `sqlite`, `excel`, `msgpack`, and `query_dsl`.
- [`C:\Users\qsten\OneDrive\Documents\ChatGPT\DSL\README.md`](C:/Users/qsten/OneDrive/Documents/ChatGPT/DSL/README.md): Standalone DSL compiler, grammar specification, and Node.js / Rust bundle documentation.
