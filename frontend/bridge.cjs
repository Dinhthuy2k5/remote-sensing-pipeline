// Chạy trên host: node frontend/bridge.js
// Nhận UDP từ C++ container, relay sang WebSocket cho browser

const dgram = require("dgram");
const WebSocket = require("ws");

const UDP_PORT = 9090;
const WS_PORT = 9091;

// WebSocket server
const wss = new WebSocket.Server({ port: WS_PORT });
console.log(`[Bridge] WebSocket server listening on ws://localhost:${WS_PORT}`);

// UDP socket
const udp = dgram.createSocket("udp4");

udp.on("listening", () => {
    const addr = udp.address();
    udp.setBroadcast(true);
    console.log(`[Bridge] UDP listening on ${addr.address}:${addr.port}`);
});

udp.on("message", (msg) => {
    const payload = msg.toString("utf8");

    // Relay tới tất cả WebSocket clients đang kết nối
    let sent = 0;
    wss.clients.forEach((client) => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(payload);
            sent++;
        }
    });

    if (sent > 0) {
        // Log gọn để không spam
        try {
            const m = JSON.parse(payload);
            process.stdout.write(
                `\r[Bridge] → ${sent} clients | `
                + `state=${m.state} `
                + `cpu=${m.cpu_percent?.toFixed(1)}% `
                + `ram=${m.ram_used_mb}MB `
                + `tiles=${m.tiles_done}/${m.tiles_total}`
            );
        } catch (_) { }
    }
});

udp.on("error", (err) => {
    console.error("[Bridge] UDP error:", err.message);
});

udp.bind(UDP_PORT);

wss.on("connection", (ws) => {
    console.log("\n[Bridge] Browser connected");
    ws.on("close", () => console.log("[Bridge] Browser disconnected"));
});

process.on("SIGINT", () => {
    console.log("\n[Bridge] Shutting down...");
    udp.close();
    wss.close();
    process.exit(0);
});