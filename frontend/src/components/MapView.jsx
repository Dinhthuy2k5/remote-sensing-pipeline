import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";

const CLASS_COLORS = {
    0: "#22c55e",  // vegetation - green
    1: "#3b82f6",  // water      - blue
    2: "#f97316",  // building   - orange
    3: "#a855f7",  // road       - purple
};
const CLASS_NAMES = ["Vegetation", "Water", "Building", "Road"];

export default function MapView({ geojson }) {
    const mapRef = useRef(null);
    const mapInst = useRef(null);

    useEffect(() => {
        mapInst.current = new maplibregl.Map({
            container: mapRef.current,
            style: {
                version: 8,
                sources: {
                    osm: {
                        type: "raster",
                        tiles: ["https://tile.openstreetmap.org/{z}/{x}/{y}.png"],
                        tileSize: 256,
                        attribution: "© OpenStreetMap contributors",
                    },
                },
                layers: [{
                    id: "osm-tiles",
                    type: "raster",
                    source: "osm",
                }],
            },
            center: [-78.56, 41.05],  // Pennsylvania (test.tif area)
            zoom: 10,
        });

        mapInst.current.addControl(new maplibregl.NavigationControl());

        return () => mapInst.current?.remove();
    }, []);

    // Vẽ detections lên map khi geojson thay đổi
    useEffect(() => {
        const map = mapInst.current;
        if (!map || !geojson?.features?.length) return;

        const addLayers = () => {
            // Xóa layer cũ nếu có
            [0, 1, 2, 3].forEach(classId => {
                const id = `detections-${classId}`;
                if (map.getLayer(id)) map.removeLayer(id);
                if (map.getSource(id)) map.removeSource(id);
            });

            // Group theo class_id
            const byClass = { 0: [], 1: [], 2: [], 3: [] };
            geojson.features.forEach(f => {
                const c = f.properties?.class_id ?? 0;
                if (byClass[c]) byClass[c].push(f);
            });

            // Thêm layer cho từng class
            [0, 1, 2, 3].forEach(classId => {
                const features = byClass[classId];
                if (!features.length) return;

                const sourceId = `detections-${classId}`;
                map.addSource(sourceId, {
                    type: "geojson",
                    data: { type: "FeatureCollection", features },
                });

                // Fill
                map.addLayer({
                    id: sourceId,
                    type: "fill",
                    source: sourceId,
                    paint: {
                        "fill-color": CLASS_COLORS[classId],
                        "fill-opacity": 0.35,
                    },
                });

                // Outline
                map.addLayer({
                    id: `${sourceId}-outline`,
                    type: "line",
                    source: sourceId,
                    paint: {
                        "line-color": CLASS_COLORS[classId],
                        "line-width": 1.5,
                        "line-opacity": 0.8,
                    },
                });
            });

            // Fly to first detection
            const first = geojson.features[0]?.geometry?.coordinates?.[0]?.[0];
            if (first) {
                map.flyTo({
                    center: first,
                    zoom: 13,
                    duration: 1500,
                });
            }

            console.log(`[Map] Rendered ${geojson.features.length} detections`);
        };

        if (map.isStyleLoaded()) addLayers();
        else map.once("load", addLayers);

    }, [geojson]);

    return (
        <div style={{ position: "relative", width: "100%", height: "100%" }}>
            <div ref={mapRef} style={{ width: "100%", height: "100%" }} />

            {/* Legend */}
            <div style={{
                position: "absolute", bottom: 30, right: 10,
                background: "rgba(30,30,46,0.9)",
                borderRadius: 8, padding: "8px 12px",
                color: "#cdd6f4", fontSize: 11,
                fontFamily: "monospace",
            }}>
                {CLASS_NAMES.map((name, i) => (
                    <div key={i} style={{
                        display: "flex", alignItems: "center", gap: 6,
                        marginBottom: 3,
                    }}>
                        <div style={{
                            width: 12, height: 12, borderRadius: 2,
                            background: CLASS_COLORS[i],
                        }} />
                        {name}
                    </div>
                ))}
            </div>
        </div>
    );
}