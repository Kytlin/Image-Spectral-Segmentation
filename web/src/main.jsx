import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import App from './App.jsx';
import './App.css';

// Entry point: mount the <App> component into <div id="root"> from index.html.
// StrictMode runs extra checks in development (for example, it runs effects twice to
// surface missing cleanups). It does nothing in the production build.
createRoot(document.getElementById('root')).render(
  <StrictMode>
    <App />
  </StrictMode>,
);
