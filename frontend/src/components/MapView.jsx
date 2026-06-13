import { useEffect, useRef } from "react";
import maplibregl from "maplibre-gl";
import "maplibre-gl/dist/maplibre-gl.css";
import { classColor, classStats, modelInfo } from "../models/modelRegistry";

export default function MapView({ geojson, modelKey = "mock" }) {
    const mapRef = useRef(null);
    const mapInst = useRef(null);
    const sourceIds = useRef([]);
    const stats = classStats(geojson, modelKey);

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
            center: [-78.56, 41.05],
            zoom: 10,
        });

        mapInst.current.addControl(new maplibregl.NavigationControl());

        return () => mapInst.current?.remove();
    }, []);

    useEffect(() => {
        const map = mapInst.current;
        if (!map || !geojson?.features?.length) return;

        const addLayers = () => {
            sourceIds.current.forEach(sourceId => {
                const outlineId = `${sourceId}-outline`;
                if (map.getLayer(outlineId)) map.removeLayer(outlineId);
                if (map.getLayer(sourceId)) map.removeLayer(sourceId);
                if (map.getSource(sourceId)) map.removeSource(sourceId);
            });
            sourceIds.current = [];

            const byClass = new Map();
            geojson.features.forEach(feature => {
                const classId = feature.properties?.class_id ?? 0;
                const features = byClass.get(classId) ?? [];
                features.push(feature);
                byClass.set(classId, features);
            });

            byClass.forEach((features, classId) => {
                const sourceId = `detections-${classId}`;
                sourceIds.current.push(sourceId);

                map.addSource(sourceId, {
                    type: "geojson",
                    data: { type: "FeatureCollection", features },
                });

                map.addLayer({
                    id: sourceId,
                    type: "fill",
                    source: sourceId,
                    paint: {
                        "fill-color": classColor(classId),
                        "fill-opacity": 0.35,
                    },
                });

                map.addLayer({
                    id: `${sourceId}-outline`,
                    type: "line",
                    source: sourceId,
                    paint: {
                        "line-color": classColor(classId),
                        "line-width": 1.5,
                        "line-opacity": 0.8,
                    },
                });
            });

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
    }, [geojson, modelKey]);

    return (
        <div style={{ position: "relative", width: "100%", height: "100%" }}>
            <div ref={mapRef} style={{ width: "100%", height: "100%" }} />

            <div style={{
                position: "absolute", bottom: 30, right: 10,
                background: "rgba(30,30,46,0.9)",
                borderRadius: 8, padding: "8px 12px",
                color: "#cdd6f4", fontSize: 11,
                fontFamily: "monospace", minWidth: 170,
            }}>
                <div style={{
                    color: "#a6adc8",
                    marginBottom: 6,
                    borderBottom: "1px solid #45475a",
                    paddingBottom: 4,
                }}>
                    {modelInfo(modelKey).label}
                </div>

                {stats.length > 0 ? stats.slice(0, 8).map(item => (
                    <div key={item.classId} style={{
                        display: "flex", alignItems: "center", gap: 6,
                        marginBottom: 3,
                    }}>
                        <div style={{
                            width: 12, height: 12, borderRadius: 2,
                            background: item.color,
                            flexShrink: 0,
                        }} />
                        <span style={{ minWidth: 0 }}>{item.name}</span>
                        <span style={{ marginLeft: "auto", color: "#a6adc8" }}>
                            {item.count}
                        </span>
                    </div>
                )) : (
                    <div style={{ color: "#a6adc8" }}>No detections</div>
                )}
            </div>
        </div>
    );
}
