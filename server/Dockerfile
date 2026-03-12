# Build stage
FROM ubuntu:22.04 AS builder

RUN apt-get update && apt-get install -y \
    cmake \
    g++ \
    make \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

COPY . .

RUN mkdir -p build && cd build && \
    cmake .. && \
    cmake --build .

# Runtime stage
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /build/build/cig-nexus-server /app/cig-nexus-server

EXPOSE 4242

CMD ["/app/cig-nexus-server", "--port", "4242"]
