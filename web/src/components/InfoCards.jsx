import Card from './Card.jsx';
import { formatBytes, formatMs, formatTimedIterations } from '../lib/format.js';

function Row({ label, children }) {
  return (
    <div className="info-row">
      <span>{label}</span>
      <span className="info-value">{children}</span>
    </div>
  );
}

// Facts about the current run: what was uploaded and what graph the server built.
export function JobInfoCard({ image, run }) {
  return (
    <Card title="Job Information" icon="ⓘ">
      {!run ? (
        <p className="placeholder">No run yet.</p>
      ) : (
        <>
          <Row label="Run">#{run.id}</Row>
          <Row label="Original size">{image ? `${image.bitmap.width} × ${image.bitmap.height}` : '–'}</Row>
          <Row label="Processed size">{run.width} × {run.height}</Row>
          <Row label="Graph nodes">{run.num_nodes.toLocaleString()}</Row>
          <Row label="Graph edges (nnz W)">{run.num_edges.toLocaleString()}</Row>
          <Row label="Gaussian sigma">{run.blur_sigma.toFixed(1)}</Row>
          <Row label="Graph build">{formatMs(run.graph_ms)}</Row>
        </>
      )}
    </Card>
  );
}

function Metric({ label, value, detail }) {
  return (
    <div className="metric">
      <span className="metric-label">{label}</span>
      <span className="metric-value">{value}</span>
      {detail && <span className="metric-detail">{detail}</span>}
    </div>
  );
}

// Numbers for the latest segmentation: the same values a row of the experiment log gets when
// the user presses Save to log. saveState is 'idle', 'saving' or 'saved'.
export function MetricsCard({ segmentation, onSave, saveState }) {
  if (!segmentation) {
    return (
      <Card title="Results & Metrics" icon="▤">
        <p className="placeholder">Metrics for the latest request appear here.</p>
      </Card>
    );
  }
  return (
    <Card title="Results & Metrics" icon="▤">
      <div className="metric-grid">
        <Metric label="Segments" value={segmentation.k} />
        <Metric label="Runtime" value={formatMs(segmentation.runtime_ms)} />
        <Metric label="Memory usage" value={formatBytes(segmentation.memory_bytes)} />
      </div>
      <div className="banner good">
        <span className="banner-icon" aria-hidden="true">✓</span>
        <div>
          <strong>Segmentation completed successfully</strong>
          <div>
            Eigensolver: {formatTimedIterations(segmentation.eigen_ms, segmentation.eigensolver_iterations)}
            <br />
            k-means: {formatTimedIterations(segmentation.kmeans_ms, segmentation.kmeans_iterations)}
          </div>
        </div>
      </div>
      <button
        type="button"
        className="button primary"
        disabled={saveState !== 'idle'}
        onClick={onSave}
      >
        {saveState === 'saved' ? '✓ Saved to log' : saveState === 'saving' ? 'Saving…' : 'Save to log'}
      </button>
    </Card>
  );
}
