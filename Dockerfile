# ---- build stage -----------------------------------------------------------
# Same base image as the runtime stage, so the binary links against exactly the
# libstdc++ it will run with.
FROM ubuntu:24.04 AS build

RUN apt-get update \
 && apt-get install -y --no-install-recommends g++ cmake make \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src

# Tests are skipped here: they run in CI, and fetching GoogleTest would make
# every image build depend on network access to GitHub.
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
 && cmake --build build -j"$(nproc)"

# ---- runtime stage ---------------------------------------------------------
FROM ubuntu:24.04

RUN useradd --system --no-create-home --shell /usr/sbin/nologin server

# The server resolves static/ and logs/ relative to its executable's parent
# directory, so the binary lives in /app/bin and those sit beside it in /app.
WORKDIR /app
COPY --from=build /src/build/multithreaded-server /app/bin/multithreaded-server
COPY static /app/static
RUN mkdir -p /app/logs && chown server:server /app/logs

USER server

# Bind all interfaces: 127.0.0.1 inside a container is unreachable from outside.
# Hosts such as Render inject PORT at runtime, which overrides this default.
ENV SERVER_IP=0.0.0.0 \
    PORT=8080 \
    LOOP_MODE=reactor \
    MAX_THREADS=2 \
    CACHE_CAPACITY=64 \
    IDLE_TIMEOUT=15

EXPOSE 8080
CMD ["/app/bin/multithreaded-server"]
