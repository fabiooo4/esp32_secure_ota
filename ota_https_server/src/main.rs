use axum::Router;
use clap::Parser;
use hyper_util::rt::{TokioExecutor, TokioIo};
use hyper_util::server::conn::auto::Builder;
use hyper_util::service::TowerToHyperService;
use rustls::pki_types::{CertificateDer, PrivateKeyDer};
use std::fs::File;
use std::io::BufReader;
use std::net::{IpAddr, SocketAddr};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use tokio::net::TcpListener;
use tokio_rustls::TlsAcceptor;
use tower::Service;
use tower_http::services::ServeDir;
use tracing::{error, info, warn};

// --- CLI Args ---
#[derive(Parser, Debug)]
#[clap(author, version, about, long_about = None)]
struct Args {
    /// Directory to serve files from
    #[clap(short, long)]
    dir: PathBuf,

    /// IP address to bind to
    #[clap(short, long, default_value = "0.0.0.0")]
    ip: IpAddr,

    /// Port to bind to
    #[clap(short, long, default_value_t = 8070)]
    port: u16,

    /// Path to custom certificates save directory
    #[clap(short, long)]
    cert_dir: Option<PathBuf>,
}

/// Load public certificate from a PEM file
fn load_certs(path: &Path) -> anyhow::Result<Vec<CertificateDer<'static>>> {
    let cert_file = File::open(path)?;
    let mut reader = BufReader::new(cert_file);
    let certs = rustls_pemfile::certs(&mut reader).collect::<Result<Vec<_>, _>>()?;
    Ok(certs)
}

/// Load private key from a PEM file
fn load_key(path: &Path) -> anyhow::Result<PrivateKeyDer<'static>> {
    let key_file = File::open(path)?;
    let mut reader = BufReader::new(key_file);
    let key = rustls_pemfile::private_key(&mut reader)?
        .ok_or_else(|| anyhow::anyhow!("could not find private key in file"))?;
    Ok(key)
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // Initialize logging
    tracing_subscriber::fmt()
        .with_env_filter(
            tracing_subscriber::EnvFilter::try_from_default_env()
                .unwrap_or_else(|_| "info,tower_http=debug".into()),
        )
        .init();

    // Parse command-line arguments
    let args = Args::parse();

    // Resolve the serving directory to an absolute path
    let serve_dir = std::fs::canonicalize(&args.dir)
        .map_err(|e| anyhow::anyhow!("Failed to find serving directory {:?}: {}", args.dir, e))?;

    // --- Server Configuration ---
    // Create the Axum app and address
    let app = Router::new().nest_service("/", ServeDir::new(&serve_dir));
    let addr = SocketAddr::new(args.ip, args.port);

    // --- Server Mode Selection ---
    match args.cert_dir {
        Some(cert_dir_path) => { // --- HTTPS Mode ---
            info!("--cert-dir provided, starting in HTTPS mode.");

            let cert_dir = std::fs::canonicalize(cert_dir_path.clone()).map_err(|e| {
                anyhow::anyhow!(
                    "Failed to find certificate directory {:?}: {}",
                    cert_dir_path,
                    e
                )
            })?;

            let default_cert_path = cert_dir.join("ca_cert.pem");
            let default_key_path = cert_dir.join("ca_key.pem");

            info!("Using certificate: {:?}", default_cert_path);
            info!("Using private key: {:?}", default_key_path);

            // --- TLS (HTTPS) Configuration ---
            let certs = load_certs(&default_cert_path)?;
            let key = load_key(&default_key_path)?;

            let tls_config = Arc::new(
                rustls::ServerConfig::builder()
                    .with_no_client_auth()
                    .with_single_cert(certs, key)
                    .map_err(|e| anyhow::anyhow!("Failed to create TLS config: {}", e))?,
            );

            // --- HTTPS Server Loop ---
            let tls_acceptor = TlsAcceptor::from(tls_config);
            let listener = TcpListener::bind(addr).await?;
            info!(
                "Starting HTTPS server on https://{}/ serving files from {:?}",
                addr, serve_dir
            );
            info!(
                "Your ESP32 should connect to: https://<your_ipv4>:{}/<bin_name>",
                args.port
            );

            let app_service = app.into_make_service_with_connect_info::<SocketAddr>();

            loop {
                let (tcp_stream, peer_addr) = match listener.accept().await {
                    Ok(s) => s,
                    Err(e) => {
                        error!("TCP accept error: {}", e);
                        continue;
                    }
                };
                info!("TCP connection accepted from: {}", peer_addr);

                let tls_acceptor = tls_acceptor.clone();
                let mut app_service = app_service.clone();

                tokio::spawn(async move {
                    let service_for_connection = app_service.call(peer_addr).await.unwrap();

                    let hyper_service = TowerToHyperService::new(service_for_connection);

                    // Perform TLS handshake
                    match tls_acceptor.accept(tcp_stream).await {
                        Ok(tls_stream) => {
                            info!("TLS handshake successful with: {}", peer_addr);
                            let io = TokioIo::new(tls_stream);

                            if let Err(err) = Builder::new(TokioExecutor::new())
                                .serve_connection(io, hyper_service)
                                .await
                            {
                                warn!("Connection from {} closed with error: {}", peer_addr, err);
                            } else {
                                info!("Connection from {} closed successfully.", peer_addr);
                            }
                        }
                        Err(e) => {
                            warn!("TLS handshake error from {}: {}", peer_addr, e);
                        }
                    }
                });
            }
        }
        None => { // --- HTTP Mode ---
            info!("--cert-dir not provided, starting in HTTP mode.");
            info!(
                "Starting HTTP server on http://{}/ serving files from {:?}",
                addr, serve_dir
            );
            info!(
                "Your ESP32 should connect to: http://{}:{}/<bin_name>",
                args.ip, args.port
            );

            let listener = TcpListener::bind(addr).await?;

            let app_service = app.into_make_service_with_connect_info::<SocketAddr>();

            loop {
                let (tcp_stream, peer_addr) = match listener.accept().await {
                    Ok(s) => s,
                    Err(e) => {
                        error!("TCP accept error: {}", e);
                        continue;
                    }
                };
                info!("TCP connection accepted from: {}", peer_addr);

                let mut app_service = app_service.clone();

                tokio::spawn(async move {
                    let service_for_connection = app_service.call(peer_addr).await.unwrap();

                    let hyper_service = TowerToHyperService::new(service_for_connection);
                    let io = TokioIo::new(tcp_stream);

                    if let Err(err) = Builder::new(TokioExecutor::new())
                        .serve_connection(io, hyper_service)
                        .await
                    {
                        warn!("Connection from {} closed with error: {}", peer_addr, err);
                    } else {
                        info!("Connection from {} closed successfully.", peer_addr);
                    }
                });
            }
        }
    }
}
