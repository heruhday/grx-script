let selected = entity.select('\nSelect entities: ', {
  out: e => e.id,
  set: e => ({ data: { code: 'CT1' }, clr: 8 }),
})
entity.set(
  { data: { code: 'CT1' }, clr: 6 },
  selected
)

entity.set({ visible: false }, selected)

function sset() {
  const name = console.read('\ncode: ');
  const selected = entity.select('\nSelect entities: ', {
    out: e => e.id,
    set: e => ({ data: { code: name }, clr: 8 }),
  })
}

function beam() {
  const start = entity.pick('\nstart: ', {
    out: e => ({ id: e.id }),
    set: e => ({ clr: 6 }),
  });
  const end = entity.pick('\nend: ', {
    out: e => ({ id: e.id }),
    set: e => ({ clr: 2 }),
  });
  const beam = entity.pick('\nbeam: ', {
    out: e => ({ id: e.id }),
    set: e => ({ data: { start: start.id, end: end.id }, clr: 8 }),
  });
  return ({ beam: beam.id, start: start.id, end: end.id });

}

function eshow() {
  const beam = entity.pick('\nbeam: ', {
    out: e => e,
  });
  const start = entity.get(beam.data.start);
  const end = entity.get(beam.data.end);
  return [start, end];
  // return ({ beam: beam.id, start: start.txt, end: end.txt, length: round(beam.l / 1000, 2) });
}