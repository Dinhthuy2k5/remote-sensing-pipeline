# Dashboard Frontend

Frontend React/Vite dùng để điều khiển pipeline xử lý ảnh viễn thám và trực quan hóa kết quả trên bản đồ MapLibre.

---

## Chức Năng

- Chọn model và cấu hình `tile_size`, `overlap`, `workers`, `confidence`.
- Upload GeoTIFF lên C++ core qua HTTP.
- Start/cancel session.
- Poll `/status` để hiển thị state và progress.
- Nhận telemetry realtime qua WebSocket bridge.
- Hiển thị CPU, RAM, FPS, queue size bằng Recharts.
- Vẽ GeoJSON polygon trên MapLibre theo class.
- Hiển thị Top Classes và Land Cover Coverage.

---

## Kiến Trúc Giao Diện

```text
frontend/
|-- bridge.cjs                 # UDP :9090 -> WebSocket :9091
|-- src/
|   |-- App.jsx                # Layout chính và state orchestration
|   |-- api/backend.js         # Wrapper gọi HTTP API backend
|   |-- components/
|   |   |-- ControlPanel.jsx   # Upload, config, start/cancel, log
|   |   |-- MapView.jsx        # MapLibre layers và GeoJSON overlay
|   |   `-- TelemetryChart.jsx # Biểu đồ CPU/RAM/FPS
|   |-- hooks/useTelemetry.js  # WebSocket telemetry hook
|   `-- models/modelRegistry.js# Class name, màu, model metadata
```

---

## Chạy Development UI

```powershell
cd frontend
npm install
npm run dev
```

Mở:

```text
http://localhost:5173
```

---

## Chạy Telemetry Bridge

Trình duyệt không nhận UDP trực tiếp, nên cần chạy bridge trên host:

```powershell
cd frontend
npm run bridge
```

Bridge:

- nhận UDP metrics từ C++ core ở port `9090`;
- phát WebSocket cho browser ở `ws://localhost:9091`.

---

## Build Frontend Tĩnh

```powershell
cd frontend
npm run build
```

Hoặc chạy bằng Docker Compose:

```powershell
docker compose up --build frontend
```

Lưu ý: frontend container chỉ serve static UI. `bridge.cjs` vẫn cần chạy riêng trên host nếu muốn telemetry realtime.

---

## Ghi Chú Vận Hành

- API base URL mặc định trỏ về `http://localhost:8080`.
- WebSocket telemetry mặc định là `ws://localhost:9091`.
- Với kết quả nhiều polygon, MapLibre có thể render chậm; nên tăng confidence hoặc giảm vùng test nếu cần demo nhanh.
- Land Cover Coverage lấy từ backend/PostGIS, không tự tính trong browser.
