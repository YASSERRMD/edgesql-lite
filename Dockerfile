# EdgeSQL Lite Docker Image
# Multi-stage build for minimal image size

# Build stage
FROM debian:bookworm-slim AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

# Copy source code
WORKDIR /build
COPY . .

# Build
RUN mkdir -p build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON && \
    make -j$(nproc)

# Runtime stage
FROM debian:bookworm-slim

# Install runtime dependencies (minimal)
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && rm -rf /var/lib/apt/lists/*

# Create non-root user
RUN useradd -m -s /bin/bash edgesql

# Copy binary
COPY --from=builder /build/build/edgesql-lite /usr/local/bin/edgesql-lite

# Create data directory
RUN mkdir -p /var/lib/edgesql && chown edgesql:edgesql /var/lib/edgesql

# Switch to non-root user
USER edgesql
WORKDIR /var/lib/edgesql

# Expose HTTP port
EXPOSE 8080

# Health check
HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://localhost:8080/health || exit 1

# Default command
ENTRYPOINT ["edgesql-lite"]
CMD ["--port", "8080", "--data-dir", "/var/lib/edgesql"]
