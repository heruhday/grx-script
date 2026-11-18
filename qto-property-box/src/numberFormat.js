const quantityFormatter = new Intl.NumberFormat('en-US', {
  minimumFractionDigits: 3,
  maximumFractionDigits: 3,
  useGrouping: true
});

const inputFormatter = new Intl.NumberFormat('en-US', {
  maximumFractionDigits: 20,
  useGrouping: true
});

export function formatUsQuantity(value) {
  return Number.isFinite(value) ? quantityFormatter.format(value) : 'Error';
}

export function formatUsInput(value) {
  return Number.isFinite(value) ? inputFormatter.format(value) : '';
}

export function parseUsNumber(text) {
  const value = String(text).trim();
  // Commas are grouping separators, never decimal separators. Validate grouping
  // before removing them so inputs such as "1,5" cannot silently become 15.
  if (!/^[+-]?(?:(?:\d+|\d{1,3}(?:,\d{3})+)(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?$/.test(value)) return NaN;
  const number = Number(value.replace(/,/g, ''));
  return Number.isFinite(number) ? number : NaN;
}
