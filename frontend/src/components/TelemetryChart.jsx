import { useTelemetry } from "../hooks/useTelemetry";
import {
    LineChart, Line, XAxis, YAxis, CartesianGrid,
    Tooltip, Legend, ResponsiveContainer
} from "recharts";

export default function TelemetryChart() {
    const { metrics, history, connected } = useTelemetry();

    const statusColor = {
        IDLE: "#6b7280",
        LOADING: "#3b82f6",
        TILING: "#8b5cf6",
        PROCESSING: "#f59e0b",
        STITCHING: "#ec4899",
        SAVING: "#06b6d4",
        DONE: "#10b981",
        ERROR: "#ef4444",
    };

    const state = metrics?.state ?? "IDLE";
    const color = statusColor[state] ?? "#6b7280";
    const progress = metrics?.tiles_total > 0
        ? Math.round(metrics.tiles_done * 100 / metrics.tiles_total)
        : 0;

    return (
        <div style={{
            background: "#1e1e2e",
            borderRadius: 12,
            padding: 16,
            color: "#cdd6f4",
            fontFamily: "monospace",
        }}>
            {/* Header */}
            <div style={{
                display: "flex", justifyContent: "space-between",
                alignItems: "center", marginBottom: 12
            }}>
                <span style={{ fontWeight: "bold", fontSize: 14 }}>
                    📡 Telemetry
                </span>
                <span style={{
                    fontSize: 11,
                    padding: "2px 8px",
                    borderRadius: 999,
                    background: connected ? "#10b981" : "#ef4444",
                    color: "#fff"
                }}>
                    {connected ? "LIVE" : "DISCONNECTED"}
                </span>
            </div>

            {/* State badge + progress */}
            <div style={{
                display: "flex", gap: 12,
                alignItems: "center", marginBottom: 12
            }}>
                <span style={{
                    padding: "3px 10px", borderRadius: 6,
                    background: color, color: "#fff",
                    fontSize: 12, fontWeight: "bold",
                }}>
                    {state}
                </span>
                {metrics?.tiles_total > 0 && (
                    <div style={{ flex: 1 }}>
                        <div style={{ fontSize: 11, marginBottom: 3 }}>
                            {metrics.tiles_done}/{metrics.tiles_total} tiles ({progress}%)
                        </div>
                        <div style={{
                            height: 6, background: "#313244",
                            borderRadius: 3, overflow: "hidden"
                        }}>
                            <div style={{
                                height: "100%", width: `${progress}%`,
                                background: color,
                                transition: "width 0.3s ease",
                            }} />
                        </div>
                    </div>
                )}
            </div>

            {/* Metrics row */}
            {metrics && (
                <div style={{
                    display: "grid", gridTemplateColumns: "1fr 1fr 1fr 1fr",
                    gap: 8, marginBottom: 16,
                }}>
                    {[
                        { label: "CPU", value: `${metrics.cpu_percent?.toFixed(1)}%` },
                        { label: "RAM", value: `${metrics.ram_used_mb} MB` },
                        { label: "FPS", value: metrics.fps?.toFixed(1) },
                        { label: "Queue", value: metrics.queue_size ?? 0 },
                    ].map(({ label, value }) => (
                        <div key={label} style={{
                            background: "#313244", borderRadius: 8,
                            padding: "6px 10px", textAlign: "center",
                        }}>
                            <div style={{ fontSize: 10, color: "#a6adc8" }}>{label}</div>
                            <div style={{ fontSize: 14, fontWeight: "bold" }}>{value}</div>
                        </div>
                    ))}
                </div>
            )}

            {/* Chart */}
            <ResponsiveContainer width="100%" height={140}>
                <LineChart data={history}
                    margin={{ top: 0, right: 0, bottom: 0, left: -20 }}>
                    <CartesianGrid strokeDasharray="3 3" stroke="#313244" />
                    <XAxis dataKey="time" tick={{ fontSize: 9 }}
                        interval="preserveStartEnd" />
                    <YAxis tick={{ fontSize: 9 }} />   {/* bỏ domain cố định */}
                    <Tooltip
                        contentStyle={{
                            background: "#1e1e2e", border: "1px solid #45475a",
                            borderRadius: 6, fontSize: 11,
                        }}
                    />
                    <Legend wrapperStyle={{ fontSize: 10 }} />
                    <Line type="monotone" dataKey="cpu_percent"
                        name="CPU%" stroke="#f59e0b"
                        dot={false} strokeWidth={1.5}
                        isAnimationActive={false} />
                    <Line type="monotone" dataKey="fps"
                        name="FPS" stroke="#10b981"
                        dot={false} strokeWidth={1.5}
                        isAnimationActive={false} />
                    <Line type="monotone" dataKey="ram_used_mb"
                        name="RAM(MB)" stroke="#3b82f6"
                        dot={false} strokeWidth={1.5}
                        isAnimationActive={false} />
                </LineChart>
            </ResponsiveContainer>
        </div>
    );
}