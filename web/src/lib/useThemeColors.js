import { useEffect, useState } from 'react';

// Chart libraries draw SVG with colour *attributes* (stroke="#2a78d6"), which can't use CSS
// variables like var(--series-1). This hook reads the current values of the CSS variables
// defined in App.css, and reads them again when the OS switches between light and dark mode.
//
// A "hook" is a function whose name starts with `use` and which calls React's own hooks
// (useState, useEffect, ...). Components call it to share this logic.
const TOKENS = ['--series-1', '--grid', '--axis', '--text-secondary', '--surface-1'];

function readTokens() {
  const styles = getComputedStyle(document.documentElement);
  return Object.fromEntries(TOKENS.map((name) => [name, styles.getPropertyValue(name).trim()]));
}

export function useThemeColors() {
  const [colours, setColours] = useState(readTokens);

  useEffect(() => {
    const query = window.matchMedia('(prefers-color-scheme: dark)');
    const update = () => setColours(readTokens());
    query.addEventListener('change', update);
    // The function returned from useEffect is the "cleanup": React calls it when the
    // component unmounts, so the listener doesn't outlive the component.
    return () => query.removeEventListener('change', update);
  }, []);

  return colours;
}
