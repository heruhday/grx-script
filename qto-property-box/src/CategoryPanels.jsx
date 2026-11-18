import React, { useState } from 'react';
import { ChevronRight, Plus, Ruler, SquareDashed, Hash, BrickWall, Weight, Box } from 'lucide-react';

const categories = [
  { name: 'Length', icon: Ruler, types: ['Length'] },
  { name: 'Area', icon: SquareDashed, types: ['Area'] },
  { name: 'No', icon: Hash, types: ['Number'] },
  { name: 'Wall', icon: BrickWall, types: ['Wall', 'Wall Finishes', 'Concrete Wall', 'Steel Wall'] },
  { name: 'Weight', icon: Weight, types: [] },
  { name: 'Volume', icon: Box, types: ['Beam', 'Column', 'Slab'] },
];

export default function CategoryPanels({ children, onAdd }) {
  const [expanded, setExpanded] = useState({ Area: true });
  const [selected, setSelected] = useState(null);
  const [visible, setVisible] = useState(categories.map(({ name }) => name));
  const articles = React.Children.toArray(children);
  return <>
    <div className="category-filters" role="group" aria-label="QTO categories">
      <button type="button" title="All categories" aria-pressed={visible.length === categories.length}
        onClick={() => setVisible(visible.length === categories.length ? [] : categories.map(({ name }) => name))}>All</button>
      {categories.map(({ name, icon: Icon }) => <button key={name} type="button" title={name}
        aria-label={name} aria-pressed={visible.includes(name)} onClick={() => setVisible((prev) => prev.includes(name) ? prev.filter((entry) => entry !== name) : [...prev, name])}><Icon size={15} /></button>)}
    </div>
    <div className="category-panels">
      {categories.filter(({ name }) => visible.includes(name)).map(({ name, types, icon: Icon }) => {
        const entries = articles.filter((article) => types.includes(article.props['data-type']));
        const active = entries.find((article) => article.props['data-item-id'] === selected) || entries[0];
        return <section className="category-panel" key={name}>
          <div className="category-panel-heading">
            <button type="button" className="category-panel-toggle" aria-expanded={!!expanded[name]} onClick={() => setExpanded((prev) => ({ ...prev, [name]: !prev[name] }))}>
              <ChevronRight size={14} className={expanded[name] ? 'expanded' : ''} /><Icon size={15} /><strong>{name}</strong><span className="category-count">{entries.length} {entries.length === 1 ? 'item' : 'items'}</span>
            </button>
            <button type="button" className="category-add" title={`Add item to ${name}`} aria-label={`Add item to ${name}`} disabled={!types.length} onClick={() => { onAdd(types[0]); setExpanded((prev) => ({ ...prev, [name]: true })); }}><Plus size={15} /></button>
          </div>
          {!!entries.length && <div className={`category-item-list ${expanded[name] ? 'expanded' : ''}`}>
            {entries.map((article) => <button type="button" key={article.key}
              className={`category-item-summary ${expanded[name] && article === active ? 'selected' : ''}`}
              onClick={() => { setSelected(article.props['data-item-id']); setExpanded((prev) => ({ ...prev, [name]: true })); }}>
              <span className="category-item-description">{article.props['data-description'] || article.props['data-code'] || 'Untitled item'}</span>
              <span className="category-item-numbers"><span>{article.props['data-code']}</span><span>{article.props['data-quantity']}</span></span>
            </button>)}
          </div>}
          {expanded[name] && !entries.length && <p className="category-empty">No items in {name}. Tap + to add one.</p>}
          {/* Keep editor instances mounted so filtering never discards an in-progress edit. */}
          {entries.map((article) => <div key={article.key} hidden={!expanded[name] || article !== active} className="category-item-editor">{article}</div>)}
        </section>;
      })}
    </div>
  </>;
}
