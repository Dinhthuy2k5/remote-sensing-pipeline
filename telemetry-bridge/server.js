import express from "express";
import { WebSocketServer } from "ws";
import http from "http";

const app = express();

app.use(express.json({ limit: "64kb" }));

const server = http.createServer(app);

const PORT = Number(process.env.PORT || 10000);

const wss = new WebSocketServer({
    server,
    path: "/ws",
});

app.get("/", (req, res) => {
    res.json({
        service: "remote-sensing-telemetry",
        status: "ok",
    });
});

app.get("/health", (req, res) => {
    res.json({
        status: "ok",
        clients: wss.clients.size,
    });
});

app.post("/telemetry", (req, res) => {
    const payload = JSON.stringify(req.body);

    let sent = 0;

    wss.clients.forEach((client) => {
        if (client.readyState === 1) {
            client.send(payload);
            sent++;
        }
    });

    console.log(
        `[Telemetry] broadcast=${sent} clients | ${payload}`
    );

    res.status(202).json({
        ok: true,
        clients: sent,
    });
});

wss.on("connection", (ws) => {
    console.log("[Telemetry] Browser connected");

    ws.on("close", () => {
        console.log("[Telemetry] Browser disconnected");
    });
});

server.listen(PORT, "0.0.0.0", () => {
    console.log(
        `[Telemetry] HTTP/WebSocket server listening on 0.0.0.0:${PORT}`
    );
});