import { useState } from "react";
import MapView from "./components/MapView";
import ControlPanel from "./components/ControlPanel";
import TelemetryChart from "./components/TelemetryChart";
import { classStats, modelInfo } from "./models/modelRegistry";

export default function App() {
  const [geojson, setGeojson] = useState(null);
  const [sessionId, setSessionId] = useState(null);
  const [modelKey, setModelKey] = useState("mock");
  const [footprint, setFootprint] = useState(null);
  const stats = classStats(geojson, modelKey);

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
          onModelChange={setModelKey}
          onFootprintChange={setFootprint}
        />

        <TelemetryChart />

        <div style={{
          background: "#1e1e2e", borderRadius: 8,
          padding: "10px 12px", color: "#cdd6f4",
          fontSize: 11, fontFamily: "monospace",
        }}>
          <div style={{
            fontWeight: "bold",
            marginBottom: 8,
            color: "#f8f8f2",
          }}>
            Top Classes · {modelInfo(modelKey).label}
          </div>
          {stats.length > 0 ? stats.slice(0, 6).map(item => (
            <div key={item.classId} style={{
              display: "grid",
              gridTemplateColumns: "12px 1fr auto",
              alignItems: "center",
              gap: 8,
              marginBottom: 5,
            }}>
              <span style={{
                width: 10,
                height: 10,
                borderRadius: 2,
                background: item.color,
              }} />
              <span>{item.name}</span>
              <span style={{ color: "#a6adc8" }}>{item.count}</span>
            </div>
          )) : (
            <div style={{ color: "#a6adc8" }}>
              Run a session to populate class counts.
            </div>
          )}
        </div>

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
        <MapView geojson={geojson} footprint={footprint} modelKey={modelKey} />
      </div>
    </div>
  );
}
