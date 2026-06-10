import { useState } from "react";
import MapView from "./components/MapView";
import ControlPanel from "./components/ControlPanel";
import TelemetryChart from "./components/TelemetryChart";

export default function App() {
  const [geojson, setGeojson] = useState(null);
  const [sessionId, setSessionId] = useState(null);

  return (
    <div style={{
      display: "grid",
      gridTemplateColumns: "340px 1fr",
      gridTemplateRows: "1fr",
      height: "100vh",
      background: "#11111b",
      gap: 8,
      padding: 8,
      boxSizing: "border-box",
    }}>
      {/* Left panel */}
      <div style={{
        display: "flex", flexDirection: "column", gap: 8,
        overflowY: "auto",
      }}>
        <div style={{
          color: "#cdd6f4", fontFamily: "monospace",
          fontSize: 16, fontWeight: "bold",
          padding: "8px 4px",
        }}>
          🛰️ Remote Sensing Pipeline
        </div>

        <ControlPanel
          onResults={setGeojson}
          onSessionChange={setSessionId}
        />

        <TelemetryChart />

        {sessionId && (
          <div style={{
            background: "#1e1e2e", borderRadius: 8,
            padding: "8px 12px", color: "#a6adc8",
            fontSize: 11, fontFamily: "monospace",
          }}>
            Session ID: {sessionId}
            {geojson && (
              <span style={{ color: "#10b981", marginLeft: 8 }}>
                ✓ {geojson.features?.length} detections
              </span>
            )}
          </div>
        )}
      </div>

      {/* Map */}
      <div style={{ borderRadius: 12, overflow: "hidden" }}>
        <MapView geojson={geojson} />
      </div>
    </div>
  );
}