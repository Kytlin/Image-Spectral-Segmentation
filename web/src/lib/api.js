// Thin wrappers around fetch() for the C++ server's endpoints (src/server/main.cpp).
// Every call takes an optional AbortSignal, so a request whose answer is no longer wanted
// (the slider moved again) can be cancelled.

export class ApiError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

async function request(path, { method = 'GET', body, signal } = {}) {
  const response = await fetch(path, {
    method,
    signal,
    headers: body ? { 'Content-Type': 'application/json' } : undefined,
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new ApiError(response.status, data.error ?? `HTTP ${response.status}`);
  }
  return data;
}

// Upload the working image and get the first segmentation. Returns {run, segmentation}.
export function createRun(
  { filename, width, height, pixels, k, thumbnail, blurSigma },
  signal,
) {
  return request('/api/runs', {
    method: 'POST',
    body: {
      filename,
      width,
      height,
      pixels,
      k,
      thumbnail,
      blur_sigma: blurSigma,
    },
    signal,
  });
}

// Re-segment a run the server still has in memory. Throws ApiError with status 410 if it doesn't.
export function resegment(runId, k, signal) {
  return request(`/api/runs/${runId}/segmentations`, { method: 'POST', body: { k }, signal });
}

// Save the run's latest result to the experiment log. `resultId` names the result on screen;
// the server refuses (409) if a newer one has replaced it.
export function saveResult(runId, resultId) {
  return request(`/api/runs/${runId}/experiments`, { method: 'POST', body: { result_id: resultId } });
}

// Delete one experiment-log entry.
export function deleteExperiment(experimentId) {
  return request(`/api/experiments/${experimentId}`, { method: 'DELETE' });
}

// Delete every experiment-log entry of a run.
export function clearLog(runId) {
  return request(`/api/runs/${runId}/experiments`, { method: 'DELETE' });
}

export function listRuns(limit = 20) {
  return request(`/api/runs?limit=${limit}`);
}

export function getRun(runId) {
  return request(`/api/runs/${runId}`);
}
