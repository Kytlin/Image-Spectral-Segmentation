import Card from './Card.jsx';
import { formatBytes, formatClock, formatMs, formatTimeAgo, formatTimedIterations } from '../lib/format.js';

// Downloads the run's experiment log as a CSV file. The browser builds the file in memory
// (a Blob), and clicking a temporary <a download> link saves it.
function saveLogAsCsv(run) {
  const quote = (text) => `"${String(text).replaceAll('"', '""')}"`;
  const header = [
    'run_id', 'filename', 'width', 'height', 'gaussian_sigma', 'time', 'k', 'runtime_ms',
    'eigensolver_ms', 'eigensolver_iterations', 'kmeans_ms', 'kmeans_iterations', 'memory_bytes',
    'eigenvalues',
  ];
  const rows = run.experiments.map((experiment) => [
    run.id, quote(run.filename), run.width, run.height, run.blur_sigma,
    new Date(experiment.created_at_ms).toISOString(), experiment.k, experiment.runtime_ms.toFixed(2),
    experiment.eigen_ms.toFixed(2), experiment.eigensolver_iterations, experiment.kmeans_ms.toFixed(2),
    experiment.kmeans_iterations, experiment.memory_bytes, quote(experiment.eigenvalues.join(' ')),
  ]);
  const csv = [header, ...rows].map((row) => row.join(',')).join('\n');

  const url = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }));
  const link = document.createElement('a');
  link.href = url;
  link.download = `run-${run.id}-log.csv`;
  link.click();
  URL.revokeObjectURL(url);
}

// Previous runs from GET /api/runs. Clicking one loads its experiment log.
export function RecentJobs({ runs, selectedRunId, onSelect, onRefresh }) {
  return (
    <Card
      title="Recent Jobs"
      icon="🕘"
      aside={
        <button type="button" className="link-button" onClick={onRefresh}>
          Refresh
        </button>
      }
    >
      {runs.length === 0 ? (
        <p className="placeholder">No runs yet.</p>
      ) : (
        <ul className="job-list">
          {/* A list in React needs a `key` per item, so React can tell items apart when
              the list changes. The database id is a natural key. */}
          {runs.map((run) => (
            <li key={run.id}>
              <button
                type="button"
                className={`job ${run.id === selectedRunId ? 'selected' : ''}`}
                onClick={() => onSelect(run.id)}
              >
                {run.thumbnail ? (
                  <img src={run.thumbnail} alt="" className="job-thumb" />
                ) : (
                  <span className="job-thumb" />
                )}
                <span className="job-text">
                  <span className="job-name">{run.filename}</span>
                  <span className="job-meta">
                    #{run.id} · k={run.last_k ?? '–'} · {formatMs(run.total_ms)}
                  </span>
                </span>
                <span className="job-time">{formatTimeAgo(run.created_at_ms)}</span>
              </button>
            </li>
          ))}
        </ul>
      )}
    </Card>
  );
}

// Every segmentation request logged for one run (GET /api/runs/:id).
export function ExperimentLog({ run, isCurrentRun, onDelete, onClear }) {
  if (!run) {
    return (
      <Card title="Experiment Log" icon="☰" className="wide">
        <p className="placeholder">Run a segmentation or pick a job from Recent Jobs.</p>
      </Card>
    );
  }
  return (
    <Card
      title={`Experiment Log — #${run.id} ${run.filename}`}
      icon="☰"
      className="wide"
      aside={
        <div className="button-row">
          <button type="button" className="button secondary" disabled={run.experiments.length === 0}
            onClick={onClear}>
            Clear log
          </button>
          <button type="button" className="button secondary" disabled={run.experiments.length === 0}
            onClick={() => saveLogAsCsv(run)}>
            Download CSV
          </button>
        </div>
      }
    >
      {!isCurrentRun && (
        <p className="hint">
          This is a past run. Its image isn't loaded in this tab, so upload it again to view the segmentation.
        </p>
      )}
      {run.experiments.length === 0 && (
        <p className="placeholder">Nothing saved for this run yet. Press Save to log to keep a result.</p>
      )}
      <div className="table-scroll">
        <table>
          <thead>
            <tr>
              <th>Time</th>
              <th>k</th>
              <th>Runtime</th>
              <th>Eigensolver</th>
              <th>k-means</th>
              <th>Memory</th>
              <th aria-label="Delete" />
            </tr>
          </thead>
          <tbody>
            {run.experiments.map((experiment) => (
              <tr key={experiment.id}>
                <td>{formatClock(experiment.created_at_ms)}</td>
                <td>{experiment.k}</td>
                <td>{formatMs(experiment.runtime_ms)}</td>
                <td>{formatTimedIterations(experiment.eigen_ms, experiment.eigensolver_iterations)}</td>
                <td>{formatTimedIterations(experiment.kmeans_ms, experiment.kmeans_iterations)}</td>
                <td>{formatBytes(experiment.memory_bytes)}</td>
                <td>
                  <button type="button" className="delete-button" aria-label="Delete this entry"
                    title="Delete this entry" onClick={() => onDelete(experiment.id)}>
                    ×
                  </button>
                </td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </Card>
  );
}
