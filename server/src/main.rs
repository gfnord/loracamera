//! HTTP PUT receiver for the loracamera base station.
//!
//! Every authenticated `PUT` is saved as `MMDDHHMM.<ext>` (UTC arrival time) in
//! `CAPTURE_DIR`. The extension is the last three characters of the URL path, so the
//! firmware's `/image.jpg` and `/audio.raw` become `.jpg` and `.raw`. A second upload with
//! the same name gets a `-2`, `-3`, … suffix instead of overwriting the first.
//!
//! Configuration (environment):
//! - `UPLOAD_TOKEN` (required, ≥ 16 characters): clients send `Authorization: Bearer <token>`.
//! - `LISTEN_ADDR` (default `0.0.0.0:8000`).
//! - `CAPTURE_DIR` (default `.`), created if missing.
//! - `MAX_UPLOAD_BYTES` (default 262144): larger bodies are rejected with 413.

use std::{env, io::ErrorKind, net::SocketAddr, path::PathBuf, process, sync::Arc};

use axum::{
    Router,
    body::{Body, to_bytes},
    extract::{ConnectInfo, Request, State},
    http::{Method, StatusCode, header},
    response::{IntoResponse, Response},
};
use tokio::{fs::OpenOptions, io::AsyncWriteExt, net::TcpListener};

const MIN_TOKEN_LEN: usize = 16;
// Same-name uploads (same minute and extension) get a numeric suffix up to this many.
const MAX_SUFFIX: u32 = 1000;

struct Config {
    // Full expected header value, `Bearer <token>`.
    authorization: Vec<u8>,
    dir: PathBuf,
    max_upload: usize,
}

fn config() -> Result<(Config, String), String> {
    let token = env::var("UPLOAD_TOKEN").map_err(|_| "UPLOAD_TOKEN is not set".to_string())?;
    if token.len() < MIN_TOKEN_LEN {
        return Err(format!("UPLOAD_TOKEN must be at least {MIN_TOKEN_LEN} characters"));
    }
    let max_upload = match env::var("MAX_UPLOAD_BYTES") {
        Ok(v) => v
            .parse()
            .ok()
            .filter(|&n: &usize| n > 0)
            .ok_or_else(|| format!("MAX_UPLOAD_BYTES must be a positive integer, got {v:?}"))?,
        Err(_) => 256 * 1024,
    };
    let addr = env::var("LISTEN_ADDR").unwrap_or_else(|_| "0.0.0.0:8000".to_string());
    let dir = PathBuf::from(env::var("CAPTURE_DIR").unwrap_or_else(|_| ".".to_string()));
    Ok((
        Config {
            authorization: format!("Bearer {token}").into_bytes(),
            dir,
            max_upload,
        },
        addr,
    ))
}

// Compares in time independent of where the inputs differ.
fn constant_time_eq(a: &[u8], b: &[u8]) -> bool {
    a.len() == b.len() && a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

// Last three characters of the path (`/image.jpg` → `jpg`); only ASCII letters and digits.
fn extension(path: &str) -> Option<&str> {
    let ext = path.get(path.len().checked_sub(3)?..)?;
    ext.bytes().all(|b| b.is_ascii_alphanumeric()).then_some(ext)
}

async fn save(cfg: &Config, ext: &str, data: &[u8]) -> std::io::Result<String> {
    let stem = chrono::Utc::now().format("%m%d%H%M").to_string();
    for n in 1..=MAX_SUFFIX {
        let name = if n == 1 { format!("{stem}.{ext}") } else { format!("{stem}-{n}.{ext}") };
        match OpenOptions::new().write(true).create_new(true).open(cfg.dir.join(&name)).await {
            Ok(mut file) => {
                file.write_all(data).await?;
                file.sync_all().await?;
                return Ok(name);
            }
            Err(e) if e.kind() == ErrorKind::AlreadyExists => continue,
            Err(e) => return Err(e),
        }
    }
    Err(std::io::Error::new(ErrorKind::AlreadyExists, format!("{stem}-{MAX_SUFFIX}.{ext} exists")))
}

async fn upload(cfg: &Config, req: Request) -> Response {
    if req.method() != Method::PUT {
        return (StatusCode::METHOD_NOT_ALLOWED, [(header::ALLOW, "PUT")], "Only PUT is supported\n")
            .into_response();
    }
    // Checked before the body is read, so unauthenticated clients can't make us buffer it.
    let authorized = req
        .headers()
        .get(header::AUTHORIZATION)
        .is_some_and(|v| constant_time_eq(v.as_bytes(), &cfg.authorization));
    if !authorized {
        return (StatusCode::UNAUTHORIZED, [(header::WWW_AUTHENTICATE, "Bearer")], "Unauthorized\n")
            .into_response();
    }
    let Some(ext) = extension(req.uri().path()).map(str::to_owned) else {
        return (StatusCode::BAD_REQUEST, "URL path must end in a 3-character extension, e.g. /image.jpg\n")
            .into_response();
    };
    let data = match to_bytes(req.into_body(), cfg.max_upload).await {
        Ok(data) if data.is_empty() => {
            return (StatusCode::BAD_REQUEST, "Empty upload\n").into_response();
        }
        Ok(data) => data,
        Err(_) => {
            return (StatusCode::PAYLOAD_TOO_LARGE, format!("Upload exceeds {} bytes\n", cfg.max_upload))
                .into_response();
        }
    };
    match save(cfg, &ext, &data).await {
        Ok(name) => (StatusCode::CREATED, format!("Saved \"{name}\" ({} bytes)\n", data.len())).into_response(),
        Err(e) => {
            eprintln!("save failed: {e}");
            (StatusCode::INTERNAL_SERVER_ERROR, "Save failed\n").into_response()
        }
    }
}

async fn handle(State(cfg): State<Arc<Config>>, ConnectInfo(peer): ConnectInfo<SocketAddr>, req: Request) -> Response {
    // Behind Caddy the peer is the proxy; X-Forwarded-For carries the base station's address.
    let client = match req.headers().get("x-forwarded-for").and_then(|v| v.to_str().ok()) {
        Some(forwarded) => format!("{forwarded} via {peer}"),
        None => peer.to_string(),
    };
    let line = format!("{client} \"{} {}\"", req.method(), req.uri().path());
    let response = upload(&cfg, req).await;
    let (parts, body) = response.into_parts();
    // Log the response text too: it names the saved file or the rejection reason.
    let text = to_bytes(body, usize::MAX).await.unwrap_or_default();
    println!("{line} {} {}", parts.status.as_u16(), String::from_utf8_lossy(&text).trim_end());
    Response::from_parts(parts, Body::from(text))
}

async fn shutdown_signal() {
    let ctrl_c = tokio::signal::ctrl_c();
    let mut term = tokio::signal::unix::signal(tokio::signal::unix::SignalKind::terminate())
        .expect("install SIGTERM handler");
    tokio::select! {
        _ = ctrl_c => {}
        _ = term.recv() => {}
    }
}

#[tokio::main(flavor = "current_thread")]
async fn main() {
    let (cfg, addr) = config().unwrap_or_else(|e| {
        eprintln!("loracamera-server: {e}");
        process::exit(2);
    });
    if let Err(e) = tokio::fs::create_dir_all(&cfg.dir).await {
        eprintln!("loracamera-server: can't create {}: {e}", cfg.dir.display());
        process::exit(1);
    }
    let listener = TcpListener::bind(&addr).await.unwrap_or_else(|e| {
        eprintln!("loracamera-server: can't listen on {addr}: {e}");
        process::exit(1);
    });
    println!(
        "loracamera-server listening on {addr}, saving to {}, max {} bytes per upload",
        cfg.dir.display(),
        cfg.max_upload
    );
    let app = Router::new().fallback(handle).with_state(Arc::new(cfg));
    axum::serve(listener, app.into_make_service_with_connect_info::<SocketAddr>())
        .with_graceful_shutdown(shutdown_signal())
        .await
        .expect("server error");
}
