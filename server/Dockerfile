# Static musl binary, cross-compiled on the build host for each target platform
# (no emulation), in an empty image.
FROM --platform=$BUILDPLATFORM rust:1-alpine AS build
ARG TARGETARCH
RUN apk add --no-cache musl-dev \
 && case "$TARGETARCH" in \
      amd64) echo x86_64-unknown-linux-musl ;; \
      arm64) echo aarch64-unknown-linux-musl ;; \
      *) echo "unsupported TARGETARCH $TARGETARCH" >&2; exit 1 ;; \
    esac > /rust-target \
 && rustup target add "$(cat /rust-target)"
# rust-lld cross-links the pure-Rust aarch64 binary with musl's bundled CRT objects; the
# native x86_64 build (and host proc-macros) keep Alpine's cc.
ENV CARGO_TARGET_AARCH64_UNKNOWN_LINUX_MUSL_LINKER=rust-lld
WORKDIR /src
COPY Cargo.toml Cargo.lock ./
COPY src ./src
RUN cargo build --release --locked --target "$(cat /rust-target)" \
 && cp "target/$(cat /rust-target)/release/loracamera-server" /loracamera-server \
 && mkdir -p /captures

FROM scratch
COPY --from=build /loracamera-server /loracamera-server
COPY --from=build --chown=10001:10001 /captures /captures
ENV CAPTURE_DIR=/captures
USER 10001:10001
EXPOSE 8000
ENTRYPOINT ["/loracamera-server"]
