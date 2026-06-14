const PALETTE = [
    "#22c55e", "#3b82f6", "#f97316", "#a855f7", "#ef4444",
    "#14b8a6", "#eab308", "#ec4899", "#06b6d4", "#84cc16",
    "#f43f5e", "#6366f1", "#f59e0b", "#10b981", "#8b5cf6",
];

export const MODEL_REGISTRY = {
    mock: {
        label: "MockAI remote sensing",
        names: ["Vegetation", "Water", "Building", "Road"],
    },
    onnx: {
        label: "YOLOv8n-seg COCO",
        names: [
            "Person", "Bicycle", "Car", "Motorcycle", "Airplane",
            "Bus", "Train", "Truck", "Boat", "Traffic Light",
            "Fire Hydrant", "Stop Sign", "Parking Meter", "Bench", "Bird",
            "Cat", "Dog", "Horse", "Sheep", "Cow",
            "Elephant", "Bear", "Zebra", "Giraffe", "Backpack",
            "Umbrella", "Handbag", "Tie", "Suitcase", "Frisbee",
            "Skis", "Snowboard", "Sports Ball", "Kite", "Baseball Bat",
            "Baseball Glove", "Skateboard", "Surfboard", "Tennis Racket", "Bottle",
            "Wine Glass", "Cup", "Fork", "Knife", "Spoon",
            "Bowl", "Banana", "Apple", "Sandwich", "Orange",
            "Broccoli", "Carrot", "Hot Dog", "Pizza", "Donut",
            "Cake", "Chair", "Couch", "Potted Plant", "Bed",
            "Dining Table", "Toilet", "TV", "Laptop", "Mouse",
            "Remote", "Keyboard", "Cell Phone", "Microwave", "Oven",
            "Toaster", "Sink", "Refrigerator", "Book", "Clock",
            "Vase", "Scissors", "Teddy Bear", "Hair Drier", "Toothbrush",
        ],
    },
    dota_obb: {
        label: "DOTA aerial OBB",
        names: [
            "Plane", "Baseball Diamond", "Bridge", "Ground Track Field",
            "Small Vehicle", "Large Vehicle", "Ship", "Tennis Court",
            "Basketball Court", "Storage Tank", "Soccer Ball Field",
            "Roundabout", "Harbor", "Swimming Pool", "Helicopter",
        ],
    },
    segformer_loveda: {
        label: "SegFormer LoveDA",
        names: [
            "Ignore", "Background", "Building", "Road",
            "Water", "Barren", "Forest", "Agricultural",
        ],
    },
};

export function modelInfo(modelKey) {
    return MODEL_REGISTRY[modelKey] ?? MODEL_REGISTRY.mock;
}

export function className(modelKey, classId) {
    const info = modelInfo(modelKey);
    return info.names[classId] ?? `Class ${classId}`;
}

export function classColor(classId) {
    return PALETTE[Math.abs(Number(classId) || 0) % PALETTE.length];
}

export function classStats(geojson, modelKey) {
    const counts = new Map();
    for (const feature of geojson?.features ?? []) {
        const classId = feature.properties?.class_id ?? 0;
        const entry = counts.get(classId) ?? {
            classId,
            name: className(modelKey, classId),
            color: classColor(classId),
            count: 0,
        };
        entry.count += 1;
        counts.set(classId, entry);
    }
    return [...counts.values()].sort((a, b) => b.count - a.count);
}
