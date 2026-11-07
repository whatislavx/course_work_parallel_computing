# Збирання і запуск сервера пошуку (NFR-012, TCK-004).
FROM ubuntu:24.04 AS build
RUN apt-get update \
    && apt-get install -y --no-install-recommends build-essential cmake ninja-build libboost-dev libboost-json-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY CMakeLists.txt ./
COPY src ./src
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSEARCH_BUILD_TESTS=OFF \
    && cmake --build build --target search_server

FROM ubuntu:24.04
RUN apt-get update \
    && apt-get install -y --no-install-recommends libboost-json1.83.0 curl \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /src/build/src/search_server /usr/local/bin/search_server
RUN useradd --system --no-create-home --uid 10001 search
USER search
EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=3s --start-period=5s --retries=5 \
    CMD curl -fsS http://127.0.0.1:8080/api/health || exit 1
ENTRYPOINT ["/usr/local/bin/search_server"]
