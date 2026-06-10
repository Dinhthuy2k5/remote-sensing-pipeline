import { useState, useEffect, useRef } from "react";

const WS_URL = "ws://localhost:9091";
const MAX_HISTORY = 60; // giữ 60 data points (~30 giây)

export function useTelemetry() {
    const [metrics, setMetrics] = useState(null);
    const [history, setHistory] = useState([]);
    const [connected, setConnected] = useState(false);
    const wsRef = useRef(null);

    useEffect(() => {
        function connect() {
            const ws = new WebSocket(WS_URL);
            wsRef.current = ws;

            ws.onopen = () => {
                setConnected(true);
                console.log("[Telemetry] WebSocket connected");
            };

            ws.onmessage = (evt) => {
                try {
                    const m = JSON.parse(evt.data);
                    const point = {
                        ...m,
                        time: new Date().toLocaleTimeString("vi-VN", {
                            hour: "2-digit", minute: "2-digit", second: "2-digit"
                        }),
                    };
                    setMetrics(point);
                    setHistory(prev => {
                        const next = [...prev, point];
                        return next.length > MAX_HISTORY
                            ? next.slice(-MAX_HISTORY)
                            : next;
                    });
                } catch (_) { }
            };

            ws.onclose = () => {
                setConnected(false);
                console.log("[Telemetry] Disconnected, retrying in 2s...");
                setTimeout(connect, 2000);
            };

            ws.onerror = () => ws.close();
        }

        connect();
        return () => {
            wsRef.current?.close();
        };
    }, []);

    return { metrics, history, connected };
}