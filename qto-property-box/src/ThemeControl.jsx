import React, { useEffect, useState } from 'react';
import { Moon, Sun } from 'lucide-react';

function savedTheme() {
  try { return localStorage.getItem('qto-theme') === 'light' ? 'light' : 'dark'; }
  catch { return 'dark'; }
}

export default function ThemeControl() {
  const [theme, setTheme] = useState(savedTheme);

  useEffect(() => {
    document.documentElement.dataset.qtoTheme = theme;
    try { localStorage.setItem('qto-theme', theme); } catch { /* Theme still works without storage. */ }
  }, [theme]);

  useEffect(() => {
    const sync = (event) => {
      if (event.key === 'qto-theme') setTheme(event.newValue === 'light' ? 'light' : 'dark');
    };
    window.addEventListener('storage', sync);
    return () => window.removeEventListener('storage', sync);
  }, []);

  return <div className="theme-control" role="group" aria-label="Appearance">
    {[['light', Sun], ['dark', Moon]].map(([value, Icon]) => (
      <button key={value} type="button" aria-label={`${value === 'light' ? 'Light' : 'Dark'} theme`}
        title={`${value === 'light' ? 'Light' : 'Dark'} theme`} aria-pressed={theme === value}
        onClick={() => setTheme(value)}><Icon aria-hidden="true" size={15} /></button>
    ))}
  </div>;
}
