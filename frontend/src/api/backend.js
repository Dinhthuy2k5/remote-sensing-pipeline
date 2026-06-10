const BASE = "http://localhost:8080";

export async function uploadFile(file, onProgress) {
    // Streaming upload — raw binary
    return fetch(`${BASE}/upload`, {
        method: "POST",
        headers: {
            "Content-Type": "application/octet-stream",
            "X-Filename": file.name,
        },
        body: file,
    }).then(r => r.json());
}

export async function setConfig(sessionId, config) {
    return fetch(`${BASE}/sessions/${sessionId}/config`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
    }).then(r => r.json());
}

export async function startSession(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/start`, {
        method: "POST",
    }).then(r => r.json());
}

export async function cancelSession(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/cancel`, {
        method: "POST",
    }).then(r => r.json());
}

export async function getStatus(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/status`)
        .then(r => r.json());
}

export async function getResults(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/results`)
        .then(r => r.json());
}

export async function healthCheck() {
    return fetch(`${BASE}/health`).then(r => r.json());
}