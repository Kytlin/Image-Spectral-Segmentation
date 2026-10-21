import { useCallback, useEffect, useMemo, useRef, useState } from 'react';
import ControlsCard from './components/ControlsCard.jsx';
import { OriginalImage, SegmentedImage } from './components/ImagePanels.jsx';
import { ConvergenceChart, EigenvalueChart } from './components/Charts.jsx';
import { JobInfoCard, MetricsCard } from './components/InfoCards.jsx';
import { ExperimentLog, RecentJobs } from './components/HistoryCards.jsx';
import {
  clearLog,
  createRun,
  deleteExperiment,
  getRun,
  listRuns,
  resegment,
  saveResult,
} from './lib/api.js';
import {
  base64ToBytes,
  bytesToBase64,
  decodeImageFile,
  makeThumbnail,
  toWorkingImage,
} from './lib/imageProcessing.js';

// How long the slider must rest before we ask the server, so dragging from 2 to 8 sends one request instead of six.
const SLIDER_DEBOUNCE_MS = 150;

// Settings the page starts with, and returns to on Reset.
const DEFAULT_K = 4;
const DEFAULT_BLUR_SIGMA = 1.0;
const DEFAULT_DISPLAY_MODE = 'palette';

export default function App() {
  // ---- State. Each useState call is one value React remembers between renders; calling the
  // setter (setK, ...) stores a new value and re-renders the component with it. ----
  const [image, setImage] = useState(null); // {filename, bitmap, working, thumbnail}
  const [k, setK] = useState(DEFAULT_K);
  const [blurSigma, setBlurSigma] = useState(DEFAULT_BLUR_SIGMA);
  const [displayMode, setDisplayMode] = useState(DEFAULT_DISPLAY_MODE);
  const [run, setRun] = useState(null); // the run the server has in memory for `image`
  const [segmentation, setSegmentation] = useState(null); // latest server result
  const [saveState, setSaveState] = useState('idle'); // for the latest result: idle | saving | saved
  const [savedExperimentId, setSavedExperimentId] = useState(null); // its log row, once saved
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState(null);
  const [history, setHistory] = useState([]); // GET /api/runs
  const [logRun, setLogRun] = useState(null); // GET /api/runs/:id, shown in the experiment log
  // Bumped by Reset. Used as the `key` of the layout, so React throws away every component
  // below it (with their internal state, e.g. the chart's eigenvector toggle) and starts fresh.
  const [sessionKey, setSessionKey] = useState(0);
  // useRef holds a value that survives re-renders without causing one. Requests compare it
  // with the session they started in, so an answer arriving after Reset is ignored.
  const sessionRef = useRef(0);
  // Recent Jobs hides runs created before this time (set by Reset). Saved logs stay in the
  // database, so reloading the page shows them again.
  const [historySince, setHistorySince] = useState(0);

  // Decoding the base64 labels is not free, so only redo it when `segmentation` changes.
  const labels = useMemo(
    () => (segmentation ? base64ToBytes(segmentation.labels) : null),
    [segmentation],
  );

  // useCallback keeps the same function object between renders, so effects that depend on
  // these functions don't re-run on every render.
  const refreshHistory = useCallback(async () => {
    try {
      setHistory(await listRuns());
    } catch (err) {
      setError(`Could not load history: ${err.message}`);
    }
  }, []);

  const showRunLog = useCallback(async (runId) => {
    try {
      setLogRun(await getRun(runId));
    } catch (err) {
      setError(`Could not load run #${runId}: ${err.message}`);
    }
  }, []);

  // Load the history once, when the page opens. The empty-ish dependency list means
  // "run after the first render only" (refreshHistory never changes).
  useEffect(() => {
    refreshHistory();
  }, [refreshHistory]);

  // ---- A new file: decode it and do all image processing in the browser. ----
  async function handleFileSelected(file) {
    try {
      const bitmap = await decodeImageFile(file);
      setImage({
        filename: file.name,
        bitmap,
        working: toWorkingImage(bitmap),
        thumbnail: makeThumbnail(bitmap),
      });
      // The old run belongs to the old image.
      setRun(null);
      setSegmentation(null);
      setError(null);
    } catch {
      setError(`Could not read ${file.name} as an image.`);
    }
  }

  // ---- Run Segmentation: upload the working image, which creates a new run. ----
  async function handleRun() {
    if (!image) return;
    setBusy(true);
    setError(null);
    const session = sessionRef.current;
    try {
      const result = await createRun({
        filename: image.filename,
        width: image.working.width,
        height: image.working.height,
        pixels: bytesToBase64(image.working.rgb),
        k,
        thumbnail: image.thumbnail,
        blurSigma,
      });
      if (session !== sessionRef.current) return; // Reset was pressed while computing
      setRun(result.run);
      setSegmentation(result.segmentation);
      setSaveState('idle');
      // Keep the selected job's log on screen (e.g. re-running the same image with another
      // sigma). Only an empty log panel switches to the new run.
      if (!logRun) showRunLog(result.run.id);
    } catch (err) {
      if (session === sessionRef.current) setError(err.message);
    } finally {
      if (session === sessionRef.current) setBusy(false);
    }
  }

  // ---- The slider: when k changes and the server has this run in memory, re-segment. ----
  // This effect runs after every render in which k, run or segmentation changed.
  useEffect(() => {
    if (!run || !segmentation || segmentation.k === k) return undefined;

    // AbortController lets us cancel the request if k changes again before it answers.
    const controller = new AbortController();
    const timer = setTimeout(async () => {
      setBusy(true);
      try {
        const result = await resegment(run.id, k, controller.signal);
        setSegmentation(result);
        setSaveState('idle'); // a new result: nothing is logged until the user saves it
      } catch (err) {
        if (err.name === 'AbortError') return; // superseded by a newer slider value
        if (err.status === 410) {
          // The server dropped this run (restart or eviction). Forget it, so Run uploads again.
          setRun(null);
          setError('This run has expired on the server. Press Run Segmentation to compute it again.');
        } else {
          setError(err.message);
        }
      } finally {
        if (!controller.signal.aborted) setBusy(false);
      }
    }, SLIDER_DEBOUNCE_MS);

    // Cleanup: runs before the next execution of this effect (k changed again) or on unmount.
    // Cancelling here means only the latest slider value's answer is ever shown.
    return () => {
      clearTimeout(timer);
      controller.abort();
      setBusy(false);
    };
  }, [k, run, segmentation]);

  // ---- Save to log: the user decides which results go into the experiment log. ----
  async function handleSave() {
    if (!run || !segmentation) return;
    setSaveState('saving');
    try {
      const saved = await saveResult(run.id, segmentation.result_id);
      setSaveState('saved');
      setSavedExperimentId(saved.experiment_id);
      refreshHistory();
      showRunLog(run.id);
    } catch (err) {
      setSaveState('idle');
      setError(err.status === 410
        ? 'This run has expired on the server. Press Run Segmentation to compute it again.'
        : `Could not save: ${err.message}`);
    }
  }

  // ---- Deleting from the log. ----
  async function handleDeleteExperiment(experimentId) {
    try {
      await deleteExperiment(experimentId);
      // Deleted the saved copy of the result on screen: allow saving it again.
      if (experimentId === savedExperimentId) setSaveState('idle');
      refreshHistory(); // a run whose log becomes empty drops out of Recent Jobs
      showRunLog(logRun.id);
    } catch (err) {
      setError(`Could not delete: ${err.message}`);
    }
  }

  async function handleClearLog() {
    // window.confirm shows the browser's OK / Cancel dialog and returns true on OK.
    if (!window.confirm(`Delete all ${logRun.experiments.length} entries of run #${logRun.id}?`)) return;
    try {
      await clearLog(logRun.id);
      if (logRun.id === run?.id) setSaveState('idle');
      refreshHistory();
      showRunLog(logRun.id);
    } catch (err) {
      setError(`Could not clear the log: ${err.message}`);
    }
  }

  // ---- Reset: back to a clean page with default settings. Saved logs stay in the database
  // (delete them with the log buttons); everything on screen is cleared. ----
  function handleReset() {
    sessionRef.current += 1;
    setSessionKey((key) => key + 1);
    setImage(null);
    setK(DEFAULT_K);
    setBlurSigma(DEFAULT_BLUR_SIGMA);
    setDisplayMode(DEFAULT_DISPLAY_MODE);
    setRun(null);
    setSegmentation(null);
    setSaveState('idle');
    setSavedExperimentId(null);
    setBusy(false);
    setError(null);
    setLogRun(null);
    setHistorySince(Date.now());
  }

  // ---- Layout. JSX looks like HTML, but {...} embeds JavaScript values, and capitalized tags
  // like <ControlsCard> are our own components, called with these attributes as `props`. ----
  return (
    <div className="app">
      <header className="topbar">
        <img src="/logo.svg" alt="" className="logo" />
        <div>
          <h1>Spectral Image Segmentation</h1>
          <p>Normalized cuts with Rayleigh quotient iteration</p>
        </div>
        <button type="button" className="button secondary topbar-action" onClick={handleReset}
          title="Clear the page and restore default settings">
          ↺ Reset
        </button>
      </header>

      {error && (
        <div className="banner critical" role="alert">
          <span className="banner-icon" aria-hidden="true">!</span>
          <div>{error}</div>
          <button type="button" className="link-button" onClick={() => setError(null)}>
            Dismiss
          </button>
        </div>
      )}

      <div className="layout" key={sessionKey}>
        <aside className="sidebar">
          <ControlsCard
            k={k}
            onKChange={setK}
            blurSigma={blurSigma}
            onBlurSigmaChange={setBlurSigma}
            graphSettingsPending={run != null && run.blur_sigma !== blurSigma}
            displayMode={displayMode}
            onDisplayModeChange={setDisplayMode}
            onFileSelected={handleFileSelected}
            onRun={handleRun}
            canRun={image != null}
            busy={busy}
            hasLiveRun={run != null}
          />
          <JobInfoCard image={image} run={run} />
          <RecentJobs
            runs={history.filter((job) => job.created_at_ms >= historySince)}
            selectedRunId={logRun?.id}
            onSelect={showRunLog}
            onRefresh={refreshHistory}
          />
        </aside>

        <main className="content">
          <div className="grid-2">
            <OriginalImage image={image} />
            <SegmentedImage image={image} labels={labels} displayMode={displayMode} busy={busy} />
          </div>
          <div className="grid-3">
            <EigenvalueChart eigenvalues={segmentation?.cached_eigenvalues ?? []} />
            <ConvergenceChart
              eigenvalues={segmentation?.eigenvalues ?? []}
              residualHistories={segmentation?.residual_histories ?? []}
            />
            <MetricsCard segmentation={segmentation} onSave={handleSave} saveState={saveState} />
          </div>
          <ExperimentLog
            run={logRun}
            isCurrentRun={logRun != null && (logRun.id === run?.id || logRun.filename === image?.filename)}
            onDelete={handleDeleteExperiment}
            onClear={handleClearLog}
          />
        </main>
      </div>
    </div>
  );
}
