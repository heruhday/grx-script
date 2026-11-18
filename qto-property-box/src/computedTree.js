const collator = new Intl.Collator('en-US', { numeric: true, sensitivity: 'base' });

export function buildComputedTree(records, levels, descriptions = {}) {
  const description = (column, value) => descriptions[
    ['Building', 'Floor', 'Area'].includes(column) ? `${column.toLowerCase()}:${value}` : value
  ]?.short || '';

  function leaves(rows, path) {
    const groups = new Map();
    for (const row of rows) {
      const key = JSON.stringify([row.Type, row.Code, row.unit]);
      if (!groups.has(key)) groups.set(key, { id: `${path}/${key}`, column: 'Code', value: row.Code,
        description: description('Code', row.Code), unit: row.unit, qty: 0, leaf: true });
      groups.get(key).qty += row.qty;
    }
    return [...groups.values()];
  }

  function group(rows, depth, path) {
    if (depth >= levels.length) return leaves(rows, path);
    const level = levels[depth];
    const groups = new Map();
    for (const row of rows) {
      const value = String(row[level.column] ?? '');
      if (!groups.has(value)) groups.set(value, []);
      groups.get(value).push(row);
    }
    const descending = ['Z to A', 'Largest to Smallest'].includes(level.order);
    return [...groups.entries()].sort(([a], [b]) => {
      const labelA = level.sortOn === 'Short Description' ? description(level.column, a) || a : a;
      const labelB = level.sortOn === 'Short Description' ? description(level.column, b) || b : b;
      const compared = collator.compare(labelA, labelB) || collator.compare(a, b);
      return descending ? -compared : compared;
    }).map(([value, members]) => {
      const id = `${path}/${JSON.stringify([level.column, value])}`;
      const children = group(members, depth + 1, id);
      // A final Code level can show its quantity directly without repeating the code.
      if (depth === levels.length - 1 && level.column === 'Code' && children.length === 1) {
        return { ...children[0], id };
      }
      return { id, column: level.column, value, description: description(level.column, value),
        entityCount: new Set(members.map((row) => row.entityId)).size, children };
    });
  }
  return group(records, 0, 'computed');
}
