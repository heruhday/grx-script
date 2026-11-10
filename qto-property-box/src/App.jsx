import React, { useState, useMemo, useEffect, useCallback, useRef } from 'react';
import { invoke } from '@tauri-apps/api/core';
import {
  Layers,
  MapPin,
  Box,
  Ruler,
  Square,
  BrickWall,
  Hash,
  Weight,
  Columns,
  Cuboid,
  LayoutGrid,
  Plus,
  Trash2,
  Copy,
  ChevronDown,
  ChevronRight,
  Code2,
  Sliders,
  Check,
  Database,
  Calculator,
  RotateCcw,
  Crosshair,
  CheckSquare,
  Table,
  X,
  Pin,
  PinOff,
  RefreshCw,
  UploadCloud,
  Wifi,
  WifiOff,
  Eye
} from 'lucide-react';

// ============================================================================
// SVG ICONS (Matching stroke style, 24x24 viewBox, currentColor)
// ============================================================================
function LengthIcon({ className = 'w-3.5 h-3.5' }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M4 6v12" />
      <path d="M20 6v12" />
      <path d="M4 12h16" />
      <path d="m7 9-3 3 3 3" />
      <path d="m17 9 3 3-3 3" />
    </svg>
  );
}

function AreaIcon({ className = 'w-3.5 h-3.5' }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <rect width="18" height="18" x="3" y="3" rx="2" />
      <path d="m3 15 12-12" />
      <path d="m9 21 12-12" />
    </svg>
  );
}

function BeamIcon({ className = 'w-3.5 h-3.5' }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="m3 9 5-4 13 4-5 4z" />
      <path d="M3 9v5l5 4v-5" />
      <path d="m8 18 13-4v-5" />
    </svg>
  );
}

function ColumnIcon({ className = 'w-3.5 h-3.5' }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="m12 3 7 4-7 4-7-4z" />
      <path d="M5 7v10l7 4" />
      <path d="M19 7v10l-7 4" />
      <path d="M12 11v10" />
    </svg>
  );
}

function SlabIcon({ className = 'w-3.5 h-3.5' }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="m12 4 9 5-9 5-9-5z" />
      <path d="M3 9v4l9 5 9-5v-4" />
    </svg>
  );
}

function WeightIcon({ className = 'w-3.5 h-3.5' }) {
  return (
    <svg className={className} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
      <path d="M9 7a3 3 0 0 1 6 0v1" />
      <path d="M9 8h6" />
      <path d="M7 8l-3 11a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2L17 8z" />
    </svg>
  );
}

const QUICK_CATEGORY_PILLS = [
  { id: 'length', label: 'LENGTH', icon: LengthIcon },
  { id: 'area', label: 'AREA', icon: AreaIcon },
  { id: 'wall', label: 'WALL', icon: BrickWall },
  { id: 'beam', label: 'BEAM', icon: BeamIcon },
  { id: 'column', label: 'COLUMN', icon: ColumnIcon },
  { id: 'slab', label: 'SLAB', icon: SlabIcon },
  { id: 'weight', label: 'WEIGHT', icon: WeightIcon },
  { id: 'count', label: 'COUNT', icon: Hash }
];

// ============================================================================
// 1. CATEGORY DEFINITIONS & FORMULA RULES (Sesuai Spesifikasi JSON QTO)
// ============================================================================
const CATEGORY_CONFIG = {
  length: {
    label: 'length',
    title: 'Linear Length',
    icon: LengthIcon,
    color: '#38bdf8',
    baseMetric: 'length',
    baseUnit: 'm',
    xdataFields: [],
    formulaLabel: 'total_length'
  },
  area: {
    label: 'area',
    title: 'Surface Area',
    icon: AreaIcon,
    color: '#34d399',
    baseMetric: 'area',
    baseUnit: 'm²',
    xdataFields: [],
    formulaLabel: 'total_area'
  },
  wall: {
    label: 'wall',
    title: 'Wall Area (Length × wh)',
    icon: BrickWall,
    color: '#fb923c',
    baseMetric: 'length',
    baseUnit: 'm',
    xdataFields: [
      { key: 'wh', label: 'Height (xdata: wh)', unit: 'm', step: 0.1, defaultVal: 3.4 }
    ],
    formulaLabel: 'total(length * height (xdata: wh))'
  },
  count: {
    label: 'count',
    title: 'Object Count',
    icon: Hash,
    color: '#a78bfa',
    baseMetric: 'count',
    baseUnit: 'unit',
    xdataFields: [],
    formulaLabel: 'total_count'
  },
  weight: {
    label: 'weight',
    title: 'Structural / Material Weight',
    icon: WeightIcon,
    color: '#f472b6',
    baseMetric: 'dynamic', // line -> length, surface -> area
    baseUnit: 'm / m²',
    xdataFields: [
      {
        key: 'wpl',
        label: 'Weight/Length (xdata: wpl)',
        unit: 'kg/m',
        step: 0.5,
        defaultVal: 36.7,
        entityFilter: 'line'
      },
      {
        key: 'wpa',
        label: 'Weight/Area (xdata: wpa)',
        unit: 'kg/m²',
        step: 0.5,
        defaultVal: 5.4,
        entityFilter: 'surface'
      }
    ],
    formulaLabel: 'line: total(length * wpl) | surface: total(area * wpa)'
  },
  beam: {
    label: 'beam',
    title: 'Structural Beam (Concrete & Steel)',
    icon: BeamIcon,
    color: '#f59e0b',
    baseMetric: 'length',
    baseUnit: 'm',
    xdataFields: [
      { key: 'w', label: 'Width (xdata: w)', unit: 'm', step: 0.05, defaultVal: 0.3 },
      { key: 'h', label: 'Height (xdata: h)', unit: 'm', step: 0.05, defaultVal: 0.6 },
      {
        key: 'swpl',
        label: 'Steel Weight/Length (xdata: swpl)',
        unit: 'kg/m',
        step: 1,
        defaultVal: 42.5
      }
    ],
    formulaLabel: 'concrete = total_length * w * h | steel = total_length * swpl'
  },
  column: {
    label: 'column',
    title: 'Structural Column (Concrete & Steel)',
    icon: ColumnIcon,
    color: '#ef4444',
    baseMetric: 'length',
    baseUnit: 'm',
    xdataFields: [
      { key: 'w', label: 'Width (xdata: w)', unit: 'm', step: 0.05, defaultVal: 0.5 },
      { key: 'h', label: 'Height (xdata: h)', unit: 'm', step: 0.05, defaultVal: 0.5 },
      {
        key: 'swpl',
        label: 'Steel Weight/Length (xdata: swpl)',
        unit: 'kg/m',
        step: 1,
        defaultVal: 68.0
      }
    ],
    formulaLabel: 'concrete = total(length * w * h) | steel = total(length * swpl)'
  },
  slab: {
    label: 'slab',
    title: 'Floor Slab (Concrete & Steel)',
    icon: SlabIcon,
    color: '#06b6d4',
    baseMetric: 'area',
    baseUnit: 'm²',
    xdataFields: [
      { key: 't', label: 'Thickness (xdata: t)', unit: 'm', step: 0.01, defaultVal: 0.15 },
      {
        key: 'swpa',
        label: 'Steel Weight/Area (xdata: swpa)',
        unit: 'kg/m²',
        step: 0.5,
        defaultVal: 18.5
      }
    ],
    formulaLabel: 'concrete = total(area * t) | steel = total(area * swpa)'
  }
};

// ============================================================================
// 2. INITIAL DEMO ENTITIES (Used when CAD is offline or in Demo Mode)
// ============================================================================
const INITIAL_CAD_ENTITIES = [
  {
    id: 'ENT-W1',
    handle: '4A1',
    layer: 'A-WALL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'North Exterior Brick Wall',
    categoryPreset: 'wall',
    codePreset: 'WALL-BRK-150',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { wh: 3.4 }
  },
  {
    id: 'ENT-W2',
    handle: '4A2',
    layer: 'A-WALL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'East Exterior Brick Wall',
    categoryPreset: 'wall',
    codePreset: 'WALL-BRK-150',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { wh: 3.4 }
  },
  {
    id: 'ENT-W3',
    handle: '4A3',
    layer: 'A-WALL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'South Exterior Brick Wall',
    categoryPreset: 'wall',
    codePreset: 'WALL-BRK-150',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { wh: 3.4 }
  },
  {
    id: 'ENT-W4',
    handle: '4A4',
    layer: 'A-WALL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'South-East Exterior Wall',
    categoryPreset: 'wall',
    codePreset: 'WALL-BRK-150',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { wh: 3.4 }
  },
  {
    id: 'ENT-B1',
    handle: '5B1',
    layer: 'S-BEAM',
    dxfType: 'LINE',
    entity_type: 'line',
    label: 'Main Girder Beam G1 (Grid A-B)',
    categoryPreset: 'beam',
    codePreset: 'BM-G1-300x600',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { w: 0.3, h: 0.6, swpl: 42.5 }
  },
  {
    id: 'ENT-B2',
    handle: '5B2',
    layer: 'S-BEAM',
    dxfType: 'LINE',
    entity_type: 'line',
    label: 'Main Girder Beam G1 (Grid B-C)',
    categoryPreset: 'beam',
    codePreset: 'BM-G1-300x600',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { w: 0.3, h: 0.6, swpl: 42.5 }
  },
  {
    id: 'ENT-B3',
    handle: '5B3',
    layer: 'S-BEAM',
    dxfType: 'LINE',
    entity_type: 'line',
    label: 'Cross Spine Beam G2 (Grid 2)',
    categoryPreset: 'beam',
    codePreset: 'BM-G1-300x600',
    quantity: 6.5,
    rawLengthMm: 6500,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { w: 0.3, h: 0.6, swpl: 42.5 }
  },
  {
    id: 'ENT-C1',
    handle: '6C1',
    layer: 'S-COL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'Column C1 (Grid A-1)',
    categoryPreset: 'column',
    codePreset: 'COL-K1-500x500',
    quantity: 4.0,
    rawLengthMm: 4000,
    rawAreaMm2: 250000,
    multiplier: 1,
    xdata: { w: 0.5, h: 0.5, swpl: 68.0 }
  },
  {
    id: 'ENT-C2',
    handle: '6C2',
    layer: 'S-COL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'Column C1 (Grid B-1)',
    categoryPreset: 'column',
    codePreset: 'COL-K1-500x500',
    quantity: 4.0,
    rawLengthMm: 4000,
    rawAreaMm2: 250000,
    multiplier: 1,
    xdata: { w: 0.5, h: 0.5, swpl: 68.0 }
  },
  {
    id: 'ENT-C3',
    handle: '6C3',
    layer: 'S-COL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'Column C1 (Grid C-1)',
    categoryPreset: 'column',
    codePreset: 'COL-K1-500x500',
    quantity: 4.0,
    rawLengthMm: 4000,
    rawAreaMm2: 250000,
    multiplier: 1,
    xdata: { w: 0.5, h: 0.5, swpl: 68.0 }
  },
  {
    id: 'ENT-C4',
    handle: '6C4',
    layer: 'S-COL',
    dxfType: 'LWPOLYLINE',
    entity_type: 'line',
    label: 'Column C1 (Grid B-2 Center)',
    categoryPreset: 'column',
    codePreset: 'COL-K1-500x500',
    quantity: 4.0,
    rawLengthMm: 4000,
    rawAreaMm2: 250000,
    multiplier: 1,
    xdata: { w: 0.5, h: 0.5, swpl: 68.0 }
  },
  {
    id: 'ENT-S1',
    handle: '7D1',
    layer: 'S-SLAB',
    dxfType: 'HATCH / REGION',
    entity_type: 'surface',
    label: 'Zone A Floor Slab S1',
    categoryPreset: 'slab',
    codePreset: 'SLB-S1-150MM',
    quantity: 36.0,
    rawLengthMm: 24000,
    rawAreaMm2: 36000000,
    multiplier: 1,
    xdata: { t: 0.15, swpa: 18.5 }
  },
  {
    id: 'ENT-S2',
    handle: '7D2',
    layer: 'S-SLAB',
    dxfType: 'HATCH / REGION',
    entity_type: 'surface',
    label: 'Zone B Floor Slab S1',
    categoryPreset: 'slab',
    codePreset: 'SLB-S1-150MM',
    quantity: 36.0,
    rawLengthMm: 24000,
    rawAreaMm2: 36000000,
    multiplier: 1,
    xdata: { t: 0.15, swpa: 18.5 }
  },
  {
    id: 'ENT-ST1',
    handle: '8E1',
    layer: 'S-STEEL',
    dxfType: 'LINE',
    entity_type: 'line',
    label: 'Secondary IWF 250x125 Rafter 1',
    categoryPreset: 'weight',
    codePreset: 'STL-IWF-250',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { wpl: 29.6, wpa: 5.4 }
  },
  {
    id: 'ENT-ST2',
    handle: '8E2',
    layer: 'S-STEEL',
    dxfType: 'LINE',
    entity_type: 'line',
    label: 'Secondary IWF 250x125 Rafter 2',
    categoryPreset: 'weight',
    codePreset: 'STL-IWF-250',
    quantity: 6.0,
    rawLengthMm: 6000,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: { wpl: 29.6, wpa: 5.4 }
  },
  {
    id: 'ENT-P1',
    handle: '9F1',
    layer: 'A-FIXT',
    dxfType: 'INSERT',
    entity_type: 'count',
    label: 'Floor Drain FD-01 (Zone A)',
    categoryPreset: 'count',
    codePreset: 'SAN-FD-100',
    quantity: 1,
    rawLengthMm: 0,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: {}
  },
  {
    id: 'ENT-P2',
    handle: '9F2',
    layer: 'A-FIXT',
    dxfType: 'INSERT',
    entity_type: 'count',
    label: 'Floor Drain FD-01 (Zone B)',
    categoryPreset: 'count',
    codePreset: 'SAN-FD-100',
    quantity: 1,
    rawLengthMm: 0,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: {}
  },
  {
    id: 'ENT-P3',
    handle: '9F3',
    layer: 'A-FIXT',
    dxfType: 'INSERT',
    entity_type: 'count',
    label: 'Floor Drain FD-01 (South)',
    categoryPreset: 'count',
    codePreset: 'SAN-FD-100',
    quantity: 1,
    rawLengthMm: 0,
    rawAreaMm2: 0,
    multiplier: 1,
    xdata: {}
  }
];

const SELECTION_PRESETS = [
  { id: 'wall', label: 'Wall (4) — A-WALL', layer: 'A-WALL', category: 'wall', code: 'WALL-BRK-150' },
  { id: 'beam', label: 'Beam (3) — S-BEAM', layer: 'S-BEAM', category: 'beam', code: 'BM-G1-300x600' },
  { id: 'column', label: 'Column (4) — S-COL', layer: 'S-COL', category: 'column', code: 'COL-K1-500x500' },
  { id: 'slab', label: 'Slab (2) — S-SLAB', layer: 'S-SLAB', category: 'slab', code: 'SLB-S1-150MM' },
  { id: 'weight', label: 'Steel (2) — S-STEEL', layer: 'S-STEEL', category: 'weight', code: 'STL-IWF-250' },
  { id: 'count', label: 'Insert (3) — A-FIXT', layer: 'A-FIXT', category: 'count', code: 'SAN-FD-100' }
];

// Unit conversion factors (from CAD drawing unit to meters / m²)
const UNIT_SCALES = {
  m: { label: 'm → m (1:1)', lengthDiv: 1, areaDiv: 1 },
  mm: { label: 'mm → m', lengthDiv: 1000, areaDiv: 1000000 },
  cm: { label: 'cm → m', lengthDiv: 100, areaDiv: 10000 }
};

// Helper format number
const fmt = (num, digits = 2) => {
  if (num === undefined || num === null || Number.isNaN(Number(num))) return '0.00';
  return Number(num).toLocaleString('en-US', {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits
  });
};

// Infer category from CAD entity layer / type / XData
function inferCategoryFromCadEntity(cadEnt) {
  const data = cadEnt.data || {};
  if (data.category && CATEGORY_CONFIG[data.category]) {
    return data.category;
  }
  const lyr = String(cadEnt.lyr || cadEnt.layer || '').toUpperCase();
  const ty = String(cadEnt.ty || cadEnt.type || '').toUpperCase();

  if (lyr.includes('WALL')) return 'wall';
  if (lyr.includes('BEAM') || lyr.includes('BALOK')) return 'beam';
  if (lyr.includes('COL') || lyr.includes('KOLOM')) return 'column';
  if (lyr.includes('SLAB') || lyr.includes('PLAT') || lyr.includes('FLOOR')) return 'slab';
  if (lyr.includes('STEEL') || lyr.includes('BAJA') || lyr.includes('WF')) return 'weight';
  if (ty.includes('BLOCKREFERENCE') || ty.includes('INSERT')) return 'count';
  if (ty.includes('HATCH') || ty.includes('REGION')) return 'area';
  if (cadEnt.a > 0 && (!cadEnt.l || lyr.includes('AREA') || lyr.includes('ROOM'))) return 'area';
  return 'length';
}

// Compute quantity in meters / m² / count from raw CAD entity based on active category & unit scale
function computeEntityBaseQuantity(ent, category, weightEntType, unitScaleKey) {
  if (ent.customQuantity !== undefined && ent.customQuantity !== null) {
    return Number(ent.customQuantity);
  }
  const scale = UNIT_SCALES[unitScaleKey] || UNIT_SCALES.mm;

  if (category === 'count') {
    return 1;
  }

  const usesArea =
    category === 'area' ||
    category === 'slab' ||
    (category === 'weight' && weightEntType === 'surface');

  if (usesArea) {
    if (ent.rawArea !== undefined && ent.rawArea > 0) {
      return Number((ent.rawArea / scale.areaDiv).toFixed(4));
    }
    return Number(ent.quantity || 0);
  } else {
    if (ent.rawLength !== undefined && ent.rawLength > 0) {
      return Number((ent.rawLength / scale.lengthDiv).toFixed(4));
    }
    return Number(ent.quantity || 0);
  }
}

// Recursively remove empty string (""), null, and undefined values from an object or array.
// Empty nested objects/arrays are pruned so no empty shells or empty keys are stored in CAD.
function removeEmptyValues(obj) {
  if (Array.isArray(obj)) {
    const cleanedArr = obj
      .map(removeEmptyValues)
      .filter((v) => v !== undefined && v !== null && !(typeof v === 'object' && Object.keys(v).length === 0));
    return cleanedArr.length > 0 ? cleanedArr : undefined;
  }
  if (obj !== null && typeof obj === 'object') {
    const cleanedObj = {};
    for (const [key, value] of Object.entries(obj)) {
      if (value === null || value === undefined) {
        continue;
      }
      if (typeof value === 'string' && value.trim() === '') {
        continue;
      }
      if (typeof value === 'object') {
        const sub = removeEmptyValues(value);
        if (sub !== undefined) {
          cleanedObj[key] = sub;
        }
      } else {
        cleanedObj[key] = value;
      }
    }
    return Object.keys(cleanedObj).length > 0 ? cleanedObj : undefined;
  }
  return obj;
}

export default function App() {
  // 1. CAD Live Connection State
  const [cadConnected, setCadConnected] = useState(false);
  const [cadPlatform, setCadPlatform] = useState('AutoCAD / GstarCAD');
  const [cadDwgName, setCadDwgName] = useState('Structural_Plan_L02.dwg');
  const [cadLayers, setCadLayers] = useState([]);
  const [ipcPort, setIpcPort] = useState(8123);
  const [unitScale, setUnitScale] = useState('m');
  const [alwaysOnTop, setAlwaysOnTop] = useState(true);
  const [autoWriteXdata, setAutoWriteXdata] = useState(false);
  const [syncStatusMsg, setSyncStatusMsg] = useState('');
  const lastVersionRef = useRef(0);

  // 2. State Location Group (location: 'Rumah', floor: 'Lt.1', Area: 'K. Tidur')
  const [location, setLocation] = useState({
    Location: '',
    Floor: '',
    Area: ''
  });

  // 3. State CAD Entities & Selection
  const [entities, setEntities] = useState([]);
  const [selectedIds, setSelectedIds] = useState([]);

  // 4. State Types Group (Code, Category, Entity Type, XData Evaluation Mode)
  const [qtoCode, setQtoCode] = useState('');
  const [qtoCategory, setQtoCategory] = useState('length');
  const [weightEntityType, setWeightEntityType] = useState('line'); // 'line' | 'surface'
  const [xdataMode, setXdataMode] = useState('uniform'); // 'uniform' | 'per_entity'

  // 5. State Quantities Factors (nr: 1, margin: 1.1, overhead: 1.2)
  // Default to nr: 1, qty: [1] per user specification
  const [factors, setFactors] = useState([
    { id: 'f-1', name: 'nr', value: 1 }
  ]);

  const handleAddFactorRow = () => {
    const defaultNames = ['nr', 'margin', 'overhead', 'waste', 'factor', 'mult'];
    const usedNames = factors.map((f) => f.name);
    const nextName = defaultNames.find((n) => !usedNames.includes(n)) || `factor_${factors.length + 1}`;
    setFactors((prev) => [
      ...prev,
      { id: `f-${Date.now()}`, name: nextName, value: 1 }
    ]);
  };

  const handleUpdateFactor = (id, field, val) => {
    setFactors((prev) =>
      prev.map((f) => {
        if (f.id === id) {
          if (field === 'value') {
            const num = parseFloat(val);
            return { ...f, value: val === '' || Number.isNaN(num) ? val : num };
          }
          return { ...f, [field]: val };
        }
        return f;
      })
    );
  };

  const handleRemoveFactorRow = (id) => {
    if (factors.length <= 1) return;
    setFactors((prev) => prev.filter((f) => f.id !== id));
  };

  // Product of all factors: nr * margin * overhead * ...
  const factorProduct = useMemo(() => {
    if (factors.length === 0) return 1;
    return factors.reduce((prod, f) => {
      const v = parseFloat(f.value);
      return prod * (Number.isNaN(v) ? 1 : v);
    }, 1);
  }, [factors]);

  // Array of factor values for XData qty: [1, 1] or [1, 1.1, 1.2]
  const qtyFactorsArray = useMemo(() => {
    return factors.map((f) => {
      const v = parseFloat(f.value);
      return Number.isNaN(v) ? 1 : v;
    });
  }, [factors]);

  // Global/Uniform XData values for the Property Box
  const [globalXdata, setGlobalXdata] = useState({
    wh: 3.4,
    wpl: 29.6,
    wpa: 5.4,
    w: 0.3,
    h: 0.6,
    t: 0.15,
    swpl: 42.5,
    swpa: 18.5
  });

  // Saved QTO Schedule Items
  const [savedSchedule, setSavedSchedule] = useState([]);
  const [showScheduleDrawer, setShowScheduleDrawer] = useState(false);
  const [showJsonModal, setShowJsonModal] = useState(false);
  const [copiedJson, setCopiedJson] = useState(false);

  // Collapsible Palette Sections (AutoCAD OPM Style)
  const [openSections, setOpenSections] = useState({
    location: true,
    types: true,
    xdata: true,
    quantities: true,
    output: true
  });

  const toggleSection = (key) => {
    setOpenSections((prev) => ({ ...prev, [key]: !prev[key] }));
  };

  // Convert CAD entities from grx-script IPC snapshot into QTO Property Box entity items
  const applyCadSnapshot = useCallback(
    (snapshot) => {
      if (!snapshot) return;
      setCadConnected(true);
      if (snapshot.platform) setCadPlatform(snapshot.platform);
      if (snapshot.dwg) setCadDwgName(snapshot.dwg);
      if (Array.isArray(snapshot.layers)) {
        setCadLayers(snapshot.layers);
      }

      if (!Array.isArray(snapshot.entities)) return;
      const cadEnts = snapshot.entities;

      if (cadEnts.length === 0) {
        setEntities([]);
        setSelectedIds([]);
        setLocation({ Location: '', Floor: '', Area: '' });
        setQtoCode('');
        return;
      }

      const first = cadEnts[0];
      const firstData = first.data || {};
      const firstProps = firstData.props || firstData;
      const inferredCat = inferCategoryFromCadEntity(first);
      const scale = UNIT_SCALES[unitScale] || UNIT_SCALES.mm;

      const getProps = (e) => {
        const d = e.data || {};
        return d.props || d;
      };

      // Multi-Select Resolution:
      // If multiple objects are selected and their values differ, display '<Varies>'
      const resolveMulti = (getter) => {
        if (!cadEnts || cadEnts.length === 0) return '';
        const firstVal = getter(cadEnts[0]);
        for (let i = 1; i < cadEnts.length; i++) {
          if (getter(cadEnts[i]) !== firstVal) {
            return '<Varies>';
          }
        }
        return firstVal;
      };

      const resolvedLocation = resolveMulti((e) => (getProps(e).location ?? getProps(e).Location ?? getProps(e).building ?? getProps(e).Building ?? ''));
      const resolvedFloor = resolveMulti((e) => (getProps(e).floor ?? getProps(e).Floor ?? ''));
      const resolvedArea = resolveMulti((e) => (getProps(e).Area ?? getProps(e).area ?? getProps(e).room ?? getProps(e).Room ?? ''));

      // Resolve category
      const resolvedCat = resolveMulti((e) => {
        const p = getProps(e);
        if (p.types && typeof p.types === 'object') {
          const typeKeys = Object.keys(p.types);
          if (typeKeys.length > 0) {
            // Keep current UI category if present on entity
            if (qtoCategory && typeKeys.includes(qtoCategory)) return qtoCategory;
            // Prefer explicit non-length types (wall, beam, column, slab, weight, count, area)
            const nonLength = typeKeys.find((k) => k !== 'length');
            if (nonLength) return nonLength;
            return typeKeys[0];
          }
        }
        return p.category ?? inferCategoryFromCadEntity(e);
      });

      // Resolve Code for this category
      const resolvedCode = resolveMulti((e) => {
        const p = getProps(e);
        const types = p.types || {};
        const catKey = resolvedCat !== '<Varies>' ? resolvedCat : inferredCat;
        const catList = types[catKey];
        const catItem = Array.isArray(catList) ? catList[0] : catList;
        if (catItem?.code) return String(catItem.code);
        if (p.code !== undefined && p.code !== null && String(p.code) !== '') return String(p.code);
        if (p.Code !== undefined && p.Code !== null && String(p.Code) !== '') return String(p.Code);
        return '';
      });

      setLocation({
        Location: resolvedLocation,
        Floor: resolvedFloor,
        Area: resolvedArea
      });

      setQtoCode(resolvedCode);

      if (resolvedCat !== '<Varies>') {
        setQtoCategory(resolvedCat);
      } else {
        setQtoCategory('<Varies>');
      }

      // Check if active category has parameters or factors defined in props.types[category]
      const catKey = resolvedCat !== '<Varies>' ? resolvedCat : inferredCat;
      const activeCatList = firstProps.types?.[catKey];
      const activeTypeData = Array.isArray(activeCatList) ? activeCatList[0] : (activeCatList || {});

      // Load parameters for active category
      if (catKey === 'wall') {
        const whVal = activeTypeData.height ?? activeTypeData.wh ?? firstData.wh ?? firstData.xdata?.wh;
        if (whVal !== undefined) setGlobalXdata((prev) => ({ ...prev, wh: Number(whVal) }));
      } else if (catKey === 'beam') {
        const wVal = activeTypeData.w ?? firstData.w ?? firstData.xdata?.w;
        const hVal = activeTypeData.h ?? firstData.h ?? firstData.xdata?.h;
        const swplVal = activeTypeData.swpl ?? firstData.swpl ?? firstData.xdata?.swpl;
        setGlobalXdata((prev) => ({
          ...prev,
          ...(wVal !== undefined ? { w: Number(wVal) } : {}),
          ...(hVal !== undefined ? { h: Number(hVal) } : {}),
          ...(swplVal !== undefined ? { swpl: Number(swplVal) } : {})
        }));
      } else if (catKey === 'column') {
        const wVal = activeTypeData.w ?? firstData.w ?? firstData.xdata?.w;
        const hVal = activeTypeData.h ?? firstData.h ?? firstData.xdata?.h;
        const swplVal = activeTypeData.swpl ?? firstData.swpl ?? firstData.xdata?.swpl;
        setGlobalXdata((prev) => ({
          ...prev,
          ...(wVal !== undefined ? { w: Number(wVal) } : {}),
          ...(hVal !== undefined ? { h: Number(hVal) } : {}),
          ...(swplVal !== undefined ? { swpl: Number(swplVal) } : {})
        }));
      } else if (catKey === 'slab') {
        const tVal = activeTypeData.t ?? firstData.t ?? firstData.xdata?.t;
        const swpaVal = activeTypeData.swpa ?? firstData.swpa ?? firstData.xdata?.swpa;
        setGlobalXdata((prev) => ({
          ...prev,
          ...(tVal !== undefined ? { t: Number(tVal) } : {}),
          ...(swpaVal !== undefined ? { swpa: Number(swpaVal) } : {})
        }));
      } else if (catKey === 'weight') {
        const wplVal = activeTypeData.wpl ?? firstData.wpl ?? firstData.xdata?.wpl;
        const wpaVal = activeTypeData.wpa ?? firstData.wpa ?? firstData.xdata?.wpa;
        setGlobalXdata((prev) => ({
          ...prev,
          ...(wplVal !== undefined ? { wpl: Number(wplVal) } : {}),
          ...(wpaVal !== undefined ? { wpa: Number(wpaVal) } : {})
        }));
        if (activeTypeData.entity_type) {
          setWeightEntityType(activeTypeData.entity_type);
        }
      }

      // Load factors (default to nr: 1, [1])
      if (activeTypeData.qty && Array.isArray(activeTypeData.qty) && activeTypeData.qty.length > 0) {
        const factorNames = ['nr', 'margin', 'overhead', 'waste', 'factor'];
        setFactors(
          activeTypeData.qty.map((qVal, idx) => ({
            id: `f-${idx + 1}`,
            name: factorNames[idx] || `factor_${idx + 1}`,
            value: Number(qVal) || 1
          }))
        );
      } else {
        setFactors([{ id: 'f-1', name: 'nr', value: 1 }]);
      }

      const firstXdata = {
        ...(firstData.xdata || {}),
        ...(firstData.wh !== undefined ? { wh: Number(firstData.wh) } : {}),
        ...(firstData.w !== undefined ? { w: Number(firstData.w) } : {}),
        ...(firstData.h !== undefined ? { h: Number(firstData.h) } : {}),
        ...(firstData.t !== undefined ? { t: Number(firstData.t) } : {}),
        ...(firstData.wpl !== undefined ? { wpl: Number(firstData.wpl) } : {}),
        ...(firstData.wpa !== undefined ? { wpa: Number(firstData.wpa) } : {}),
        ...(firstData.swpl !== undefined ? { swpl: Number(firstData.swpl) } : {}),
        ...(firstData.swpa !== undefined ? { swpa: Number(firstData.swpa) } : {})
      };

      if (Object.keys(firstXdata).length > 0) {
        setGlobalXdata((prev) => ({ ...prev, ...firstXdata }));
      }

      if (firstData.entity_type === 'surface' || firstData.entity_type === 'line') {
        setWeightEntityType(firstData.entity_type);
      }

      const mapped = cadEnts.map((e) => {
        const d = e.data || {};
        const entXdata = {
          wh: d.xdata?.wh ?? d.wh ?? globalXdata.wh,
          wpl: d.xdata?.wpl ?? d.wpl ?? globalXdata.wpl,
          wpa: d.xdata?.wpa ?? d.wpa ?? globalXdata.wpa,
          w: d.xdata?.w ?? d.w ?? globalXdata.w,
          h: d.xdata?.h ?? d.h ?? globalXdata.h,
          t: d.xdata?.t ?? d.t ?? globalXdata.t,
          swpl: d.xdata?.swpl ?? d.swpl ?? globalXdata.swpl,
          swpa: d.xdata?.swpa ?? d.swpa ?? globalXdata.swpa
        };
        const rawL = typeof e.l === 'number' ? e.l : 0;
        const rawA = typeof e.a === 'number' ? e.a : 0;
        const tyClean = String(e.ty || 'Entity').replace(/^AcDb/, '');
        const usesArea =
          inferredCat === 'area' ||
          inferredCat === 'slab' ||
          (inferredCat === 'weight' && d.entity_type === 'surface');

        const baseQty =
          inferredCat === 'count'
            ? 1
            : usesArea
            ? Number((rawA / scale.areaDiv).toFixed(3))
            : Number((rawL / scale.lengthDiv).toFixed(3));

        return {
          id: e.hand,
          handle: e.hand,
          layer: e.lyr || '0',
          dxfType: tyClean,
          entity_type: usesArea ? 'surface' : inferredCat === 'count' ? 'count' : 'line',
          label: `${tyClean} (${e.lyr || '0'})`,
          categoryPreset: inferredCat,
          codePreset: d.props?.types?.[inferredCat]?.code || d.props?.code || d.code || '',
          rawLength: rawL,
          rawArea: rawA,
          quantity: baseQty,
          multiplier: d.multiplier !== undefined ? Number(d.multiplier) : 1,
          xdata: entXdata,
          rawCadData: d,
          fromCad: true
        };
      });

      setEntities(mapped);
      setSelectedIds(mapped.map((m) => m.id));
    },
    [unitScale, globalXdata]
  );

  const applyCadSnapshotRef = useRef(applyCadSnapshot);
  useEffect(() => {
    applyCadSnapshotRef.current = applyCadSnapshot;
  }, [applyCadSnapshot]);

  // Poll AutoCAD / GstarCAD selection state safely without overlapping requests
  useEffect(() => {
    let active = true;
    let isPolling = false;
    const pollCad = async () => {
      if (isPolling) return;
      isPolling = true;
      try {
        const res = await invoke('cad_poll_selection', {
          lastVersion: lastVersionRef.current,
          port: ipcPort
        });
        if (!active || !res) return;
        if (res.error) {
          return;
        }
        setCadConnected(true);
        if (res.platform) setCadPlatform(res.platform);
        if (res.dwg) setCadDwgName(res.dwg);
        if (res.changed) {
          lastVersionRef.current = res.version || 0;
          applyCadSnapshotRef.current(res);
        }
      } catch (_err) {
        if (active) {
          setCadConnected(false);
        }
      } finally {
        isPolling = false;
      }
    };

    pollCad();
    const timer = setInterval(pollCad, 800);
    return () => {
      active = false;
      clearInterval(timer);
    };
  }, [ipcPort]);

  // Recompute quantities when unitScale or qtoCategory or weightEntityType changes for CAD entities
  useEffect(() => {
    setEntities((prev) =>
      prev.map((ent) => {
        if (!ent.fromCad && ent.rawLengthMm === undefined) return ent;
        const rawL = ent.rawLength !== undefined ? ent.rawLength : ent.rawLengthMm || 0;
        const rawA = ent.rawArea !== undefined ? ent.rawArea : ent.rawAreaMm2 || 0;
        const scale = UNIT_SCALES[unitScale] || UNIT_SCALES.mm;

        if (qtoCategory === 'count') {
          return { ...ent, quantity: 1 };
        }
        const usesArea =
          qtoCategory === 'area' ||
          qtoCategory === 'slab' ||
          (qtoCategory === 'weight' && weightEntityType === 'surface');

        if (usesArea && rawA > 0) {
          return { ...ent, quantity: Number((rawA / scale.areaDiv).toFixed(3)) };
        }
        if (!usesArea && rawL > 0) {
          return { ...ent, quantity: Number((rawL / scale.lengthDiv).toFixed(3)) };
        }
        return ent;
      })
    );
  }, [unitScale, qtoCategory, weightEntityType]);

  // Toggle Always-On-Top in Tauri window
  const handleToggleAlwaysOnTop = async () => {
    const next = !alwaysOnTop;
    setAlwaysOnTop(next);
    try {
      await invoke('set_always_on_top', { alwaysOnTop: next });
    } catch (_e) {
      // Fallback if running in browser dev preview
    }
  };

  // Selected entity objects
  const selectedEntities = useMemo(
    () => entities.filter((e) => selectedIds.includes(e.id)),
    [entities, selectedIds]
  );

  // Handle category switch: load existing code and parameters for the category if already set on entity
  const handleCategorySelect = useCallback(
    (newCat) => {
      setQtoCategory(newCat);
      if (selectedEntities.length > 0) {
        const first = selectedEntities[0];
        const p = first.rawCadData?.props || first.rawCadData || {};
        const catList = p.types?.[newCat];
        const catData = Array.isArray(catList) ? catList[0] : catList;
        if (catData?.code) {
          setQtoCode(catData.code);
        } else if (p.code) {
          setQtoCode(p.code);
        } else {
          setQtoCode('');
        }

        if (catData?.qty && Array.isArray(catData.qty) && catData.qty.length > 0) {
          const factorNames = ['nr', 'margin', 'overhead', 'waste', 'factor'];
          setFactors(
            catData.qty.map((qVal, idx) => ({
              id: `f-${idx + 1}`,
              name: factorNames[idx] || `factor_${idx + 1}`,
              value: Number(qVal) || 1
            }))
          );
        } else {
          setFactors([{ id: 'f-1', name: 'nr', value: 1 }]);
        }

        if (newCat === 'wall') {
          const whVal = catData?.height ?? catData?.wh ?? p.wh;
          if (whVal !== undefined) setGlobalXdata((prev) => ({ ...prev, wh: Number(whVal) }));
        } else if (newCat === 'beam') {
          if (catData?.w !== undefined) setGlobalXdata((prev) => ({ ...prev, w: Number(catData.w) }));
          if (catData?.h !== undefined) setGlobalXdata((prev) => ({ ...prev, h: Number(catData.h) }));
          if (catData?.swpl !== undefined) setGlobalXdata((prev) => ({ ...prev, swpl: Number(catData.swpl) }));
        } else if (newCat === 'column') {
          if (catData?.w !== undefined) setGlobalXdata((prev) => ({ ...prev, w: Number(catData.w) }));
          if (catData?.h !== undefined) setGlobalXdata((prev) => ({ ...prev, h: Number(catData.h) }));
          if (catData?.swpl !== undefined) setGlobalXdata((prev) => ({ ...prev, swpl: Number(catData.swpl) }));
        } else if (newCat === 'slab') {
          if (catData?.t !== undefined) setGlobalXdata((prev) => ({ ...prev, t: Number(catData.t) }));
          if (catData?.swpa !== undefined) setGlobalXdata((prev) => ({ ...prev, swpa: Number(catData.swpa) }));
        } else if (newCat === 'weight') {
          if (catData?.wpl !== undefined) setGlobalXdata((prev) => ({ ...prev, wpl: Number(catData.wpl) }));
          if (catData?.wpa !== undefined) setGlobalXdata((prev) => ({ ...prev, wpa: Number(catData.wpa) }));
          if (catData?.entity_type) setWeightEntityType(catData.entity_type);
        }
      } else {
        setFactors([{ id: 'f-1', name: 'nr', value: 1 }]);
      }
    },
    [selectedEntities]
  );

  // Select all entities on a layer in CAD (or Demo preset)
  const handleSelectLayerInCad = async (layerName, presetCategory, presetCode) => {
    if (cadConnected) {
      try {
        setSyncStatusMsg(`Selecting layer ${layerName} in ${cadPlatform}...`);
        const res = await invoke('cad_select_layer', {
          layer: layerName,
          port: ipcPort
        });
        if (res && !res.error) {
          lastVersionRef.current = res.version || 0;
          applyCadSnapshot(res);
          if (presetCategory) setQtoCategory(presetCategory);
          if (presetCode) setQtoCode(presetCode);
          setSyncStatusMsg(`Selected ${res.entities?.length || 0} object(s) on ${layerName}`);
          setTimeout(() => setSyncStatusMsg(''), 2500);
          return;
        }
      } catch (_err) {
        // Fallback to demo preset below
      }
    }

    // Offline / Demo mode preset selection
    const matching = INITIAL_CAD_ENTITIES.filter((e) => e.layer === layerName);
    if (matching.length > 0) {
      setEntities(INITIAL_CAD_ENTITIES);
      setSelectedIds(matching.map((e) => e.id));
      if (presetCategory) setQtoCategory(presetCategory);
      if (presetCode) setQtoCode(presetCode);
      const sampleXdata = matching[0].xdata || {};
      setGlobalXdata((prev) => ({ ...prev, ...sampleXdata }));
      if (presetCategory === 'weight') {
        setWeightEntityType(matching[0].entity_type === 'surface' ? 'surface' : 'line');
      }
    }
  };

  // Focus / Zoom to a single entity in AutoCAD / GstarCAD
  const handleFocusEntityInCad = async (handleStr) => {
    if (!cadConnected || !handleStr) return;
    try {
      await invoke('cad_select_handles', {
        handles: [handleStr],
        zoom: true,
        port: ipcPort
      });
    } catch (_e) {
      // Ignore error in offline mode
    }
  };

  // Manual Force Refresh from CAD
  const handleManualRefresh = async () => {
    lastVersionRef.current = 0;
    try {
      const res = await invoke('cad_poll_selection', {
        lastVersion: 0,
        port: ipcPort
      });
      if (res && !res.error) {
        lastVersionRef.current = res.version || 0;
        applyCadSnapshot(res);
        setSyncStatusMsg(`Synced ${res.entities?.length || 0} object(s) from ${res.platform || 'CAD'}`);
        setTimeout(() => setSyncStatusMsg(''), 2000);
      }
    } catch (_e) {
      setCadConnected(false);
      setSyncStatusMsg('CAD IPC not reachable (127.0.0.1:8123) — Demo Mode');
      setTimeout(() => setSyncStatusMsg(''), 2500);
    }
  };

  // Update Global XData & push to selected entities
  const handleGlobalXdataChange = (key, rawVal) => {
    const num = parseFloat(rawVal);
    const val = Number.isNaN(num) ? 0 : num;
    setGlobalXdata((prev) => ({ ...prev, [key]: val }));
    setEntities((prev) =>
      prev.map((ent) =>
        selectedIds.includes(ent.id)
          ? { ...ent, xdata: { ...ent.xdata, [key]: val } }
          : ent
      )
    );
  };

  // Update individual entity quantity or xdata
  const updateEntityField = (id, field, rawVal) => {
    const num = parseFloat(rawVal);
    const val = Number.isNaN(num) ? 0 : num;
    setEntities((prev) =>
      prev.map((ent) => (ent.id === id ? { ...ent, [field]: val } : ent))
    );
  };

  const updateEntityXdata = (id, xkey, rawVal) => {
    const num = parseFloat(rawVal);
    const val = Number.isNaN(num) ? 0 : num;
    setEntities((prev) =>
      prev.map((ent) =>
        ent.id === id ? { ...ent, xdata: { ...ent.xdata, [xkey]: val } } : ent
      )
    );
  };

  // Add manual quantity row to current selection
  const addCustomEntityRow = () => {
    const newId = `ENT-CUSTOM-${Date.now().toString().slice(-3)}`;
    const newEnt = {
      id: newId,
      handle: Math.floor(100 + Math.random() * 899)
        .toString(16)
        .toUpperCase(),
      layer: selectedEntities[0]?.layer || 'QTO-USER',
      dxfType: qtoCategory === 'slab' || qtoCategory === 'area' ? 'HATCH' : 'LWPOLYLINE',
      entity_type: qtoCategory === 'slab' || qtoCategory === 'area' ? 'surface' : 'line',
      label: `Custom ${qtoCategory.toUpperCase()} Entity`,
      categoryPreset: qtoCategory,
      codePreset: qtoCode,
      quantity: qtoCategory === 'count' ? 1 : 6.0,
      multiplier: 1,
      xdata: { ...globalXdata }
    };
    setEntities((prev) => [...prev, newEnt]);
    setSelectedIds((prev) => [...prev, newId]);
  };

  // ============================================================================
  // 3. QTO CALCULATION ENGINE (Exact Implementation of Prompt Rules)
  // ============================================================================
  const qtoResult = useMemo(() => {
    const items = selectedEntities;
    // Base geometry sum across selected entities (length in m, area in m², or count)
    const baseGeometry = items.reduce((acc, ent) => acc + (Number(ent.quantity) || 0), 0);
    const sumProductBase = baseGeometry;
    const quantitiesArray = qtyFactorsArray;

    const getX = (ent, key) => {
      if (xdataMode === 'per_entity' && ent.xdata && ent.xdata[key] !== undefined) {
        return Number(ent.xdata[key]) || 0;
      }
      return Number(globalXdata[key]) || 0;
    };

    if (qtoCategory === 'length') {
      const val = baseGeometry * factorProduct;
      return {
        mode: 'single',
        primaryLabel: 'total_length',
        primaryValue: val,
        primaryUnit: 'm',
        quantitiesArray,
        sumProductBase,
        defaultValuePayload: Number(val.toFixed(3))
      };
    }

    if (qtoCategory === 'area') {
      const val = baseGeometry * factorProduct;
      return {
        mode: 'single',
        primaryLabel: 'total_area',
        primaryValue: val,
        primaryUnit: 'm²',
        quantitiesArray,
        sumProductBase,
        defaultValuePayload: Number(val.toFixed(3))
      };
    }

    if (qtoCategory === 'wall') {
      const wh = getX(items[0] || {}, 'wh') || 3.0;
      const wallArea = baseGeometry * wh * factorProduct;
      return {
        mode: 'single',
        primaryLabel: 'wall_area',
        primaryValue: wallArea,
        primaryUnit: 'm²',
        secondaryNote: `Base length: ${fmt(sumProductBase)} m × wh (${wh}m) × factor (${fmt(factorProduct, 2)})`,
        quantitiesArray,
        sumProductBase,
        defaultValuePayload: Number(wallArea.toFixed(3))
      };
    }

    if (qtoCategory === 'count') {
      const countVal = (items.length || 1) * factorProduct;
      return {
        mode: 'single',
        primaryLabel: 'total_count',
        primaryValue: countVal,
        primaryUnit: 'unit',
        quantitiesArray,
        sumProductBase: items.length || 1,
        defaultValuePayload: Number(countVal.toFixed(0))
      };
    }

    if (qtoCategory === 'weight') {
      const wFactor = weightEntityType === 'line' ? (getX(items[0] || {}, 'wpl') || 29.6) : (getX(items[0] || {}, 'wpa') || 5.4);
      const totalWeight = baseGeometry * wFactor * factorProduct;
      return {
        mode: 'single',
        primaryLabel:
          weightEntityType === 'line'
            ? 'total(length * wpl)'
            : 'total(area * wpa)',
        primaryValue: totalWeight,
        primaryUnit: 'kg',
        secondaryNote:
          weightEntityType === 'line'
            ? `line (${fmt(sumProductBase)} m × ${wFactor} kg/m × ${fmt(factorProduct, 2)})`
            : `surface (${fmt(sumProductBase)} m² × ${wFactor} kg/m² × ${fmt(factorProduct, 2)})`,
        quantitiesArray,
        sumProductBase,
        defaultValuePayload: Number(totalWeight.toFixed(3))
      };
    }

    if (qtoCategory === 'beam' || qtoCategory === 'column') {
      const w = getX(items[0] || {}, 'w') || 0.3;
      const h = getX(items[0] || {}, 'h') || 0.6;
      const swpl = getX(items[0] || {}, 'swpl') || 42.5;
      const concreteVolume = baseGeometry * w * h * factorProduct;
      const steelWeight = baseGeometry * swpl * factorProduct;
      return {
        mode: 'dual',
        concrete_volume: concreteVolume,
        steel_weight: steelWeight,
        quantitiesArray,
        sumProductBase,
        defaultValuePayload: {
          concrete_volume: Number(concreteVolume.toFixed(3)),
          steel_weight: Number(steelWeight.toFixed(3))
        }
      };
    }

    if (qtoCategory === 'slab') {
      const t = getX(items[0] || {}, 't') || 0.15;
      const swpa = getX(items[0] || {}, 'swpa') || 18.5;
      const concreteVolume = baseGeometry * t * factorProduct;
      const steelWeight = baseGeometry * swpa * factorProduct;
      return {
        mode: 'dual',
        concrete_volume: concreteVolume,
        steel_weight: steelWeight,
        quantitiesArray,
        sumProductBase,
        defaultValuePayload: {
          concrete_volume: Number(concreteVolume.toFixed(3)),
          steel_weight: Number(steelWeight.toFixed(3))
        }
      };
    }

    const defaultVal = baseGeometry * factorProduct;
    return {
      mode: 'single',
      primaryLabel: 'value',
      primaryValue: defaultVal,
      primaryUnit: '',
      quantitiesArray,
      sumProductBase,
      defaultValuePayload: defaultVal
    };
  }, [selectedEntities, qtoCategory, weightEntityType, xdataMode, globalXdata, factorProduct, qtyFactorsArray]);

  // Active XData fields for current category
  const activeXdataFields = useMemo(() => {
    const cfg = CATEGORY_CONFIG[qtoCategory];
    if (!cfg) return [];
    if (qtoCategory === 'weight') {
      return cfg.xdataFields.filter((f) => f.entityFilter === weightEntityType);
    }
    return cfg.xdataFields;
  }, [qtoCategory, weightEntityType]);

  // JSON Schema Output matching user's specification:
  // { props: { location, floor, Area, types: { [cat]: [{ code, qty: [...] }] } } }
  // Empty values are removed from props and types
  const jsonOutput = useMemo(() => {
    const entX = {};
    activeXdataFields.forEach((f) => {
      entX[f.key] = globalXdata[f.key];
    });

    const activeCat = qtoCategory !== '<Varies>' ? qtoCategory : 'length';

    const currentTypeEntry = {
      code: qtoCode && qtoCode !== '<Varies>' ? qtoCode : '',
      ...(activeCat === 'wall' ? { height: Number(entX.wh ?? globalXdata.wh ?? 3.4) } : {}),
      ...(activeCat === 'beam' ? { w: Number(entX.w ?? globalXdata.w ?? 0.3), h: Number(entX.h ?? globalXdata.h ?? 0.6), swpl: Number(entX.swpl ?? globalXdata.swpl ?? 42.5) } : {}),
      ...(activeCat === 'column' ? { w: Number(entX.w ?? globalXdata.w ?? 0.5), h: Number(entX.h ?? globalXdata.h ?? 0.5), swpl: Number(entX.swpl ?? globalXdata.swpl ?? 68.0) } : {}),
      ...(activeCat === 'slab' ? { t: Number(entX.t ?? globalXdata.t ?? 0.15), swpa: Number(entX.swpa ?? globalXdata.swpa ?? 18.5) } : {}),
      ...(activeCat === 'weight' ? {
          ...(weightEntityType === 'line'
            ? { wpl: Number(entX.wpl ?? globalXdata.wpl ?? 29.6) }
            : { wpa: Number(entX.wpa ?? globalXdata.wpa ?? 5.4) }),
          entity_type: weightEntityType
        } : {}),
      qty: qtyFactorsArray
    };

    const raw = {
      props: {
        location: location.Location && location.Location !== '<Varies>' ? location.Location : '',
        floor: location.Floor && location.Floor !== '<Varies>' ? location.Floor : '',
        Area: location.Area && location.Area !== '<Varies>' ? location.Area : '',
        types: {
          [activeCat]: [currentTypeEntry]
        }
      }
    };

    return removeEmptyValues(raw) || { props: {} };
  }, [location, qtoCode, qtoCategory, weightEntityType, activeXdataFields, globalXdata, qtyFactorsArray]);

  // Write QTO XData to selected entities in AutoCAD / GstarCAD
  const handleApplyXdataToCad = useCallback(async () => {
    const cadHandles = selectedEntities.filter((e) => e.fromCad).map((e) => e.handle);
    if (!cadConnected || cadHandles.length === 0) {
      setSyncStatusMsg('Updated local QTO XData state');
      setTimeout(() => setSyncStatusMsg(''), 2000);
      return;
    }

    const activeXdataObj = {};
    activeXdataFields.forEach((f) => {
      activeXdataObj[f.key] = globalXdata[f.key];
    });

    const entityPatches = selectedEntities
      .filter((e) => e.fromCad)
      .map((ent) => {
        const origData = ent.rawCadData || {};
        const existingProps = origData.props || origData;
        const existingTypes = (origData.props && origData.props.types) ? { ...origData.props.types } : {};
        const entX = xdataMode === 'per_entity' ? { ...activeXdataObj, ...(ent.xdata || {}) } : activeXdataObj;

        // If field value is '<Varies>', DO NOT UPDATE that field (preserve entity's existing value!)
        const finalLocation = location.Location === '<Varies>'
          ? (existingProps.location ?? existingProps.Location ?? existingProps.building ?? existingProps.Building ?? '')
          : location.Location;

        const finalFloor = location.Floor === '<Varies>'
          ? (existingProps.floor ?? existingProps.Floor ?? '')
          : location.Floor;

        const finalArea = location.Area === '<Varies>'
          ? (existingProps.Area ?? existingProps.area ?? existingProps.Room ?? existingProps.room ?? '')
          : location.Area;

        const finalCategory = qtoCategory === '<Varies>'
          ? ent.categoryPreset
          : qtoCategory;

        const existingCatList = Array.isArray(existingTypes[finalCategory])
          ? existingTypes[finalCategory]
          : (existingTypes[finalCategory] ? [existingTypes[finalCategory]] : []);

        const finalCode = qtoCode === '<Varies>'
          ? (existingCatList[0]?.code ?? origData.code ?? '')
          : qtoCode;

        // Construct category entry under types[category]
        const typeEntry = {
          code: finalCode || '',
          ...(finalCategory === 'wall' ? { height: Number(entX.wh ?? globalXdata.wh ?? 3.4) } : {}),
          ...(finalCategory === 'beam' ? { w: Number(entX.w ?? globalXdata.w ?? 0.3), h: Number(entX.h ?? globalXdata.h ?? 0.6), swpl: Number(entX.swpl ?? globalXdata.swpl ?? 42.5) } : {}),
          ...(finalCategory === 'column' ? { w: Number(entX.w ?? globalXdata.w ?? 0.5), h: Number(entX.h ?? globalXdata.h ?? 0.5), swpl: Number(entX.swpl ?? globalXdata.swpl ?? 68.0) } : {}),
          ...(finalCategory === 'slab' ? { t: Number(entX.t ?? globalXdata.t ?? 0.15), swpa: Number(entX.swpa ?? globalXdata.swpa ?? 18.5) } : {}),
          ...(finalCategory === 'weight' ? {
              ...(weightEntityType === 'line'
                ? { wpl: Number(entX.wpl ?? globalXdata.wpl ?? 29.6) }
                : { wpa: Number(entX.wpa ?? globalXdata.wpa ?? 5.4) }),
              entity_type: weightEntityType
            } : {}),
          qty: qtyFactorsArray
        };

        const updatedTypes = {
          [finalCategory]: [typeEntry]
        };

        const rawData = {
          props: {
            location: finalLocation,
            floor: finalFloor,
            Area: finalArea,
            types: updatedTypes
          }
        };

        return {
          handle: ent.handle,
          data: removeEmptyValues(rawData) || { props: {} }
        };
      });

    try {
      const res = await invoke('cad_apply_qto_xdata', {
        patches: entityPatches,
        port: ipcPort
      });
      if (res && !res.error) {
        setSyncStatusMsg(`XData written to ${entityPatches.length} entity(s) in ${cadPlatform}`);
        // Immediately reflect the updated props in local entities state
        setEntities((prev) =>
          prev.map((e) => {
            const patch = entityPatches.find((p) => p.handle === e.handle);
            if (patch) {
              return {
                ...e,
                rawCadData: patch.data
              };
            }
            return e;
          })
        );
      } else {
        setSyncStatusMsg(`XData error: ${res?.error || 'Failed'}`);
      }
    } catch (err) {
      setSyncStatusMsg(`Failed to write XData: ${String(err)}`);
    }
    setTimeout(() => setSyncStatusMsg(''), 2500);
  }, [
    selectedEntities,
    cadConnected,
    activeXdataFields,
    globalXdata,
    xdataMode,
    location,
    qtoCode,
    qtoCategory,
    weightEntityType,
    qtoResult,
    jsonOutput,
    ipcPort,
    cadPlatform
  ]);

  // Auto-sync XData when autoWriteXdata is enabled
  useEffect(() => {
    if (!autoWriteXdata || !cadConnected) return;
    const t = setTimeout(() => {
      handleApplyXdataToCad();
    }, 400);
    return () => clearTimeout(t);
  }, [autoWriteXdata, cadConnected, location, qtoCode, qtoCategory, weightEntityType, globalXdata, handleApplyXdataToCad]);

  // Global F5 shortcut to trigger 'Apply XData'
  useEffect(() => {
    const handleKeyDown = (e) => {
      if (e.key === 'F5' || e.code === 'F5') {
        e.preventDefault();
        handleApplyXdataToCad();
      }
    };
    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [handleApplyXdataToCad]);

  // Record current QTO calculation into schedule table
  const handleRecordSchedule = () => {
    const entry = {
      id: `QTO-${Date.now().toString().slice(-4)}`,
      location: location.Location,
      floor: location.Floor,
      area: location.Area,
      code: qtoCode,
      category: qtoCategory,
      count: selectedEntities.length,
      baseSum: qtoResult.sumProductBase,
      result:
        qtoResult.mode === 'dual'
          ? `Concrete: ${fmt(qtoResult.concrete_volume, 3)} m³ | Steel: ${fmt(qtoResult.steel_weight, 2)} kg`
          : `${fmt(qtoResult.primaryValue, 3)} ${qtoResult.primaryUnit}`
    };
    setSavedSchedule((prev) => [entry, ...prev]);
    setShowScheduleDrawer(true);
  };

  const handleCopyJson = () => {
    navigator.clipboard.writeText(JSON.stringify(jsonOutput, null, 2));
    setCopiedJson(true);
    setTimeout(() => setCopiedJson(false), 1800);
  };

  const activeCategoryConfig = CATEGORY_CONFIG[qtoCategory] || CATEGORY_CONFIG.length;

  return (
    <div className="flex flex-col h-screen w-full bg-[#252930] text-slate-200 font-sans select-none overflow-hidden">
      {/* ====================================================================
          1. COMPACT PALETTE HEADER BAR (AutoCAD / GstarCAD OPM Style)
      ==================================================================== */}
      <header className="bg-[#21252b] border-b border-[#111317] px-2.5 py-2 flex items-center justify-between gap-2 shrink-0">
        <div className="flex items-center gap-2 min-w-0">
          <div className="flex items-center gap-1.5 bg-[#c72c2c] text-white px-2 py-0.5 rounded text-[11px] font-bold tracking-wider shadow-sm shrink-0">
            <Sliders className="w-3 h-3" />
            <span>QTO</span>
          </div>
          <div className="min-w-0">
            <div className="flex items-center gap-1.5">
              <h1 className="text-xs font-bold uppercase tracking-wider text-slate-100 truncate">
                QTO Property Box
              </h1>
              <span
                onClick={handleManualRefresh}
                title={
                  cadConnected
                    ? `Connected to ${cadPlatform} (${cadDwgName}) on 127.0.0.1:${ipcPort}`
                    : 'Offline / Demo Simulator Mode (Click to retry CAD connection)'
                }
                className={`cursor-pointer inline-flex items-center gap-1 text-[10px] px-1.5 py-0.5 rounded font-mono border ${
                  cadConnected
                    ? 'bg-emerald-950/70 text-emerald-300 border-emerald-500/40'
                    : 'bg-amber-950/60 text-amber-300 border-amber-500/40'
                }`}
              >
                {cadConnected ? <Wifi className="w-2.5 h-2.5" /> : <WifiOff className="w-2.5 h-2.5" />}
                <span>{cadConnected ? cadPlatform : 'Demo'}</span>
              </span>
            </div>
            <p className="text-[10px] text-slate-400 truncate">
              {cadDwgName} • <span className="text-amber-400 font-mono">{selectedEntities.length} Selected</span>
            </p>
          </div>
        </div>

        {/* Right Controls: Unit Scale, Always-On-Top, Schedule, JSON */}
        <div className="flex items-center gap-1 shrink-0">
          {/* Unit Scale Selector (A3) */}
          <select
            value={unitScale}
            onChange={(e) => setUnitScale(e.target.value)}
            title="Konversi Satuan Gambar CAD ke Meter (m / m²)"
            className="bg-[#181a1f] text-amber-300 text-[11px] font-mono px-1.5 py-1 rounded border border-slate-700 focus:outline-none focus:border-amber-400"
          >
            {Object.entries(UNIT_SCALES).map(([k, v]) => (
              <option key={k} value={k}>
                {v.label}
              </option>
            ))}
          </select>

          <button
            onClick={handleManualRefresh}
            title="Refresh Selection from AutoCAD / GstarCAD"
            className="p-1.5 rounded bg-[#2c313a] hover:bg-[#353b45] border border-slate-700 text-slate-300 transition-colors"
          >
            <RefreshCw className="w-3.5 h-3.5" />
          </button>

          <button
            onClick={handleToggleAlwaysOnTop}
            title={alwaysOnTop ? 'Pinned Always-on-Top (Click to unpin)' : 'Pin Window Always-on-Top'}
            className={`p-1.5 rounded border transition-colors ${
              alwaysOnTop
                ? 'bg-amber-500/20 border-amber-500/60 text-amber-300'
                : 'bg-[#2c313a] border-slate-700 text-slate-400 hover:text-slate-200'
            }`}
          >
            {alwaysOnTop ? <Pin className="w-3.5 h-3.5" /> : <PinOff className="w-3.5 h-3.5" />}
          </button>

          <button
            onClick={() => setShowScheduleDrawer((prev) => !prev)}
            title="Toggle BOQ Schedule Drawer"
            className={`px-2 py-1 rounded text-[11px] font-medium border flex items-center gap-1 transition-colors ${
              showScheduleDrawer
                ? 'bg-sky-500/20 border-sky-500 text-sky-300'
                : 'bg-[#2c313a] border-slate-700 text-slate-300 hover:bg-[#353b45]'
            }`}
          >
            <Table className="w-3.5 h-3.5" />
            <span>{savedSchedule.length}</span>
          </button>

          <button
            onClick={() => setShowJsonModal(true)}
            title="Live JSON Schema Output"
            className="px-2 py-1 rounded text-[11px] font-medium bg-[#2c313a] border border-slate-700 text-slate-300 hover:bg-[#353b45] flex items-center gap-1 transition-colors"
          >
            <Code2 className="w-3.5 h-3.5 text-amber-400" />
            <span>JSON</span>
          </button>
        </div>
      </header>

      {/* ====================================================================
          2. QUICK SELECT LAYER / CATEGORY BAR (Top of OPM Palette)
      ==================================================================== */}
      <div className="p-2 bg-[#1c1f24] border-b border-[#141619] space-y-1.5 shrink-0">
        {/* Layer / Category Drop-down */}
        <div className="flex items-center gap-1.5">
          <Layers className="w-4 h-4 text-sky-400 shrink-0" />
          <select
            value={qtoCategory}
            onChange={(e) => {
              const cat = e.target.value;
              const preset = SELECTION_PRESETS.find((p) => p.category === cat);
              if (!cadConnected && preset) {
                handleSelectLayerInCad(preset.layer, preset.category, preset.code);
              } else {
                handleCategorySelect(cat);
              }
            }}
            className={`flex-1 bg-[#14161a] text-xs px-2 py-1.5 rounded border border-slate-700 focus:outline-none focus:border-amber-400 font-medium ${
              qtoCategory === '<Varies>' ? 'text-amber-400 italic' : 'text-slate-100'
            }`}
          >
            {qtoCategory === '<Varies>' && (
              <option value="<Varies>">&lt;Varies&gt; (Multiple Categories)</option>
            )}
            <option value="wall">Wall — Layer: A-WALL (Length × wh)</option>
            <option value="beam">Beam — Layer: S-BEAM (Concrete & Steel)</option>
            <option value="column">Column — Layer: S-COL (Concrete & Steel)</option>
            <option value="slab">Slab — Layer: S-SLAB (Area × t & swpa)</option>
            <option value="weight">Weight — Layer: S-STEEL (wpl / wpa)</option>
            <option value="count">Count — Layer: A-FIXT (Block Insert)</option>
            <option value="length">Length (Linear Measurement)</option>
            <option value="area">Area (Surface Measurement)</option>
          </select>

          {/* If connected to CAD and drawing has layers, allow selecting any CAD layer directly */}
          {cadConnected && cadLayers.length > 0 && (
            <select
              defaultValue=""
              onChange={(e) => {
                if (e.target.value) {
                  handleSelectLayerInCad(e.target.value);
                  e.target.value = '';
                }
              }}
              className="w-36 bg-[#14161a] text-sky-300 text-[11px] px-2 py-1.5 rounded border border-sky-500/30 focus:outline-none focus:border-sky-400"
              title="Select all objects on a CAD Layer"
            >
              <option value="" disabled>
                Select CAD Layer...
              </option>
              {cadLayers.map((l) => (
                <option key={l.layer} value={l.layer}>
                  {l.layer} ({l.count})
                </option>
              ))}
            </select>
          )}
        </div>

        {/* Category Quick Pills */}
        <div className="flex items-center gap-1 overflow-x-auto pb-0.5">
          {QUICK_CATEGORY_PILLS.map((pill) => {
            const isActive = qtoCategory === pill.id;
            const IconComp = pill.icon;
            return (
              <button
                key={pill.id}
                onClick={() => handleCategorySelect(pill.id)}
                className={`px-2 py-1 rounded text-[10px] font-semibold transition-all flex items-center gap-1.5 whitespace-nowrap ${
                  isActive
                    ? 'bg-amber-500 text-slate-950 shadow-sm'
                    : 'bg-[#282c34] text-slate-300 hover:bg-[#323842] hover:text-white'
                }`}
                title={`Set category to ${pill.label}`}
              >
                <IconComp className={`w-3.5 h-3.5 shrink-0 ${isActive ? 'text-slate-950' : 'text-sky-400'}`} />
                <span>{pill.label}</span>
              </button>
            );
          })}
        </div>
      </div>

      {/* Status Banner when syncing with AutoCAD / GstarCAD */}
      {syncStatusMsg && (
        <div className="bg-sky-950/90 border-b border-sky-500/40 px-3 py-1 text-[11px] text-sky-200 flex items-center justify-between shrink-0">
          <span>{syncStatusMsg}</span>
          <button onClick={() => setSyncStatusMsg('')} className="text-sky-400 hover:text-white">
            <X className="w-3 h-3" />
          </button>
        </div>
      )}

      {/* ====================================================================
          3. SCROLLABLE 2-COLUMN AUTOCAD OPM PROPERTY GRID
      ==================================================================== */}
      <div className="flex-1 overflow-y-auto divide-y divide-[#181a1f] text-xs">
        {/* ----------------------------------------------------------------
            GROUP 1: LOCATION (Building, Floor, Room)
        ---------------------------------------------------------------- */}
        <div>
          <button
            onClick={() => toggleSection('location')}
            className="w-full bg-[#2f3540] hover:bg-[#363d4a] px-3 py-1.5 flex items-center justify-between text-left font-semibold text-slate-200 transition-colors"
          >
            <span className="flex items-center gap-1.5">
              {openSections.location ? (
                <ChevronDown className="w-3.5 h-3.5 text-amber-400" />
              ) : (
                <ChevronRight className="w-3.5 h-3.5 text-slate-400" />
              )}
              <MapPin className="w-3.5 h-3.5 text-amber-400" />
              <span>Location</span>
            </span>
            <span className="text-[10px] font-mono text-slate-400">props.location</span>
          </button>

          {openSections.location && (
            <div className="bg-[#1e2228] divide-y divide-[#282c34]">
              {/* Location */}
              <div className="grid grid-cols-12 items-center">
                <label className="col-span-5 px-3 py-1.5 text-slate-300 border-r border-[#282c34] truncate">
                  Location
                </label>
                <div className="col-span-7 px-2 py-1">
                  <input
                    type="text"
                    value={location.Location}
                    placeholder={location.Location === '<Varies>' ? '<Varies>' : 'Enter location (e.g. Rumah)...'}
                    onFocus={(e) => {
                      if (e.target.value === '<Varies>') e.target.select();
                    }}
                    onChange={(e) =>
                      setLocation((prev) => ({ ...prev, Location: e.target.value }))
                    }
                    className={`w-full bg-[#15181c] px-2 py-1 rounded border border-slate-700/80 focus:outline-none focus:border-amber-400 ${
                      location.Location === '<Varies>' ? 'text-amber-400 font-mono italic text-[11px]' : 'text-slate-100'
                    }`}
                  />
                </div>
              </div>

              {/* Floor */}
              <div className="grid grid-cols-12 items-center">
                <label className="col-span-5 px-3 py-1.5 text-slate-300 border-r border-[#282c34] truncate">
                  Floor
                </label>
                <div className="col-span-7 px-2 py-1">
                  <input
                    type="text"
                    value={location.Floor}
                    placeholder={location.Floor === '<Varies>' ? '<Varies>' : 'e.g. 1st Floor'}
                    onFocus={(e) => {
                      if (e.target.value === '<Varies>') e.target.select();
                    }}
                    onChange={(e) =>
                      setLocation((prev) => ({ ...prev, Floor: e.target.value }))
                    }
                    className={`w-full bg-[#15181c] px-2 py-1 rounded border border-slate-700/80 focus:outline-none focus:border-amber-400 ${
                      location.Floor === '<Varies>' ? 'text-amber-400 font-mono italic text-[11px]' : 'text-slate-100'
                    }`}
                  />
                </div>
              </div>

              {/* Area (Room) */}
              <div className="grid grid-cols-12 items-center">
                <label className="col-span-5 px-3 py-1.5 text-slate-300 border-r border-[#282c34] truncate">
                  Area (Room)
                </label>
                <div className="col-span-7 px-2 py-1">
                  <input
                    type="text"
                    value={location.Area}
                    placeholder={location.Area === '<Varies>' ? '<Varies>' : 'e.g. Room'}
                    onFocus={(e) => {
                      if (e.target.value === '<Varies>') e.target.select();
                    }}
                    onChange={(e) =>
                      setLocation((prev) => ({ ...prev, Area: e.target.value }))
                    }
                    className={`w-full bg-[#15181c] px-2 py-1 rounded border border-slate-700/80 focus:outline-none focus:border-amber-400 ${
                      location.Area === '<Varies>' ? 'text-amber-400 font-mono italic text-[11px]' : 'text-slate-100'
                    }`}
                  />
                </div>
              </div>
            </div>
          )}
        </div>

        {/* ----------------------------------------------------------------
            GROUP 2: TYPES (Code, Category, Entity Type)
        ---------------------------------------------------------------- */}
        <div>
          <button
            onClick={() => toggleSection('types')}
            className="w-full bg-[#2f3540] hover:bg-[#363d4a] px-3 py-1.5 flex items-center justify-between text-left font-semibold text-slate-200 transition-colors"
          >
            <span className="flex items-center gap-1.5">
              {openSections.types ? (
                <ChevronDown className="w-3.5 h-3.5 text-amber-400" />
              ) : (
                <ChevronRight className="w-3.5 h-3.5 text-slate-400" />
              )}
              <Box className="w-3.5 h-3.5 text-sky-400" />
              <span>Types</span>
            </span>
            <span className="text-[10px] font-mono text-slate-400">props.types</span>
          </button>

          {openSections.types && (
            <div className="bg-[#1e2228] divide-y divide-[#282c34]">
              {/* Code */}
              <div className="grid grid-cols-12 items-center">
                <label className="col-span-5 px-3 py-1.5 text-slate-300 border-r border-[#282c34] truncate">
                  Code
                </label>
                <div className="col-span-7 px-2 py-1">
                  <input
                    type="text"
                    value={qtoCode}
                    placeholder={qtoCode === '<Varies>' ? '<Varies>' : 'Work item code (e.g. FloorFin/CT1)...'}
                    onFocus={(e) => {
                      if (e.target.value === '<Varies>') e.target.select();
                    }}
                    onChange={(e) => setQtoCode(e.target.value)}
                    className={`w-full bg-[#15181c] font-mono px-2 py-1 rounded border border-slate-700/80 focus:outline-none focus:border-amber-400 ${
                      qtoCode === '<Varies>' ? 'text-amber-400 italic text-[11px]' : 'text-amber-300'
                    }`}
                  />
                </div>
              </div>

              {/* Category */}
              <div className="grid grid-cols-12 items-center">
                <label className="col-span-5 px-3 py-1.5 text-slate-300 border-r border-[#282c34] truncate">
                  Category
                </label>
                <div className="col-span-7 px-2 py-1">
                  <select
                    value={qtoCategory}
                    onChange={(e) => handleCategorySelect(e.target.value)}
                    className={`w-full bg-[#15181c] px-2 py-1 rounded border border-slate-700/80 focus:outline-none focus:border-amber-400 font-medium ${
                      qtoCategory === '<Varies>' ? 'text-amber-400 italic' : 'text-slate-100'
                    }`}
                  >
                    {qtoCategory === '<Varies>' && (
                      <option value="<Varies>">&lt;Varies&gt; (Multiple Categories)</option>
                    )}
                    <option value="length">length</option>
                    <option value="area">area</option>
                    <option value="wall">wall</option>
                    <option value="count">count</option>
                    <option value="weight">weight</option>
                    <option value="beam">beam</option>
                    <option value="column">column</option>
                    <option value="slab">slab</option>
                  </select>
                </div>
              </div>

              {/* Entity Type selector (specifically needed when Category == weight) */}
              {qtoCategory === 'weight' && (
                <div className="grid grid-cols-12 items-center bg-pink-500/5">
                  <label className="col-span-5 px-3 py-1.5 text-pink-300 border-r border-[#282c34] truncate">
                    entity_type
                  </label>
                  <div className="col-span-7 px-2 py-1">
                    <select
                      value={weightEntityType}
                      onChange={(e) => setWeightEntityType(e.target.value)}
                      className="w-full bg-[#15181c] text-pink-200 px-2 py-1 rounded border border-pink-500/40 focus:outline-none"
                    >
                      <option value="line">line (length × xdata: wpl)</option>
                      <option value="surface">surface (area × xdata: wpa)</option>
                    </select>
                  </div>
                </div>
              )}

              {/* Formula Rule Preview */}
              <div className="grid grid-cols-12 items-center">
                <label className="col-span-5 px-3 py-1.5 text-slate-400 border-r border-[#282c34] truncate">
                  Rule
                </label>
                <div
                  className="col-span-7 px-2.5 py-1.5 text-[11px] font-mono text-slate-400 truncate"
                  title={activeCategoryConfig.formulaLabel}
                >
                  {activeCategoryConfig.formulaLabel}
                </div>
              </div>
            </div>
          )}
        </div>

        {/* ----------------------------------------------------------------
            GROUP 3: EXTENDED ENTITY DATA (XDATA PARAMETERS)
        ---------------------------------------------------------------- */}
        <div>
          <button
            onClick={() => toggleSection('xdata')}
            className="w-full bg-[#2f3540] hover:bg-[#363d4a] px-3 py-1.5 flex items-center justify-between text-left font-semibold text-slate-200 transition-colors"
          >
            <span className="flex items-center gap-1.5">
              {openSections.xdata ? (
                <ChevronDown className="w-3.5 h-3.5 text-amber-400" />
              ) : (
                <ChevronRight className="w-3.5 h-3.5 text-slate-400" />
              )}
              <Database className="w-3.5 h-3.5 text-emerald-400" />
              <span>XData Parameters (xdata)</span>
            </span>
            <span className="text-[10px] font-mono text-emerald-400">
              {activeXdataFields.length > 0
                ? activeXdataFields.map((f) => f.key).join(', ')
                : 'direct'}
            </span>
          </button>

          {openSections.xdata && (
            <div className="bg-[#1e2228] divide-y divide-[#282c34]">
              {activeXdataFields.length === 0 ? (
                <div className="px-3 py-2 text-slate-400 text-[11px]">
                  Kategori <strong className="text-slate-200">{qtoCategory}</strong> menghitung
                  langsung geometri objek CAD (<code className="text-sky-400">quantities</code>)
                  tanpa parameter pengali XData tambahan.
                </div>
              ) : (
                <>
                  {/* Evaluation Mode Toggle */}
                  <div className="grid grid-cols-12 items-center bg-[#181b20]">
                    <label className="col-span-5 px-3 py-1.5 text-slate-400 border-r border-[#282c34] text-[11px]">
                      XData Scope
                    </label>
                    <div className="col-span-7 px-2 py-1 flex gap-1">
                      <button
                        onClick={() => setXdataMode('uniform')}
                        className={`flex-1 py-0.5 rounded text-[11px] font-medium ${
                          xdataMode === 'uniform'
                            ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/40'
                            : 'bg-slate-800 text-slate-400'
                        }`}
                      >
                        Uniform
                      </button>
                      <button
                        onClick={() => setXdataMode('per_entity')}
                        className={`flex-1 py-0.5 rounded text-[11px] font-medium ${
                          xdataMode === 'per_entity'
                            ? 'bg-amber-500/20 text-amber-300 border border-amber-500/40'
                            : 'bg-slate-800 text-slate-400'
                        }`}
                      >
                        Per-Entity
                      </button>
                    </div>
                  </div>

                  {/* Dynamic XData Fields */}
                  {activeXdataFields.map((field) => (
                    <div key={field.key} className="grid grid-cols-12 items-center">
                      <label className="col-span-6 px-3 py-1.5 text-slate-300 border-r border-[#282c34] flex items-center justify-between gap-1">
                        <span className="truncate" title={field.label}>
                          {field.label}
                        </span>
                        <span className="text-[10px] font-mono text-emerald-400 bg-emerald-950/60 px-1 rounded shrink-0">
                          {field.key}
                        </span>
                      </label>
                      <div className="col-span-6 px-2 py-1 flex items-center gap-1.5">
                        <input
                          type="number"
                          step={field.step}
                          value={globalXdata[field.key]}
                          onChange={(e) => handleGlobalXdataChange(field.key, e.target.value)}
                          className="w-full bg-[#15181c] text-emerald-300 font-mono px-2 py-1 rounded border border-slate-700/80 focus:outline-none focus:border-emerald-400 text-right"
                        />
                        <span className="text-[11px] text-slate-400 w-10 shrink-0 font-mono">
                          {field.unit}
                        </span>
                      </div>
                    </div>
                  ))}
                </>
              )}
            </div>
          )}
        </div>

        {/* ----------------------------------------------------------------
            GROUP 4: QUANTITIES / FACTORS (Product of Values)
        ---------------------------------------------------------------- */}
        <div>
          <button
            onClick={() => toggleSection('quantities')}
            className="w-full bg-[#2f3540] hover:bg-[#363d4a] px-3 py-1.5 flex items-center justify-between text-left font-semibold text-slate-200 transition-colors"
          >
            <span className="flex items-center gap-1.5">
              {openSections.quantities ? (
                <ChevronDown className="w-3.5 h-3.5 text-amber-400" />
              ) : (
                <ChevronRight className="w-3.5 h-3.5 text-slate-400" />
              )}
              <Calculator className="w-3.5 h-3.5 text-purple-400" />
              <span>Quantities ([number])</span>
            </span>
            <span className="text-[10px] font-mono text-amber-300 truncate max-w-[190px]">
              [{qtyFactorsArray.join(', ')}] = {fmt(factorProduct, 2)}x
            </span>
          </button>

          {openSections.quantities && (
            <div className="bg-[#1e2228] p-2 space-y-2">
              {/* Base CAD Geometry summary header */}
              <div className="flex items-center justify-between text-[11px] bg-[#17191e] px-2.5 py-1.5 rounded border border-slate-800">
                <span className="text-slate-400">
                  Base Geometry:{' '}
                  <strong className="text-slate-100 font-mono">
                    {fmt(qtoResult.sumProductBase, 3)} {activeCategoryConfig.baseUnit}
                  </strong>
                  {selectedEntities.length > 0 && (
                    <span className="text-[10px] text-slate-500 ml-1.5">
                      ({selectedEntities.length} obj{selectedEntities.length > 1 ? 's' : ''})
                    </span>
                  )}
                </span>
                <button
                  type="button"
                  onClick={handleAddFactorRow}
                  className="text-amber-400 hover:text-amber-300 flex items-center gap-1 text-[11px] font-semibold bg-amber-400/10 hover:bg-amber-400/20 px-2 py-0.5 rounded transition-colors"
                  title="Add factor row (e.g. nr, margin, overhead)"
                >
                  <Plus className="w-3 h-3" />
                  <span>Add Row</span>
                </button>
              </div>

              {/* Factors List (nr, margin, overhead, etc.) */}
              <div className="divide-y divide-slate-800 border border-slate-800 rounded bg-[#15181c] overflow-hidden">
                {factors.map((f, idx) => (
                  <div key={f.id} className="px-2.5 py-1.5 flex items-center justify-between gap-2">
                    <div className="flex items-center gap-1.5 flex-1 min-w-0">
                      <span className="text-slate-500 text-[10px] font-mono w-3.5 text-center">
                        {idx + 1}.
                      </span>
                      <input
                        type="text"
                        value={f.name}
                        onChange={(e) => handleUpdateFactor(f.id, 'name', e.target.value)}
                        placeholder="factor name (e.g. nr, margin)"
                        className="flex-1 min-w-[70px] bg-[#1e2229] font-mono text-slate-200 text-xs px-2 py-1 rounded border border-slate-700 focus:outline-none focus:border-amber-400"
                      />
                    </div>
                    <div className="flex items-center gap-1.5 shrink-0">
                      <span className="text-slate-500 text-xs font-mono">×</span>
                      <input
                        type="number"
                        step="0.05"
                        value={f.value}
                        onChange={(e) => handleUpdateFactor(f.id, 'value', e.target.value)}
                        className="w-16 bg-[#1e2229] text-right font-mono text-amber-300 text-xs px-2 py-1 rounded border border-slate-700 focus:outline-none focus:border-amber-400"
                        title={`Factor value for ${f.name}`}
                      />
                      <button
                        type="button"
                        onClick={() => handleRemoveFactorRow(f.id)}
                        disabled={factors.length <= 1}
                        title={factors.length <= 1 ? 'Minimum 1 factor required' : `Delete ${f.name}`}
                        className={`p-1 rounded text-slate-500 hover:text-rose-400 hover:bg-rose-500/10 transition-colors ${
                          factors.length <= 1 ? 'opacity-30 cursor-not-allowed' : ''
                        }`}
                      >
                        <Trash2 className="w-3 h-3" />
                      </button>
                    </div>
                  </div>
                ))}

                {/* Factors product calculation summary */}
                <div className="bg-[#191c22] px-2.5 py-1.5 flex items-center justify-between text-[11px] text-slate-400 border-t border-slate-800">
                  <span className="truncate max-w-[210px] text-slate-400 text-[10px] font-mono">
                    Factor Product ({factors.map((f) => f.name || '?').join(' × ')}):
                  </span>
                  <span className="font-mono text-amber-300 font-bold text-xs">
                    {fmt(factorProduct, 3)}
                  </span>
                </div>
              </div>
            </div>
          )}
        </div>

        {/* ----------------------------------------------------------------
            GROUP 5: COMPUTED OUTPUT (default_value)
        ---------------------------------------------------------------- */}
        <div>
          <button
            onClick={() => toggleSection('output')}
            className="w-full bg-[#2f3540] hover:bg-[#363d4a] px-3 py-1.5 flex items-center justify-between text-left font-semibold text-slate-200 transition-colors"
          >
            <span className="flex items-center gap-1.5">
              {openSections.output ? (
                <ChevronDown className="w-3.5 h-3.5 text-amber-400" />
              ) : (
                <ChevronRight className="w-3.5 h-3.5 text-slate-400" />
              )}
              <CheckSquare className="w-3.5 h-3.5 text-amber-400" />
              <span>Computed Output (default_value)</span>
            </span>
            <span className="text-[10px] font-mono text-amber-400">Result</span>
          </button>

          {openSections.output && (
            <div className="bg-[#1e2228] divide-y divide-[#282c34]">
              {qtoResult.mode === 'dual' ? (
                <>
                  {/* Concrete Volume */}
                  <div className="grid grid-cols-12 items-center bg-amber-500/5">
                    <label className="col-span-6 px-3 py-2 text-amber-200 border-r border-[#282c34] font-medium">
                      concrete_volume
                    </label>
                    <div className="col-span-6 px-3 py-2 text-right font-mono font-bold text-amber-400 text-sm">
                      {fmt(qtoResult.concrete_volume, 3)} m³
                    </div>
                  </div>
                  {/* Steel Weight */}
                  <div className="grid grid-cols-12 items-center bg-sky-500/5">
                    <label className="col-span-6 px-3 py-2 text-sky-200 border-r border-[#282c34] font-medium">
                      steel_weight
                    </label>
                    <div className="col-span-6 px-3 py-2 text-right font-mono font-bold text-sky-400 text-sm">
                      {fmt(qtoResult.steel_weight, 2)} kg
                    </div>
                  </div>
                </>
              ) : (
                <div className="grid grid-cols-12 items-center bg-emerald-500/5">
                  <label className="col-span-6 px-3 py-2 text-emerald-200 border-r border-[#282c34] font-medium truncate">
                    {qtoResult.primaryLabel}
                  </label>
                  <div className="col-span-6 px-3 py-2 text-right font-mono font-bold text-emerald-400 text-sm">
                    {fmt(qtoResult.primaryValue, qtoCategory === 'count' ? 0 : 3)}{' '}
                    {qtoResult.primaryUnit}
                  </div>
                </div>
              )}
            </div>
          )}
        </div>
      </div>

      {/* ====================================================================
          4. COLLAPSIBLE BOQ SCHEDULE DRAWER
      ==================================================================== */}
      {showScheduleDrawer && (
        <div className="bg-[#1b1e24] border-t border-slate-700 p-2.5 max-h-48 overflow-y-auto shrink-0">
          <div className="flex items-center justify-between mb-1.5">
            <span className="text-[11px] font-semibold text-amber-400 uppercase tracking-wider">
              BOQ Schedule ({savedSchedule.length})
            </span>
            <div className="flex items-center gap-2">
              {savedSchedule.length > 0 && (
                <button
                  onClick={() => setSavedSchedule([])}
                  className="text-[10px] text-rose-400 hover:text-rose-300 flex items-center gap-0.5"
                >
                  <Trash2 className="w-3 h-3" />
                  <span>Clear</span>
                </button>
              )}
              <button
                onClick={() => setShowScheduleDrawer(false)}
                className="text-slate-400 hover:text-white text-xs"
              >
                <X className="w-3.5 h-3.5" />
              </button>
            </div>
          </div>
          {savedSchedule.length === 0 ? (
            <p className="text-[11px] text-slate-400">
              Belum ada item QTO yang direkam. Klik tombol &ldquo;Save Schedule&rdquo; di bawah.
            </p>
          ) : (
            <table className="w-full text-left border-collapse text-[11px]">
              <thead>
                <tr className="border-b border-slate-700 text-slate-400">
                  <th className="py-1 px-1.5">Location</th>
                  <th className="py-1 px-1.5">Code</th>
                  <th className="py-1 px-1.5">Cat</th>
                  <th className="py-1 px-1.5 text-right">Result</th>
                </tr>
              </thead>
              <tbody>
                {savedSchedule.map((row) => (
                  <tr key={row.id} className="border-b border-slate-800/80">
                    <td className="py-1 px-1.5 text-slate-300 truncate max-w-[110px]">
                      {row.floor} • {row.area || row.room}
                    </td>
                    <td className="py-1 px-1.5 font-mono text-amber-300">{row.code}</td>
                    <td className="py-1 px-1.5 uppercase text-[10px]">{row.category}</td>
                    <td className="py-1 px-1.5 text-right font-mono text-emerald-400 font-semibold">
                      {row.result}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          )}
        </div>
      )}

      {/* ====================================================================
          5. BOTTOM SUMMARY & CAD XDATA ACTION FOOTER
      ==================================================================== */}
      <footer className="p-2.5 bg-[#1c1f24] border-t border-[#111317] flex items-center justify-between gap-2 shrink-0">
        <label className="flex items-center gap-1.5 text-[11px] text-slate-300 cursor-pointer">
          <input
            type="checkbox"
            checked={autoWriteXdata}
            onChange={(e) => setAutoWriteXdata(e.target.checked)}
            className="rounded border-slate-600 bg-slate-800 text-amber-500 focus:ring-0"
          />
          <span>Auto-Write XData</span>
        </label>

        <div className="flex items-center gap-1.5">
          <button
            onClick={handleApplyXdataToCad}
            title="Write Location, Code, Category & XData to selected AutoCAD / GstarCAD objects (Shortcut: F5)"
            className="px-2.5 py-1.5 rounded bg-sky-600 hover:bg-sky-500 text-white font-semibold text-xs flex items-center gap-1 shadow transition-colors"
          >
            <UploadCloud className="w-3.5 h-3.5" />
            <span>Apply XData</span>
            <kbd className="ml-0.5 px-1 py-0.5 text-[9px] leading-none bg-sky-800/80 text-sky-200 rounded border border-sky-400/40 font-mono font-semibold">F5</kbd>
          </button>

          <button
            onClick={handleRecordSchedule}
            className="px-2.5 py-1.5 rounded bg-amber-500 hover:bg-amber-400 text-slate-950 font-semibold text-xs flex items-center gap-1 shadow transition-colors"
          >
            <Check className="w-3.5 h-3.5" />
            <span>Save Schedule</span>
          </button>
        </div>
      </footer>

      {/* ====================================================================
          6. LIVE JSON SCHEMA MODAL
      ==================================================================== */}
      {showJsonModal && (
        <div className="fixed inset-0 z-50 bg-black/75 backdrop-blur-sm flex items-center justify-center p-3">
          <div className="bg-[#1e2229] border border-slate-700 rounded-lg max-w-lg w-full overflow-hidden shadow-2xl flex flex-col max-h-[85vh]">
            <div className="px-3.5 py-2.5 bg-[#282c34] border-b border-slate-700 flex items-center justify-between">
              <div className="flex items-center gap-2">
                <Code2 className="w-4 h-4 text-amber-400" />
                <span className="text-xs font-bold uppercase tracking-wider text-slate-100">
                  QTO Property Box — Live JSON State
                </span>
              </div>
              <div className="flex items-center gap-2">
                <button
                  onClick={handleCopyJson}
                  className="px-2 py-0.5 rounded bg-slate-700 hover:bg-slate-600 text-[11px] text-slate-200 flex items-center gap-1"
                >
                  <Copy className="w-3 h-3" />
                  <span>{copiedJson ? 'Copied!' : 'Copy'}</span>
                </button>
                <button
                  onClick={() => setShowJsonModal(false)}
                  className="text-slate-400 hover:text-white"
                >
                  <X className="w-4 h-4" />
                </button>
              </div>
            </div>
            <div className="p-3.5 overflow-y-auto bg-[#14161a] flex-1">
              <pre className="text-[11px] font-mono text-emerald-300 leading-relaxed whitespace-pre-wrap">
                {JSON.stringify(jsonOutput, null, 2)}
              </pre>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
