CREATE TABLE deployments (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    repository_id INTEGER NOT NULL REFERENCES repositories(id) ON DELETE RESTRICT,
    branch TEXT NOT NULL,
    commit_sha TEXT NOT NULL,
    slug TEXT NOT NULL,
    hostname TEXT NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('queued', 'starting', 'running', 'failed', 'stopped')),
    frontend_port INTEGER,
    backend_port INTEGER,
    compose_project TEXT NOT NULL,
    worktree_path TEXT NOT NULL,
    created_at TEXT NOT NULL,
    started_at TEXT,
    stopped_at TEXT,
    updated_at TEXT NOT NULL,
    error_message TEXT
);

CREATE INDEX idx_deployments_repository_branch
    ON deployments(repository_id, branch, id DESC);
CREATE INDEX idx_deployments_hostname_status
    ON deployments(hostname, status);
CREATE INDEX idx_deployments_status ON deployments(status);
CREATE INDEX idx_deployments_ports ON deployments(frontend_port, backend_port);
