// test/test_editor_inputs.js
console.log('\n--- Testing Editor Input Methods ---');

// 1. Test editor.getString
const str = editor.getString('\nEnter custom text: ', {
  allowSpaces: true,
  default: 'DefaultLayer'
});
console.log('getString result:', str);

// 2. Test editor.getNumber
const num = editor.getNumber('\nEnter width (mm): ', {
  default: 250.0,
  integer: false
});
console.log('getNumber result:', num);

// 3. Test editor.getPoint
const pt = editor.getPoint('\nPick start point: ');
if (pt) {
  console.log(`getPoint result: [${pt[0]}, ${pt[1]}, ${pt[2]}] (x: ${pt.x}, y: ${pt.y}, z: ${pt.z})`);

  // Pick second point with basePoint rubber-band
  const pt2 = editor.getPoint('\nPick end point: ', pt);
  if (pt2) {
    const dx = pt2.x - pt.x;
    const dy = pt2.y - pt.y;
    const dist = Math.sqrt(dx * dx + dy * dy);
    console.log('Distance between points:', round(dist, 2));
  }
}

// 4. Test editor.getValue
const val = editor.getValue('\nEnter JS value (object, array, number, string): ', {
  default: { status: 'OK', code: 200 }
});
console.log('getValue result:', console.inspect(val));
console.log('Type of getValue result:', typeof val);

console.log('\n--- Editor Input Test Complete ---');
