import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { formatUsInput, formatUsQuantity, parseUsNumber } from '../qto-property-box/src/numberFormat.js';

assert.equal(formatUsQuantity(1234.5), '1,234.500');
assert.equal(formatUsQuantity(-9876543.125), '-9,876,543.125');
assert.equal(formatUsQuantity(0), '0.000');
assert.equal(formatUsQuantity(NaN), 'Error');
assert.equal(formatUsInput(1234.56), '1,234.56');
assert.equal(formatUsInput(0.00125), '0.00125');
assert.equal(parseUsNumber('1,234.56'), 1234.56);
assert.equal(parseUsNumber('-1,234,567.89'), -1234567.89);
assert.equal(parseUsNumber('1234.56'), 1234.56);
assert.equal(parseUsNumber('.25'), 0.25);
assert.equal(parseUsNumber('1.'), 1);
assert.equal(parseUsNumber('1.25e3'), 1250);
for (const text of ['', '-', '.', '1,5', '1.234,56', '1,23,456', '1,234.5abc', 'Infinity']) {
  assert.ok(Number.isNaN(parseUsNumber(text)), text);
}
// Start Node with a non-US default locale; explicit US output must stay identical.
const url = new URL('../qto-property-box/src/numberFormat.js', import.meta.url).href;
const result = JSON.parse(execFileSync(process.execPath, ['--input-type=module', '-e', `
  import { formatUsQuantity, formatUsInput } from ${JSON.stringify(url)};
  console.log(JSON.stringify({ qty: formatUsQuantity(1234.56), input: formatUsInput(1234.56) }));
`], { env: { ...process.env, LANG: 'de_DE.UTF-8', LC_ALL: 'de_DE.UTF-8' }, encoding: 'utf8' }));
assert.deepEqual(result, { qty: '1,234.560', input: '1,234.56' });
console.log('US quantity, input parsing, precision, and non-US locale checks passed');
