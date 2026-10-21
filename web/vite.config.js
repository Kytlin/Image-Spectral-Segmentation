import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

// `npm run dev` serves the app on http://localhost:5173 with hot reload. Requests to /api are
// forwarded to the C++ server on port 8080, so the browser sees a single origin.
export default defineConfig({
  plugins: [react()],
  server: {
    proxy: {
      '/api': 'http://localhost:8080',
    },
  },
});
