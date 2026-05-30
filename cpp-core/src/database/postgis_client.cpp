#include "database/postgis_client.hpp"
#include "common/logger.hpp"
#include <sstream>
#include <iomanip>

namespace rs
{

    PostGISClient::PostGISClient(const std::string &conn_str)
        : conn_str_(conn_str)
    {
        try
        {
            conn_ = std::make_unique<pqxx::connection>(conn_str_);
            if (conn_->is_open())
                LOG_INFO("PostGISClient",
                         "Connected to PostgreSQL: " + std::string(conn_->dbname()));
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("Connection failed: ") + e.what());
        }
    }

    PostGISClient::~PostGISClient()
    {
        if (conn_ && conn_->is_open())
        {
            conn_->disconnect();
            LOG_DEBUG("PostGISClient", "Disconnected.");
        }
    }

    bool PostGISClient::isConnected() const
    {
        return conn_ && conn_->is_open();
    }

    bool PostGISClient::reconnect()
    {
        try
        {
            conn_ = std::make_unique<pqxx::connection>(conn_str_);
            return conn_->is_open();
        }
        catch (...)
        {
            return false;
        }
    }

    std::string PostGISClient::toWKT(const std::vector<GeoPoint> &polygon)
    {
        if (polygon.size() < 4)
            return "";

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(8);
        oss << "POLYGON((";
        for (size_t i = 0; i < polygon.size(); i++)
        {
            if (i > 0)
                oss << ",";
            oss << polygon[i].lon << " " << polygon[i].lat;
        }
        // Đóng vòng polygon
        oss << "," << polygon[0].lon << " " << polygon[0].lat;
        oss << "))";
        return oss.str();
    }

    int64_t PostGISClient::createSession(const std::string &filename,
                                         int tile_total)
    {
        if (!isConnected() && !reconnect())
            return -1;
        try
        {
            pqxx::work txn(*conn_);
            auto r = txn.exec_params(
                "INSERT INTO sessions(filename, status, tile_total) "
                "VALUES($1, $2, $3) RETURNING id",
                filename, "LOADING", tile_total);
            txn.commit();
            int64_t id = r[0][0].as<int64_t>();
            LOG_INFO("PostGISClient",
                     "Created session " + std::to_string(id));
            return id;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("createSession failed: ") + e.what());
            return -1;
        }
    }

    bool PostGISClient::updateSessionStatus(int64_t session_id,
                                            SessionStatus status)
    {
        if (!isConnected() && !reconnect())
            return false;
        static const char *names[] = {
            "IDLE", "LOADING", "TILING", "PROCESSING",
            "STITCHING", "SAVING", "DONE", "ERROR", "RECOVERING"};
        try
        {
            pqxx::work txn(*conn_);

            txn.exec_params(
                "UPDATE sessions SET status=$1, updated_at=NOW() WHERE id=$2",
                names[(int)status], session_id);
            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("updateSessionStatus failed: ") + e.what());
            return false;
        }
    }

    bool PostGISClient::updateSessionProgress(int64_t session_id,
                                              int tile_done)
    {
        if (!isConnected() && !reconnect())
            return false;
        try
        {
            pqxx::work txn(*conn_);
            txn.exec_params(
                "UPDATE sessions SET tile_done=$1, updated_at=NOW() WHERE id=$2",
                tile_done, session_id);
            txn.commit();
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("updateSessionProgress failed: ") + e.what());
            return false;
        }
    }

    bool PostGISClient::insertDetections(
        const std::vector<GeoDetection> &detections)
    {
        if (detections.empty())
            return true;
        if (!isConnected() && !reconnect())
            return false;
        try
        {
            pqxx::work txn(*conn_);
            for (const auto &det : detections)
            {
                std::string wkt = toWKT(det.polygon);
                if (wkt.empty())
                    continue;
                // INSERT: dùng WKT (Well-Known Text) — đây là format trung gian
                // "POLYGON((-78.627 41.121, -78.625 41.121, ...))"
                txn.exec_params(
                    "INSERT INTO detections(session_id, geom, class_id, confidence) "
                    "VALUES($1, ST_GeomFromText($2, 4326), $3, $4)",
                    det.session_id, wkt, det.class_id, det.confidence);
            }
            txn.commit();
            LOG_DEBUG("PostGISClient",
                      "Inserted " + std::to_string(detections.size()) + " detections.");
            return true;
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("insertDetections failed: ") + e.what());
            return false;
        }
    }

    std::string PostGISClient::queryDetectionsGeoJSON(int64_t session_id)
    {
        if (!isConnected() && !reconnect())
            return "{}";
        try
        {
            pqxx::nontransaction ntxn(*conn_);
            auto r = ntxn.exec_params(
                "SELECT json_build_object("
                "  'type', 'FeatureCollection',"
                "  'features', COALESCE(json_agg("
                "    json_build_object("
                "      'type', 'Feature',"
                "      'geometry', ST_AsGeoJSON(geom)::json,"
                "      'properties', json_build_object("
                "        'id', id,"
                "        'class_id', class_id,"
                "        'confidence', confidence,"
                "        'session_id', session_id"
                "      )"
                "    )"
                "  ), '[]'::json)"
                ") FROM detections WHERE session_id = $1",
                session_id);
            if (r.empty() || r[0][0].is_null())
                return "{}";
            return r[0][0].as<std::string>();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("queryDetectionsGeoJSON failed: ") + e.what());
            return "{}";
        }
    }

} // namespace rs