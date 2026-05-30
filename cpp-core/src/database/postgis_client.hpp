#pragma once
#include "common/types.hpp"
#include <pqxx/pqxx>
#include <string>
#include <vector>
#include <memory>

namespace rs
{

    class PostGISClient
    {
    public:
        explicit PostGISClient(const std::string &conn_str);
        ~PostGISClient();

        PostGISClient(const PostGISClient &) = delete;
        PostGISClient &operator=(const PostGISClient &) = delete;

        // Thêm sau dòng operator= delete
        PostGISClient(PostGISClient &&) = default;
        PostGISClient &operator=(PostGISClient &&) = default;

        bool isConnected() const;

        // Session management
        int64_t createSession(const std::string &filename, int tile_total);
        bool updateSessionStatus(int64_t session_id, SessionStatus status);
        bool updateSessionProgress(int64_t session_id, int tile_done);

        // Insert detections — batch insert cho hiệu suất
        bool insertDetections(const std::vector<GeoDetection> &detections);

        // Query — trả GeoJSON string
        std::string queryDetectionsGeoJSON(int64_t session_id);

    private:
        std::string conn_str_;
        std::unique_ptr<pqxx::connection> conn_;

        // Build WKT polygon từ 4 GeoPoint
        // "POLYGON((lon1 lat1, lon2 lat2, lon3 lat3, lon4 lat4, lon1 lat1))"
        static std::string toWKT(const std::vector<GeoPoint> &polygon);
        bool reconnect();
    };

} // namespace rs