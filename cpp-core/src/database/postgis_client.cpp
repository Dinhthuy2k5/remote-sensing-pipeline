#include "database/postgis_client.hpp"
#include "common/logger.hpp"
#include "nlohmann/json.hpp"
#include <sstream>
#include <iomanip>

namespace rs
{
    namespace
    {
        const char *EMPTY_FEATURE_COLLECTION = "{\"type\":\"FeatureCollection\",\"features\":[]}";
    }

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

    std::string PostGISClient::queryDetectionsGeoJSON(
        int64_t session_id,
        const std::vector<GeoPoint> &footprint)
    {
        if (!isConnected() && !reconnect())
            return EMPTY_FEATURE_COLLECTION;
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
                return EMPTY_FEATURE_COLLECTION;

            auto result = nlohmann::json::parse(r[0][0].as<std::string>());
            std::string footprint_wkt = toWKT(footprint);
            if (footprint_wkt.empty())
                return result.dump();

            // Assign cross-class overlap to the class with the stronger maximum
            // confidence so the class coverages form a disjoint partition.
            auto coverage = ntxn.exec_params(
                "WITH footprint AS ("
                "  SELECT ST_MakeValid(ST_GeomFromText($2, 4326)) AS geom"
                "), clipped AS ("
                "  SELECT d.class_id, d.confidence,"
                "         ST_Intersection(ST_MakeValid(d.geom), f.geom) AS geom"
                "  FROM detections d CROSS JOIN footprint f"
                "  WHERE d.session_id = $1 AND ST_Intersects(d.geom, f.geom)"
                "), class_unions AS ("
                "  SELECT class_id, MAX(confidence) AS priority,"
                "         ST_UnaryUnion(ST_Collect(geom)) AS geom"
                "  FROM clipped WHERE NOT ST_IsEmpty(geom) GROUP BY class_id"
                "), disjoint_classes AS ("
                "  SELECT c.class_id, ST_Difference("
                "    c.geom, COALESCE(("
                "      SELECT ST_UnaryUnion(ST_Collect(h.geom))"
                "      FROM class_unions h"
                "      WHERE h.priority > c.priority"
                "         OR (h.priority = c.priority AND h.class_id < c.class_id)"
                "    ), ST_GeomFromText('POLYGON EMPTY', 4326))"
                "  ) AS geom"
                "  FROM class_unions c"
                "), class_areas AS ("
                "  SELECT class_id, ST_Area("
                "    ST_CollectionExtract(ST_MakeValid(geom), 3)::geography"
                "  ) AS area_m2"
                "  FROM disjoint_classes WHERE NOT ST_IsEmpty(geom)"
                "), footprint_area AS ("
                "  SELECT ST_Area(geom::geography) AS area_m2 FROM footprint"
                ")"
                "SELECT json_build_object("
                "  'footprint_area_m2', fa.area_m2,"
                "  'classified_area_m2', COALESCE((SELECT SUM(area_m2) FROM class_areas), 0),"
                "  'unclassified_percent', GREATEST(0, 100 - COALESCE(("
                "    SELECT SUM(area_m2) * 100.0 / NULLIF(fa.area_m2, 0) FROM class_areas"
                "  ), 0)),"
                "  'classes', COALESCE(("
                "    SELECT json_agg(json_build_object("
                "      'class_id', class_id,"
                "      'area_m2', area_m2,"
                "      'percent', area_m2 * 100.0 / NULLIF(fa.area_m2, 0)"
                "    ) ORDER BY area_m2 DESC) FROM class_areas"
                "  ), '[]'::json)"
                ") FROM footprint_area fa",
                session_id, footprint_wkt);

            if (!coverage.empty() && !coverage[0][0].is_null())
                result["coverage"] = nlohmann::json::parse(
                    coverage[0][0].as<std::string>());

            return result.dump();
        }
        catch (const std::exception &e)
        {
            LOG_ERROR("PostGISClient",
                      std::string("queryDetectionsGeoJSON failed: ") + e.what());
            return EMPTY_FEATURE_COLLECTION;
        }
    }

} // namespace rs
