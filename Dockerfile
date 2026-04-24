FROM ubuntu:22.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    g++ \
    cmake \
    make \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j

RUN set -eux; \
    mkdir -p /app/runtime/lib; \
    cp /app/build/server /app/runtime/server; \
    find /app/build -type f -name "libThreadPool.so*" -exec cp -d {} /app/runtime/lib/ \;; \
    test -n "$(find /app/runtime/lib -maxdepth 1 -type f -name 'libThreadPool.so*' -print -quit)"; \
    LD_LIBRARY_PATH=/app/runtime/lib ldd /app/runtime/server | grep -q "libThreadPool"

FROM ubuntu:22.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /app/runtime/server /app/server
COPY --from=build /app/runtime/lib /app/lib

ENV LD_LIBRARY_PATH=/app/lib

EXPOSE 8080
CMD ["/app/server"]
