# Image Spectral Segmentation

Splits an image into regions using spectral clustering. Each pixel becomes a node in a sparse
similarity graph. The pipeline solves for the leading eigenvectors of the normalized affinity
matrix (Shi & Malik's normalized cuts, in the Ng–Jordan–Weiss formulation) and groups pixels with
k-means++ on that spectral embedding.

The core is written in C++17 with Eigen. A C++ HTTP server exposes it to a ReactJS frontend, and
PostgreSQL keeps a log of every run and experiment. Everything runs under Docker Compose.

**Status:** the segmentation pipeline and the web app run end to end.

## Pipeline

1. **Preprocess:** downsample to about 160 px wide, then apply a Gaussian blur.
2. **Graph:** build the sparse 8-neighbour affinity W with w_ij = exp(−‖c_i − c_j‖² / σ²), the
   degrees d, and M = D^{-1/2} W D^{-1/2}. M's eigenvalues lie in [−1, 1].
3. **Eigenvectors:** find the top k eigenpairs of M with **Rayleigh quotient iteration** (RQI),
   one at a time, each deflated against the ones found before. Each pair starts with a few
   inverse-iteration steps at the fixed shift σ = 1.001. These steer the vector towards the largest
   remaining eigenvalue, and then RQI converges cubically. Linear solves use Eigen's `SparseLU`,
   with the symbolic analysis done once.
4. **Clustering:** normalize each row of the n × k eigenvector matrix, then run k-means
   (k-means++ seeding, Lloyd iterations, best of 5 restarts).
5. **Output:** paint each segment with its mean colour.

| File | Contents |
| --- | --- |
| `src/image_io.*`, `src/preprocess.*` | image loading/saving, downsampling, blur |
| `src/graph.*` | affinity matrix W, degrees, normalized M |
| `src/eigensolver.*` | RQI with warm-up and deflation |
| `src/clustering.*` | spectral embedding, k-means, rendering |
| `src/main.cpp` | `segment`: the full pipeline |
| `src/rqi_experiments.cpp` | `rqi_bench`: eigensolver experiments |
| `src/server/` | `seg_server`: HTTP API, in-memory run cache, PostgreSQL access |
| `db/schema.sql` | database tables |
| `web/` | ReactJS frontend (Vite) |

## Web app

```sh
docker compose up --build        # then open http://localhost:8080
```

```
 browser (React)                       seg_server (C++)                     PostgreSQL
 ───────────────                       ────────────────                     ──────────
 decode + resize on <canvas>  ──POST /api/runs──▶  blur, W, M, RQI, k-means  ──INSERT runs──▶
 paint labels on <canvas>     ◀── labels (base64)   cache M + eigenpairs
 move k slider                ──POST /api/runs/:id/segmentations──▶ reuse cached eigenpairs
 Save to log                  ──POST /api/runs/:id/experiments───▶ latest result ──INSERT experiments──▶
 history / experiment log     ──GET /api/runs, /api/runs/:id──────────────────  SELECT ...
```

- **The browser does the image processing.** It decodes the upload, resizes it to at most 160 px
  on a `<canvas>`, and sends raw RGB bytes. It also paints the returned labels, either with
  palette colours or with each segment's mean colour.
- **Slider changes reuse cached eigenvectors.** The server keeps each run's matrix M and its
  eigenpairs in an in-memory LRU map keyed by `run_id`, holding 20 runs (`CACHE_CAPACITY`). A
  smaller k only reruns k-means, typically in 20–200 ms. A larger k computes just the missing
  eigenpairs, since RQI with deflation finds them one at a time. After a restart or an
  eviction, the API returns `410 Gone` and the page asks for a new upload.
- **PostgreSQL stores the log, not the results.** The `runs` table has one row per upload. The
  `experiments` table only gets a row when the user presses **Save to log**. Each row holds k,
  runtime, eigensolver and k-means time and iterations, memory usage and eigenvalues. Recent
  Jobs lists only runs with saved results. Each log row has a delete button, **Clear log**
  empties a run's log, and **Download CSV** exports it. **Reset** clears the page and restores
  the default settings without touching saved logs.
- **What triggers a recompute.** Changing k only reruns the clustering, plus any missing
  eigenpairs. Changing the Gaussian sigma changes the graph, so pressing Run builds a new run,
  with a new graph and new eigenvectors.
- **Memory usage** is the run's peak working set: M, the stored eigenvectors, and the largest
  sparse LU factorization.

| Endpoint | Purpose |
| --- | --- |
| `POST /api/runs` | `{filename, width, height, pixels, k, thumbnail, blur_sigma}` → new run + first segmentation |
| `POST /api/runs/:id/segmentations` | `{k}` → segmentation from the cache (`410` if evicted) |
| `POST /api/runs/:id/experiments` | `{result_id}` → save the latest result to the log (`409` if replaced) |
| `DELETE /api/experiments/:id` | delete one log entry |
| `DELETE /api/runs/:id/experiments` | clear a run's log |
| `GET /api/runs?limit=20` | history of recent runs |
| `GET /api/runs/:id` | one run with its experiment log |
| `GET /api/health` | liveness check |

Frontend development with hot reload (needs Node 20+ locally): keep the compose stack running,
then run `cd web && npm install && npm run dev` and open http://localhost:5173. Vite forwards
`/api` to port 8080. To inspect the database, run `psql postgresql://seg:seg@localhost:5433/seg`.
To reset it, run `docker compose down -v`.

## Build and run the command-line tools

With Docker:

```sh
docker build -t seg .
docker run --rm seg
```

Locally (requires CMake at least v3.28, a C++17 compiler, and Eigen 3; `seg_server` also needs
`libpq-dev` and is skipped without it):

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/segment data/samples/shapes.png   # writes out/segments.png
./build-release/rqi_bench                          # eigensolver experiments, about 90 s
```

## Eigensolver experiments (`rqi_bench`)

Test images are about 48 × 40 pixels (n ≈ 2000), so a dense eigensolver can supply the exact
spectrum to compare against. "Ranks" are the positions in the spectrum of the eigenvalues that
were found (1 = largest). "Exact" is the accuracy when the exact top-k eigenvectors go through
the same k-means.

| Image | k | Gap λ_k − λ_{k+1} | Plain RQI ranks | Warm RQI ranks | Accuracy (plain / warm / exact) |
| --- | --- | --- | --- | --- | --- |
| Clean quadrants | 4 | 2.3e-3 | ~680 | 1–4 | 46% / 100% / 100% |
| `shapes.png` | 3 | 1.6e-4 | ~790 | 1, 2, 4 | 38% / 69% / 62% |
| Low contrast + noise | 4 | 2.6e-3 | ~650 | 1–4 | 27% / 100% / 100% |
| Smooth gradient | 4 | 2.5e-3 | ~645 | 1–4 | n/a |

What the experiments show:

- **Plain RQI finds the wrong eigenvalues.** RQI converges to the eigenvalue nearest its starting
  Rayleigh quotient. M has a zero diagonal, so a random vector's quotient is about tr(M)/n = 0,
  and RQI lands in the middle of the spectrum. Three or more warm-up steps fix this on most images.
- **Clustered eigenvalues defeat one-at-a-time search.** On `shapes.png`, λ₃ and λ₄ differ by
  1.6e-4, and RQI returns λ₄ instead of λ₃. Even with exact eigenvectors, the smooth modes of the
  gradient background compete with the shapes, so that image's main limitation is the graph model.
- **Convergence is cubic only near the end.** Residuals go
  4.8e-6 → 5.9e-8 → 1.1e-10 → 1.1e-15, so each step gains more digits (2, then 3, then 5). The
  cubic rate only shows once the residual is small compared with the eigengap, because the
  constant scales with 1/gap².
- **Cost:** each RQI step needs a fresh sparse LU factorization. That takes 3 ms at 40², 22 ms at
  80², 133 ms at 160², and 347 ms at 240². The full 160 × 120 pipeline needs 21 factorizations
  (about 2.4 s).

Next steps: block inverse iteration with Rayleigh–Ritz (all k eigenpairs from one factorization,
returned in order) or a Lanczos solver (Spectra), and a graph model that handles smooth
backgrounds.
