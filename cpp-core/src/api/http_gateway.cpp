#define CPPHTTPLIB_OPENSSL_SUPPORT 0
#include "api/http_gateway.hpp"
#include "httplib_wrapper.hpp" // ← thay cho httplib.h trực tiếp
#include "nlohmann/json.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <ctime>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace rs
{

    HttpGateway::HttpGateway(int port)
        : port_(port), svr_(std::make_unique<httplib::Server>())
    {
        setupRoutes();
    }

    HttpGateway::~HttpGateway() { stop(); }

    std::string HttpGateway::sessionStoragePath()
    {
        const char *p = std::getenv("SESSION_STORAGE_PATH");
        return p ? std::string(p) : "/tmp/sessions";
    }

    // ─── parseConfig — dùng nlohmann/json, an toàn với mọi format ─
    PipelineConfig HttpGateway::parseConfig(const std::string &body)
    {
        PipelineConfig cfg;
        if (body.empty())
            return cfg;
        try
        {
            auto j = json::parse(body);
            cfg.tile_size = j.value("tile_size", cfg.tile_size);
            cfg.overlap = j.value("overlap", cfg.overlap);
            cfg.model = j.value("model", cfg.model);
            cfg.max_workers = j.value("max_workers", cfg.max_workers);
            cfg.conf_thresh = j.value("conf_thresh", cfg.conf_thresh);
        }
        catch (const json::exception &e)
        {
            LOG_WARN("HttpGateway",
                     std::string("parseConfig JSON error: ") + e.what() + " — using defaults.");
        }
        return cfg;
    }

    // ─── sessionInfoToJson ────────────────────────────────────────
    std::string HttpGateway::sessionInfoToJson(const SessionInfo &info)
    {
        static const char *state_names[] = {
            "IDLE", "LOADING", "TILING", "PROCESSING",
            "STITCHING", "SAVING", "DONE", "ERROR", "RECOVERING"};
        int progress = info.tile_total > 0
                           ? (info.tile_done * 100 / info.tile_total)
                           : 0;

        json j = {
            {"session_id", info.id},
            {"filename", info.filename},
            {"status", state_names[(int)info.status]},
            {"tile_total", info.tile_total},
            {"tile_done", info.tile_done},
            {"progress", progress}};
        return j.dump();
    }

    // ─── setupRoutes ──────────────────────────────────────────────
    void HttpGateway::setupRoutes()
    {

        // CORS
        svr_->set_pre_routing_handler(
            [](const httplib::Request &, httplib::Response &res)
            {
                res.set_header("Access-Control-Allow-Origin", "*");
                res.set_header("Access-Control-Allow-Methods",
                               "GET, POST, OPTIONS");
                res.set_header("Access-Control-Allow-Headers",
                               "Content-Type");
                return httplib::Server::HandlerResponse::Unhandled;
            });

        svr_->Options(".*", [](const httplib::Request &,
                               httplib::Response &res)
                      { res.status = 204; });

        // ── GET /health ───────────────────────────────────────────
        svr_->Get("/health", [](const httplib::Request &,
                                httplib::Response &res)
                  {
        json j = {{"status", "ok"}, {"service", "rs-pipeline"}};
        res.set_content(j.dump(), "application/json"); });

        // ── POST /upload — STREAMING, không load file vào RAM ─────
        // Client gửi raw binary body (Content-Type: application/octet-stream)
        // Header X-Filename: tên file gốc (optional)
        svr_->Post("/upload",
                   [this](const httplib::Request &req,
                          httplib::Response &res,
                          const httplib::ContentReader &content_reader)
                   {
                       // ── Bước 1: Xin ID từ hệ thống TRƯỚC khi tạo thư mục ──
                       int64_t session_id = upload_init_cb_
                                                ? upload_init_cb_()
                                                : (int64_t)std::time(nullptr);

                       // ── Bước 2: Tạo thư mục bằng đúng ID chuẩn ───────────
                       std::string session_dir = sessionStoragePath() + "/" + std::to_string(session_id);

                       try
                       {
                           fs::create_directories(session_dir);
                       }
                       catch (...)
                       {
                           res.status = 500;
                           res.set_content(
                               json{{"error", "Cannot create session dir"}}.dump(),
                               "application/json");
                           return false;
                       }

                       std::string filepath = session_dir + "/input.tif";
                       std::ofstream ofs(filepath, std::ios::binary);
                       if (!ofs)
                       {
                           res.status = 500;
                           res.set_content(
                               json{{"error", "Cannot write file to disk"}}.dump(),
                               "application/json");
                           return false;
                       }

                       // ── Bước 3: Stream file thẳng xuống disk ──────────────
                       size_t total_bytes = 0;
                       content_reader([&](const char *data, size_t len)
                                      {
            ofs.write(data, (std::streamsize)len);
            total_bytes += len;
            return true; });
                       ofs.close();

                       LOG_INFO("HttpGateway",
                                "Streamed to disk: " + filepath + " (" + std::to_string(total_bytes) + " bytes)" + " session_id=" + std::to_string(session_id));

                       // ── Bước 4: Báo pipeline file đã sẵn sàng ─────────────
                       std::string filename = "input.tif";
                       if (req.has_header("X-Filename"))
                           filename = req.get_header_value("X-Filename");

                       if (upload_complete_cb_)
                           upload_complete_cb_(session_id, filepath, filename, total_bytes);

                       json resp = {
                           {"status", "uploaded"},
                           {"session_id", session_id},
                           {"filename", filename},
                           {"size", total_bytes}};
                       res.set_content(resp.dump(), "application/json");
                       return true;
                   });

        // ── POST /sessions/:id/config ─────────────────────────────
        svr_->Post(R"(/sessions/(\d+)/config)",
                   [this](const httplib::Request &req, httplib::Response &res)
                   {
                       int64_t sid = std::stoll(req.matches[1]);
                       PipelineConfig cfg = parseConfig(req.body);
                       bool ok = config_cb_ ? config_cb_(sid, cfg) : true;

                       if (!ok)
                       {
                           res.status = 404;
                           res.set_content(
                               json{{"error", "Session not found"}}.dump(),
                               "application/json");
                           return;
                       }

                       json resp = {
                           {"status", "ok"},
                           {"session_id", sid},
                           {"tile_size", cfg.tile_size},
                           {"overlap", cfg.overlap},
                           {"model", cfg.model},
                           {"conf_thresh", cfg.conf_thresh}};
                       res.set_content(resp.dump(), "application/json");
                       LOG_INFO("HttpGateway",
                                "Config set for session " + std::to_string(sid) + ": tile_size=" + std::to_string(cfg.tile_size) + " model=" + cfg.model);
                   });

        // ── POST /sessions/:id/start ──────────────────────────────
        svr_->Post(R"(/sessions/(\d+)/start)",
                   [this](const httplib::Request &req, httplib::Response &res)
                   {
                       int64_t sid = std::stoll(req.matches[1]);

                       // Config inline trong body (optional)
                       if (!req.body.empty() && config_cb_)
                           config_cb_(sid, parseConfig(req.body));

                       bool ok = start_cb_ ? start_cb_(sid) : false;
                       if (!ok)
                       {
                           res.status = 409;
                           res.set_content(
                               json{{"error", "Cannot start. Check session state."}}.dump(),
                               "application/json");
                           return;
                       }

                       res.status = 202;
                       json resp = {
                           {"status", "accepted"},
                           {"session_id", sid},
                           {"message", "Pipeline started. Poll /status for progress."}};
                       res.set_content(resp.dump(), "application/json");
                   });

        // ── POST /sessions/:id/cancel ─────────────────────────────
        svr_->Post(R"(/sessions/(\d+)/cancel)",
                   [this](const httplib::Request &req, httplib::Response &res)
                   {
                       int64_t sid = std::stoll(req.matches[1]);
                       bool ok = cancel_cb_ ? cancel_cb_(sid) : false;

                       if (!ok)
                       {
                           res.status = 409;
                           res.set_content(
                               json{{"error",
                                     "Cannot cancel session " + std::to_string(sid)}}
                                   .dump(),
                               "application/json");
                           return;
                       }

                       json resp = {{"status", "cancelled"}, {"session_id", sid}};
                       res.set_content(resp.dump(), "application/json");
                       LOG_INFO("HttpGateway",
                                "Session " + std::to_string(sid) + " cancelled.");
                   });

        // ── GET /sessions/:id/status ──────────────────────────────
        svr_->Get(R"(/sessions/(\d+)/status)",
                  [this](const httplib::Request &req, httplib::Response &res)
                  {
                      int64_t sid = std::stoll(req.matches[1]);
                      if (status_cb_)
                          res.set_content(sessionInfoToJson(status_cb_(sid)),
                                          "application/json");
                      else
                          res.set_content(
                              json{{"session_id", sid}, {"status", "UNKNOWN"}}.dump(),
                              "application/json");
                  });

        // ── GET /sessions/:id/results ─────────────────────────────
        svr_->Get(R"(/sessions/(\d+)/results)",
                  [this](const httplib::Request &req, httplib::Response &res)
                  {
                      int64_t sid = std::stoll(req.matches[1]);
                      std::string geojson = results_cb_
                                                ? results_cb_(sid)
                                                : "{}";
                      res.set_content(geojson, "application/json");
                  });

        LOG_INFO("HttpGateway", "All routes configured.");
    }

    void HttpGateway::start()
    {
        running_ = true;
        LOG_INFO("HttpGateway",
                 "Listening on 0.0.0.0:" + std::to_string(port_));
        svr_->listen("0.0.0.0", port_);
        running_ = false;
    }

    void HttpGateway::stop()
    {
        if (svr_)
            svr_->stop();
        running_ = false;
    }

} // namespace rs