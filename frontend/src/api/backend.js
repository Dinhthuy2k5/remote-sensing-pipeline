const BASE = "http://localhost:8080";

async function jsonOrThrow(response) {
    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
        throw new Error(data.error || data.message || `HTTP ${response.status}`);
    }
    return data;
}

export async function uploadFile(file, onProgress) {
    // Streaming upload — raw binary
    return fetch(`${BASE}/upload`, {
        method: "POST",
        headers: {
            "Content-Type": "application/octet-stream",
            "X-Filename": file.name,
        },
        body: file,
    }).then(jsonOrThrow);
}

export async function setConfig(sessionId, config) {
    return fetch(`${BASE}/sessions/${sessionId}/config`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(config),
    }).then(jsonOrThrow);
}

export async function startSession(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/start`, {
        method: "POST",
    }).then(jsonOrThrow);
}

export async function cancelSession(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/cancel`, {
        method: "POST",
    }).then(jsonOrThrow);
}

export async function getStatus(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/status`)
        .then(jsonOrThrow);
}

export async function getResults(sessionId) {
    return fetch(`${BASE}/sessions/${sessionId}/results`)
        .then(jsonOrThrow);
}

export async function healthCheck() {
    return fetch(`${BASE}/health`).then(jsonOrThrow);
}
