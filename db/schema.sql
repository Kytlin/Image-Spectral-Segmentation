-- Schema for the segmentation server. The postgres container runs this file once, when its
-- data volume is first created (see docker-compose.yml). After changing it, recreate the volume:
--     docker compose down -v && docker compose up --build

-- One row per uploaded image. The graph and eigenvectors for a run live in the server's memory;
-- this table only keeps what is needed to list and describe the run later.
CREATE TABLE runs (
    id           BIGSERIAL PRIMARY KEY,
    created_at   TIMESTAMPTZ NOT NULL DEFAULT now(),
    filename     TEXT NOT NULL,
    width        INTEGER NOT NULL CHECK (width > 0),   -- size of the image the server received
    height       INTEGER NOT NULL CHECK (height > 0),
    num_nodes    INTEGER NOT NULL,                     -- pixels = graph nodes
    num_edges    INTEGER NOT NULL,                     -- nonzeros of W (affinity matrix)
    blur_sigma   DOUBLE PRECISION NOT NULL,
    sigma_color  DOUBLE PRECISION NOT NULL,
    weight_threshold DOUBLE PRECISION NOT NULL,        -- edges lighter than this are dropped
    graph_ms     DOUBLE PRECISION NOT NULL,            -- blur + building W and M (diagonal matrix term of Laplacian)
    thumbnail    TEXT                                  -- small JPEG as a data: URL, for the history list
);

-- One row per segmentation result the user chose to keep (the "Save to log" button).
CREATE TABLE experiments (
    id                    BIGSERIAL PRIMARY KEY,

    -- removing a row for uploaded image will remove corresponding experiment rows
    run_id                BIGINT NOT NULL REFERENCES runs (id) ON DELETE CASCADE,

    created_at            TIMESTAMPTZ NOT NULL DEFAULT now(),
    k                     INTEGER NOT NULL CHECK (k > 0),
    eigenpairs_cached     INTEGER NOT NULL,            -- eigenpairs already in memory before this request
    eigenpairs_computed   INTEGER NOT NULL,            -- new eigenpairs computed for this request
    factorizations        INTEGER NOT NULL,            -- sparse LU factorizations done for this request
    eigen_ms              DOUBLE PRECISION NOT NULL,
    kmeans_ms             DOUBLE PRECISION NOT NULL,
    max_residual          DOUBLE PRECISION NOT NULL,   -- worst ||Mv - lambda v|| among the k pairs used
    all_converged         BOOLEAN NOT NULL,
    eigenvalues           DOUBLE PRECISION[] NOT NULL, -- the k eigenvalues used, in the order found

    -- algorithm time of this request, which is total time spent on built, eigen, and k-means stage
    runtime_ms            DOUBLE PRECISION NOT NULL,   

    eigensolver_iterations INTEGER NOT NULL,           -- consists of RQI steps done for this request
    kmeans_iterations     INTEGER NOT NULL,            -- Lloyd iterations of the kept k-means run
    memory_bytes          BIGINT NOT NULL              -- M + stored eigenvectors + largest LU factorization
);

-- The history endpoint lists recent runs, and a run's page lists its experiments.
-- These indexes make both lookups fast as the tables grow.
CREATE INDEX runs_created_at_idx ON runs (created_at DESC);
CREATE INDEX experiments_run_id_idx ON experiments (run_id, created_at);
