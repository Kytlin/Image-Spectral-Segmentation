# Image Spectral Segmentation

Splits an image into regions using spectral clustering. Each pixel becomes a node in a sparse
similarity graph. The pipeline solves for the leading eigenvectors of the normalized affinity
matrix (Shi & Malik's normalized cuts, in the Ng–Jordan–Weiss formulation) and groups pixels with
k-means++ on that spectral embedding.

The core is written in C++17 with Eigen. It will be served by an asynchronous job backend (HTTP
API, PostgreSQL) and ReactJS frontend, all running under Docker Compose.

**Status:** the segmentation pipeline runs end to end. The web backend is not started yet.

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

## Build and run

With Docker:

```sh
docker build -t seg .
docker run --rm seg
```

Locally (requires CMake at least v3.28, a C++17 compiler, and Eigen 3):

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
| Clean quadrants | 4 | 2.3e-3 | ~680 | 1–4 | 50% / 100% / 100% |
| `shapes.png` | 3 | 1.6e-4 | ~775 | 1, 2, 4 | 38% / 69% / 62% |
| Low contrast + noise | 4 | 2.6e-3 | ~660 | 1–4 | 27% / 100% / 100% |
| Smooth gradient | 4 | 2.5e-3 | ~655 | 1–4 | n/a |

What the experiments show:

- **Plain RQI finds the wrong eigenvalues.** RQI converges to the eigenvalue nearest its starting
  Rayleigh quotient. M has a zero diagonal, so a random vector's quotient is about tr(M)/n = 0,
  and RQI lands in the middle of the spectrum. Three or more warm-up steps fix this on most images.
- **Clustered eigenvalues defeat one-at-a-time search.** On `shapes.png`, λ₃ and λ₄ differ by
  1.6e-4, and RQI returns λ₄ instead of λ₃. Even with exact eigenvectors, the smooth modes of the
  gradient background compete with the shapes, so that image's main limitation is the graph model.
- **Convergence is cubic only near the end.** Residuals go
  1.6e-5 → 8e-6 → 6.6e-7 → 9.8e-9 → 1.4e-11 → 2e-16. The constant scales with 1/gap².
- **Cost:** each RQI step needs a fresh sparse LU factorization. That takes 3 ms at 40², 22 ms at
  80², 133 ms at 160², and 347 ms at 240². The full 160 × 120 pipeline needs 21 factorizations
  (about 2.4 s).

Next steps: block inverse iteration with Rayleigh–Ritz (all k eigenpairs from one factorization,
returned in order) or a Lanczos solver (Spectra), and a graph model that handles smooth
backgrounds.
