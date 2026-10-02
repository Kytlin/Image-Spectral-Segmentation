# Image Spectral Segmentation

Splits an image into regions using spectral clustering. Each pixel becomes a node in a sparse
similarity graph. The pipeline solves for the leading eigenvectors of the normalized affinity
matrix (Shi & Malik's normalized cuts, in the Ng–Jordan–Weiss formulation) and groups pixels with
k-means++ on that spectral embedding.

The core is written in C++17 with Eigen and Spectra. It will be served by an asynchronous job
backend (HTTP API, PostgreSQL, Redis-backed worker pool) and a React front end, all running under
Docker Compose.

**Status:** early development. The build system and container are in place; the segmentation
pipeline is in progress.

## Build and run

With Docker:

```sh
docker build -t seg .
docker run --rm seg
```

Locally (requires CMake at least v3.28, a C++17 compiler, and Eigen 3):

```sh
cmake -S . -B build
cmake --build build
./build/segment
```
