# ─── Stage 1: Build ───────────────────────────────────────────
FROM osgeo/gdal:ubuntu-small-3.6.3 AS builder

RUN apt-get update && apt-get install -y \
    cmake g++ libpqxx-dev libpq-dev pkg-config wget ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Cài ONNX Runtime
RUN wget -q https://github.com/microsoft/onnxruntime/releases/download/v1.16.3/onnxruntime-linux-x64-1.16.3.tgz \
    && tar -xzf onnxruntime-linux-x64-1.16.3.tgz \
    && cp -r onnxruntime-linux-x64-1.16.3/include/* /usr/local/include/ \
    && cp -r onnxruntime-linux-x64-1.16.3/lib/*     /usr/local/lib/ \
    && ldconfig \
    && rm -rf onnxruntime-linux-x64-1.16.3*

WORKDIR /app
COPY . .

RUN cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build --parallel 4

# ─── Stage 2: Runtime ─────────────────────────────────────────
FROM osgeo/gdal:ubuntu-small-3.6.3

RUN apt-get update && apt-get install -y \
    libpqxx-6.4 libpq5 libgomp1 \
    && rm -rf /var/lib/apt/lists/*

# Copy ONNX Runtime .so từ builder
COPY --from=builder /usr/local/lib/libonnxruntime* /usr/local/lib/
RUN ldconfig

WORKDIR /app
COPY --from=builder /app/build/rs_pipeline .
COPY --from=builder /app/models /app/models

EXPOSE 8080
EXPOSE 9090/udp

CMD ["./rs_pipeline", "/app/data/test.tif"]