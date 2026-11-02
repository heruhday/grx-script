// test/demo.js
console.log('\n========================================');
console.log('Hello from AutoCAD JavaScript via AutoLISP!');
console.log('Current Document Database: Active');
console.log('Date & Time: ' + new Date().toLocaleString());
console.log('========================================\n');

// Pick or select an entity to demonstrate live interaction
const ent = entity.pick('\nSelect any entity to test JS from LISP: ');
if (ent) {
  console.log('Picked Entity:');
  console.log('  Handle :', ent.hand);
  console.log('  Class  :', ent.ty);
  console.log('  Layer  :', ent.lyr);
  if (ent.l !== undefined) {
    console.log('  Length :', round(ent.l, 2), 'mm');
  }
  
  // Highlight entity color to yellow (2)
  entity.set({ clr: 2 }, ent.id);
  console.log('Entity color changed to Yellow (ACI 2)!');
} else {
  console.log('No entity picked.');
}

'Execution Completed';
