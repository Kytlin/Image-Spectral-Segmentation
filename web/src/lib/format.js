export function formatMs(ms) {
  if (ms == null) return '–';
  if (ms >= 1000) return `${(ms / 1000).toFixed(2)} s`;
  return ms >= 100 ? `${Math.round(ms)} ms` : `${ms.toFixed(2)} ms`;
}

// "3.21 s (65 iter)"
export function formatTimedIterations(ms, iterations) {
  return `${formatMs(ms)} (${iterations} iter)`;
}

export function formatBytes(bytes) {
  if (bytes == null) return '–';
  return bytes >= 1024 * 1024 ? `${(bytes / (1024 * 1024)).toFixed(1)} MB` : `${Math.round(bytes / 1024)} KB`;
}

export function formatScientific(value, digits = 1) {
  if (value == null) return '–';
  return value.toExponential(digits);
}

// "just now", "5 min ago", "3 h ago", or a date for anything older than a day.
export function formatTimeAgo(epochMs) {
  const seconds = (Date.now() - epochMs) / 1000;
  if (seconds < 60) return 'just now';
  if (seconds < 3600) return `${Math.floor(seconds / 60)} min ago`;
  if (seconds < 86400) return `${Math.floor(seconds / 3600)} h ago`;
  return new Date(epochMs).toLocaleDateString();
}

export function formatClock(epochMs) {
  return new Date(epochMs).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' });
}

// Powers of ten covering [min, max], for clean ticks on a log axis.
export function powerOfTenTicks(min, max) {
  const ticks = [];
  for (let exponent = Math.floor(Math.log10(min)); exponent <= Math.ceil(Math.log10(max)); exponent++) {
    ticks.push(10 ** exponent);
  }
  return ticks;
}
