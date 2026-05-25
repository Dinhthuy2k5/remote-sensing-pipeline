CREATE EXTENSION IF NOT EXISTS postgis;

CREATE TABLE IF NOT EXISTS sessions (
    id          BIGSERIAL PRIMARY KEY,
    filename    TEXT NOT NULL,
    status      TEXT NOT NULL DEFAULT 'IDLE',
    created_at  TIMESTAMPTZ DEFAULT NOW(),
    updated_at  TIMESTAMPTZ DEFAULT NOW(),
    tile_total  INT DEFAULT 0,
    tile_done   INT DEFAULT 0
);

CREATE TABLE IF NOT EXISTS detections (
    id          BIGSERIAL PRIMARY KEY,
    session_id  BIGINT NOT NULL REFERENCES sessions(id) ON DELETE CASCADE,
    geom        GEOMETRY(Polygon, 4326) NOT NULL,
    class_id    INT NOT NULL,
    confidence  FLOAT NOT NULL,
    created_at  TIMESTAMPTZ DEFAULT NOW()
);

CREATE INDEX IF NOT EXISTS idx_detections_geom
    ON detections USING GIST(geom);

CREATE INDEX IF NOT EXISTS idx_detections_session
    ON detections(session_id);
