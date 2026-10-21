import Card from './Card.jsx';

export const MIN_K = 2;
export const MAX_K = 8;

// One labelled slider with its current value and range ends.
function SliderField({ id, label, value, min, max, step, digits, onChange, children }) {
  return (
    <div className="field">
      <div className="field-row">
        <label htmlFor={id}>{label}</label>
        <span className="value-chip">{value.toFixed(digits)}</span>
      </div>
      <input
        id={id}
        type="range"
        min={min}
        max={max}
        step={step}
        value={value}
        onChange={(event) => onChange(Number(event.target.value))}
      />
      <div className="field-row hint">
        <span>{min.toFixed(digits)}</span>
        <span>{max.toFixed(digits)}</span>
      </div>
      {children}
    </div>
  );
}

// Upload button, k slider, display mode, and the Run button. This component holds no state
// of its own: the values come in as props from App, and changes go back out through the
// on... callbacks. React calls this pattern "lifting state up".
export default function ControlsCard({
  k,
  onKChange,
  blurSigma,
  onBlurSigmaChange,
  graphSettingsPending,
  displayMode,
  onDisplayModeChange,
  onFileSelected,
  onRun,
  canRun,
  busy,
  hasLiveRun,
}) {
  return (
    <Card title="Segmentation Controls" icon="⚙">
      {/* A file <input> is hard to style, so it's hidden inside a <label> that looks like a
          button. Clicking the label opens the file picker. */}
      <label className="button primary">
        <input
          type="file"
          accept="image/jpeg,image/png,image/webp"
          hidden
          onChange={(event) => {
            const file = event.target.files?.[0];
            if (file) onFileSelected(file);
            event.target.value = ''; // so choosing the same file again still fires onChange
          }}
        />
        Upload Image
      </label>
      <p className="hint">JPG, PNG or WebP. Resized in your browser to at most 160 px.</p>

      <SliderField id="k-slider" label="Number of segments (k)" value={k} min={MIN_K} max={MAX_K}
        step={1} digits={0} onChange={onKChange}>
        <p className="hint">
          {hasLiveRun ? 'Applies immediately: only the clustering is redone, so it is fast.' : 'Pick k, then run.'}
        </p>
      </SliderField>

      <SliderField id="sigma-slider" label="Gaussian sigma (smoothing)" value={blurSigma} min={0}
        max={3} step={0.1} digits={1} onChange={onBlurSigmaChange} />
      <p className={`notice ${graphSettingsPending ? 'pending' : ''}`}>
        ⚠ Sigma changes the graph itself, so the graph and all eigenvectors are recomputed (a
        few seconds). It takes effect when you press Run Segmentation.
      </p>

      <div className="field">
        <span className="field-label">Segment colours</span>
        <div className="segmented-control" role="radiogroup" aria-label="Segment colours">
          {[
            ['palette', 'Palette'],
            ['mean', 'Mean colour'],
          ].map(([value, label]) => (
            <button
              key={value}
              type="button"
              role="radio"
              aria-checked={displayMode === value}
              className={displayMode === value ? 'active' : ''}
              onClick={() => onDisplayModeChange(value)}
            >
              {label}
            </button>
          ))}
        </div>
      </div>

      <button type="button" className="button primary" disabled={!canRun || busy} onClick={onRun}>
        {busy ? 'Working…' : '▶ Run Segmentation'}
      </button>
    </Card>
  );
}
