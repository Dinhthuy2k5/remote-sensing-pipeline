import { useState, useRef } from "react";
import {
    uploadFile, setConfig, startSession,
    cancelSession, getStatus, getResults
} from "../api/backend";

export default function ControlPanel({ onResults, onSessionChange }) {
    const [sessionId, setSessionId] = useState(null);
    const [status, setStatus] = useState("idle");
    const [progress, setProgress] = useState(0);
    const [log, setLog] = useState([]);
    const [config, setConfigState] = useState({
        tile_size: 512, overlap: 64, model: "mock"
    });
    const pollingRef = useRef(null);

    const addLog = (msg) => setLog(prev =>
        [...prev.slice(-19),
        `[${new Date().toLocaleTimeString()}] ${msg}`]);

    // ── Upload + start flow ─────────────────────────────────
    async function handleFileChange(e) {
        const file = e.target.files[0];
        if (!file) return;

        addLog(`Uploading ${file.name} (${(file.size / 1e6).toFixed(1)}MB)...`);
        setStatus("uploading");

        try {
            const r = await uploadFile(file);
            const sid = r.session_id;
            setSessionId(sid);
            onSessionChange?.(sid);
            addLog(`Uploaded. session_id=${sid}`);

            // Config
            await setConfig(sid, config);
            addLog(`Config set: tile_size=${config.tile_size} model=${config.model}`);

            // Start
            await startSession(sid);
            addLog("Pipeline started. Polling status...");
            setStatus("running");
            startPolling(sid);

        } catch (err) {
            addLog(`Error: ${err.message}`);
            setStatus("error");
        }
    }

    function startPolling(sid) {
        pollingRef.current = setInterval(async () => {
            try {
                const s = await getStatus(sid);
                setProgress(s.progress ?? 0);
                addLog(`${s.status} ${s.tile_done}/${s.tile_total}`);

                if (s.status === "DONE") {
                    clearInterval(pollingRef.current);
                    setStatus("done");
                    addLog("Fetching results...");
                    const geojson = await getResults(sid);
                    onResults?.(geojson);
                    addLog(`Done. ${geojson.features?.length ?? 0} detections on map.`);
                } else if (s.status === "ERROR") {
                    clearInterval(pollingRef.current);
                    setStatus("error");
                    addLog("Pipeline ERROR.");
                }
            } catch (_) { }
        }, 1000);
    }

    async function handleCancel() {
        if (!sessionId) return;
        clearInterval(pollingRef.current);
        await cancelSession(sessionId);
        setStatus("cancelled");
        addLog("Session cancelled.");
    }

    // ── Render ──────────────────────────────────────────────
    const btnStyle = (color) => ({
        padding: "8px 16px", borderRadius: 6, border: "none",
        background: color, color: "#fff", cursor: "pointer",
        fontWeight: "bold", fontSize: 13,
    });

    return (
        <div style={{
            background: "#1e1e2e", borderRadius: 12,
            padding: 16, color: "#cdd6f4",
            fontFamily: "monospace", display: "flex",
            flexDirection: "column", gap: 12,
        }}>
            <div style={{ fontWeight: "bold", fontSize: 14 }}>
                🛰️ Pipeline Control
            </div>

            {/* Config */}
            <div style={{
                display: "grid",
                gridTemplateColumns: "1fr 1fr 1fr", gap: 8
            }}>
                {[
                    { key: "tile_size", label: "Tile Size", type: "number" },
                    { key: "overlap", label: "Overlap", type: "number" },
                    { key: "model", label: "Model", type: "text" },
                ].map(({ key, label, type }) => (
                    <div key={key}>
                        <div style={{
                            fontSize: 10, color: "#a6adc8",
                            marginBottom: 3
                        }}>{label}</div>
                        <input
                            type={type}
                            value={config[key]}
                            onChange={e => setConfigState(prev => ({
                                ...prev,
                                [key]: type === "number"
                                    ? parseInt(e.target.value) || 0
                                    : e.target.value
                            }))}
                            style={{
                                width: "100%", padding: "4px 8px",
                                background: "#313244", border: "1px solid #45475a",
                                borderRadius: 4, color: "#cdd6f4",
                                fontSize: 12, boxSizing: "border-box",
                            }}
                        />
                    </div>
                ))}
            </div>

            {/* Upload button */}
            <label style={{
                ...btnStyle("#3b82f6"),
                textAlign: "center", cursor: "pointer",
                opacity: status === "running" ? 0.5 : 1,
            }}>
                📂 Choose GeoTIFF
                <input type="file" accept=".tif,.tiff"
                    onChange={handleFileChange}
                    disabled={status === "running"}
                    style={{ display: "none" }} />
            </label>

            {/* Progress bar */}
            {status === "running" && (
                <div>
                    <div style={{ fontSize: 11, marginBottom: 4 }}>
                        Processing... {progress}%
                    </div>
                    <div style={{
                        height: 8, background: "#313244",
                        borderRadius: 4, overflow: "hidden"
                    }}>
                        <div style={{
                            height: "100%", width: `${progress}%`,
                            background: "#f59e0b",
                            transition: "width 0.5s ease",
                        }} />
                    </div>
                    <button onClick={handleCancel}
                        style={{
                            ...btnStyle("#ef4444"),
                            marginTop: 8, width: "100%"
                        }}>
                        ✕ Cancel
                    </button>
                </div>
            )}

            {/* Status badge */}
            <div style={{
                padding: "4px 10px", borderRadius: 6,
                background: {
                    idle: "#313244", uploading: "#3b82f6",
                    running: "#f59e0b", done: "#10b981",
                    error: "#ef4444", cancelled: "#6b7280",
                }[status] ?? "#313244",
                fontSize: 12, textAlign: "center", fontWeight: "bold",
            }}>
                {status.toUpperCase()}
            </div>

            {/* Log */}
            <div style={{
                background: "#11111b", borderRadius: 6,
                padding: 8, height: 120, overflowY: "auto",
                fontSize: 10, lineHeight: 1.6,
            }}>
                {log.map((l, i) => (
                    <div key={i} style={{ color: "#a6adc8" }}>{l}</div>
                ))}
            </div>
        </div>
    );
}