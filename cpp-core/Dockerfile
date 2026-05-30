# ─── Stage 1: Build ───────────────────────────────────────────
FROM osgeo/gdal:ubuntu-small-3.6.3 AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    libpqxx-dev \
    libpq-dev \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel 4

# ─── Stage 2: Runtime ─────────────────────────────────────────
FROM osgeo/gdal:ubuntu-small-3.6.3

RUN apt-get update && apt-get install -y \
    libpqxx-6.4 \
    libpq5 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/rs_pipeline .

EXPOSE 8080
EXPOSE 9090/udp

CMD ["./rs_pipeline", "/app/data/test.tif"]