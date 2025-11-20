use gst::prelude::*;
use gstreamer as gst;

use async_std::net::{TcpListener, TcpStream};
use async_std::task;
use async_tungstenite::tungstenite::protocol::frame::coding::CloseCode;
use async_tungstenite::tungstenite::protocol::{CloseFrame, Message};
use chrono::{SubsecRound, Utc};
use futures::prelude::*;
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    future, pin_mut,
};
use gstreamer::glib::ControlFlow;
use gstreamer::MessageView;
use log::{error, info, warn};
use serde::{Deserialize, Serialize};
use std::{
    collections::HashMap,
    io::Error as IoError,
    net::SocketAddr,
    sync::{Arc, Mutex, Once},
};

// --- FIXED: Use a thread-safe Mutex for the global pipeline ---
// The `Mutex` provides safe, exclusive access to the GStreamer pipeline.
// `Option<gst::Pipeline>` allows the pipeline to be present or absent (Null state).
static PIPELINE_GUARD: Mutex<Option<gst::Pipeline>> = Mutex::new(None);
static PIPELINE_INIT: Once = Once::new();

// We'll keep the GstPipelineControl for single-start logic
type GstPipelineControl = Arc<Once>;

type Tx = UnboundedSender<Message>;
type PeerMap = Arc<Mutex<HashMap<SocketAddr, Tx>>>;

pub struct Peer {
    pub(crate) ip: String,
    pub(crate) time_connected: String,
}

pub struct StreamConfig {
    pub(crate) resolution: (u32, u32),
    pub(crate) framerate: u32,
    pub(crate) bitrate: u32,
}

pub struct StreamingState {
    pub(crate) peers: HashMap<SocketAddr, Peer>,
    pub(crate) dpi_scale: f32,
    pub(crate) native_resolution: (u32, u32),
    pub(crate) stream_config: Option<StreamConfig>,
    pub(crate) connection_status: ConnectionStatus,
    pub(crate) pin: String,
}

pub static STREAMING_STATE_GUARD: Mutex<Option<StreamingState>> = Mutex::new(None);

// ----------------------------------------------------------------------
// --- GStreamer Functions (Now Thread-Safe) ----------------------------
// ----------------------------------------------------------------------

#[derive(Copy, Clone)]
pub(crate) enum ConnectionStatus {
    Ready,
    Connected,
    Error,
}

fn init_gstreamer() {
    // This function will initialize GStreamer only once.
    PIPELINE_INIT.call_once(|| {
        gst::init().unwrap();
        info!("GStreamer initialized.");
        gst::log::set_default_threshold(gst::DebugLevel::Warning);
    });
}

// fn udpsrc_sink_pad_probe(_pad: &gst::Pad, info: &mut gst::PadProbeInfo) -> gst::PadProbeReturn {
//     if let Some(gst::PadProbeData::Buffer(ref buffer)) = info.data {
//         // Acquire the lock for the global pipeline state.
//         let mut guard = PIPELINE_GUARD.lock().unwrap();
//
//         // Use `Option::take()` to extract the pipeline and replace the value with None.
//         // The extracted pipeline reference will then be dropped when it goes out of scope.
//         if let Some(pipeline) = guard.as_ref() {
//             // Check pipeline
//             let dot_data = pipeline.debug_to_dot_data(gst::DebugGraphDetails::ALL);
//             let dot_str = dot_data.as_str();
//             let a = 2;
//             let b = 2;
//             let c = a + b;
//         }
//     }
//
//     gst::PadProbeReturn::Ok
// }

fn check_factory_exists(factory_name: &str) -> bool {
    gst::ElementFactory::find(factory_name).is_some()
}

fn start_gstreamer_pipeline(addr: SocketAddr, config: StreamConfigMessage) {
    // Acquire the lock for the global pipeline state
    let mut guard = PIPELINE_GUARD.lock().unwrap();

    // Check if a pipeline is already running
    if guard.is_some() {
        warn!("Pipeline already running. Not restarting.");
        return;
    }

    let host = addr.ip().to_string();

    let found_amf = check_factory_exists("amfh264enc");

    let encoder_str = if found_amf {
        info!("amfh264enc is available.");

        format!(
            "d3d11convert ! \
        videorate ! \
        video/x-raw(memory:D3D11Memory),width={},height={},format=NV12,framerate={}/1 ! \
        amfh264enc name=enc preset=speed usage=ultra-low-latency rate-control=cbr bitrate={} gop-size=30 ! ",
            config.video_width,
            config.video_height,
            config.framerate,
            config.bitrate * 1024
        )
    } else {
        format!("videoconvert ! \
        videoscale ! \
        videorate ! \
        video/x-raw,width={},height={},format=NV12,framerate={}/1 ! \
        x264enc name=enc tune=zerolatency sliced-threads=true speed-preset=ultrafast bframes=0 bitrate={} key-int-max=30 ! ",
                config.video_width,
                config.video_height,
                config.framerate,
                config.bitrate * 1024
        )
    };

    let pipeline_str = format!(
        "rtpbin name=rtp \
        d3d11screencapturesrc show-cursor=true ! \
        {}\
        video/x-h264,profile=baseline ! \
        rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
        application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
        rtp.send_rtp_sink_0 \
        rtp.send_rtp_src_0 ! \
        udpsink name=videoudpsrc host={} port=5601 sync=false \
        wasapi2src loopback=true low-latency=true ! \
        queue ! \
        audioconvert ! \
        audioresample ! \
        audio/x-raw,rate=48000 ! \
        opusenc perfect-timestamp=true audio-type=restricted-lowdelay bitrate-type=cbr frame-size=10 ! \
        rtpopuspay ! \
        application/x-rtp,encoding-name=OPUS,media=audio,payload=127 !
        rtp.send_rtp_sink_1 \
        rtp.send_rtp_src_1 ! \
        udpsink host={} port=5602 sync=false",
        encoder_str, host, host
    );

    info!("Attempting to parse pipeline: \n{}", pipeline_str);

    let mut context = gst::ParseContext::new();

    let pipeline = match gst::parse::launch_full(
        &pipeline_str,
        Some(&mut context),
        gst::ParseFlags::empty(),
    ) {
        Ok(pipeline) => pipeline,
        Err(err) => {
            if let Some(gst::ParseError::NoSuchElement) = err.kind::<gst::ParseError>() {
                error!("Missing element(s): {:?}", context.missing_elements());
            } else {
                error!("Failed to parse pipeline: {err}");
            }
            return;
        }
    };

    let pipeline = pipeline.downcast::<gst::Pipeline>().unwrap();

    // // Add a probe
    // {
    //     let udpsrc = pipeline
    //         .by_name("videoudpsrc")
    //         .expect("Could not find videoudpsrc element.");
    //
    //     let pad = udpsrc
    //         .static_pad("sink")
    //         .expect("Could not find static sink pad in videoudpsrc.");
    //
    //     pad.add_probe(gst::PadProbeType::BUFFER, move |pad, info| {
    //         udpsrc_sink_pad_probe(pad, info)
    //     });
    // }

    // Check pipeline
    // let dot_data = pipeline.debug_to_dot_data(gst::DebugGraphDetails::ALL);
    // let _dot_str = dot_data.as_str();

    let bus = pipeline.bus().unwrap();

    let _bus_watch_id = bus.add_watch(move |_, msg| {
        match msg.view() {
            MessageView::Error(err) => {
                error!(
                    "Error from {:?}: {} ({:?})",
                    err.src().map(|s| s.path_string()),
                    err.error(),
                    err.debug()
                );
                // An error occurred, you might want to quit the application here
                // Returning `glib::Continue(false)` stops the watch.
                // In a real app, you'd send an event to the main thread to handle shutdown.
                // For simplicity here, we'll just log and continue.
            }
            MessageView::Warning(warning) => {
                error!(
                    "Warning from {:?}: {} ({:?})",
                    warning.src().map(|s| s.path_string()),
                    warning.error(),
                    warning.debug()
                );
            }
            MessageView::Eos(_) => {
                error!("End of stream reached.");
                // End of stream, you might want to quit the application here
                // Returning `glib::Continue(false)` stops the watch.
            }
            MessageView::StateChanged(state_changed) => {
                error!(
                    "Pipeline state changed from {:?} to {:?} (pending: {:?})",
                    state_changed.old(),
                    state_changed.current(),
                    state_changed.pending(),
                );
            }
            // Add more match arms for other message types you care about
            _ => {
                error!("Unhandled message: {:?}", msg.type_()); // Uncomment for all messages
            }
        }
        ControlFlow::Continue
    });

    // Store the running pipeline in the global Mutex
    *guard = Some(pipeline.clone());

    // Set pipeline to playing
    if let Err(e) = pipeline.set_state(gst::State::Playing) {
        error!("Failed to set pipeline to Playing: {}", e);
    } else {
        info!("Pipeline started playing to {}!", addr);
    }
}

pub fn stop_gstreamer_pipeline() {
    // Acquire the lock for the global pipeline state.
    let mut guard = PIPELINE_GUARD.lock().unwrap();

    // Use `Option::take()` to extract the pipeline and replace the value with None.
    // The extracted pipeline reference will then be dropped when it goes out of scope.
    if let Some(pipeline) = guard.take() {
        pipeline
            .set_state(gst::State::Null)
            .expect("Unable to set the pipeline to the `Null` state");
        info!("Pipeline stopped.");
    }
    // The lock is automatically released when `guard` goes out of scope.
}

// ----------------------------------------------------------------------
// --- Asynchronous WebSocket Functions ---------------------------------
// ----------------------------------------------------------------------

async fn handle_connection(
    peer_map: PeerMap,
    raw_stream: TcpStream,
    addr: SocketAddr,
    start_once: GstPipelineControl,
) {
    info!("Incoming TCP connection from: {}", addr);

    let ws_stream = async_tungstenite::accept_async(raw_stream)
        .await
        .expect("Error during the websocket handshake occurred");

    info!("WebSocket connection established: {}", addr);

    {
        let mut guard = STREAMING_STATE_GUARD.lock().unwrap();
        let date_as_string = Utc::now().trunc_subsecs(0).to_string();
        if let Some(state) = guard.as_mut() {
            state.peers.insert(
                addr,
                Peer {
                    ip: addr.to_string(),
                    time_connected: date_as_string,
                },
            );
        }
    }

    // Initialize gstreamer.
    let init_gst = move || {
        init_gstreamer();
    };
    start_once.call_once(init_gst);

    // Insert the write part of this peer to the peer map.
    let (tx, rx) = unbounded();
    peer_map.lock().unwrap().insert(addr, tx);

    let (outgoing, incoming) = ws_stream.split();

    let broadcast_incoming = incoming
        .try_filter(|msg| future::ready(!msg.is_close()))
        .try_for_each(|msg| {
            let current_peer_map = peer_map.clone();

            // Handle the incoming message/command
            if msg.is_text() {
                let text_msg = msg.clone();
                handle_text_message(text_msg, addr, current_peer_map);
            }

            let peers = peer_map.lock().unwrap();
            let broadcast_recipients = peers
                .iter()
                .filter(|(peer_addr, _)| peer_addr != &&addr)
                .map(|(_, ws_sink)| ws_sink);

            for recp in broadcast_recipients {
                recp.unbounded_send(msg.clone()).unwrap();
            }

            future::ok(())
        });

    let receive_from_others = rx.map(Ok).forward(outgoing);

    pin_mut!(broadcast_incoming, receive_from_others);
    future::select(broadcast_incoming, receive_from_others).await;

    info!("WebSocket {} disconnected", &addr);
    peer_map.lock().unwrap().remove(&addr);

    {
        let mut guard = STREAMING_STATE_GUARD.lock().unwrap();
        if let Some(state) = guard.as_mut() {
            state.peers.remove(&addr);
            state.stream_config = None;
            state.connection_status = ConnectionStatus::Ready;
        }
    }

    // Stop Pipeline if this was the last client
    if peer_map.lock().unwrap().is_empty() {
        // Spawn a task to run the blocking pipeline stop function
        task::spawn_blocking(stop_gstreamer_pipeline);
    }
}

pub async fn run_websocket(port: u32) -> Result<(), IoError> {
    let addr = format!("0.0.0.0:{}", port);

    let state = PeerMap::new(Mutex::new(HashMap::new()));
    let gst_control = GstPipelineControl::new(Once::new());

    let try_socket = TcpListener::bind(&addr).await;
    let listener = try_socket.expect("Failed to bind");
    info!("WebSocket listening on: {}", addr);

    while let Ok((stream, addr)) = listener.accept().await {
        task::spawn(handle_connection(
            state.clone(),
            stream,
            addr,
            gst_control.clone(),
        ));
    }

    Ok(())
}

#[derive(Debug, Serialize, Deserialize)]
pub struct StreamConfigMessage {
    pub pin: String,
    pub video_width: u32,
    pub video_height: u32,
    pub framerate: u32,
    pub bitrate: u32,
}

// Video control via WebSocket.
fn handle_text_message(msg: Message, addr: SocketAddr, peer_map: PeerMap) {
    let text = match msg {
        Message::Text(t) => t,
        _ => return, // Handle other message types
    };

    match serde_json::from_str::<StreamConfigMessage>(&text) {
        Ok(config_msg) => {
            info!(
                "✅ Stream config received successfully:\n\tPIN: {}\n\tVideo Size: {}x{}\n\tBitrate: {}",
                config_msg.pin, config_msg.video_width, config_msg.video_height, config_msg.bitrate
            );

            let mut authenticated = false;

            {
                let mut guard = STREAMING_STATE_GUARD.lock().unwrap();
                if let Some(state) = guard.as_mut() {
                    authenticated = state.pin == config_msg.pin;

                    if authenticated {
                        let config = StreamConfig {
                            resolution: (config_msg.video_width, config_msg.video_height),
                            framerate: config_msg.framerate,
                            bitrate: config_msg.bitrate,
                        };

                        state.stream_config = Some(config);
                        state.connection_status = ConnectionStatus::Connected;
                    }
                }
            }

            if authenticated {
                // Spawn a task to run the blocking pipeline start function
                task::spawn_blocking(move || {
                    start_gstreamer_pipeline(addr, config_msg);
                });
            } else {
                warn!("Authentication failed for {}. Closing connection.", addr);
                if let Some(tx) = peer_map.lock().unwrap().get(&addr) {
                    if let Err(e) = tx.unbounded_send(Message::Close(Some(CloseFrame {
                        code: CloseCode::Invalid,
                        reason: "Authentication Failed".into(),
                    }))) {
                        error!("Failed to send close message to {}: {}", addr, e);
                    }
                }
                // The `broadcast_incoming` loop will eventually detect the send error or the actual close
                // and the connection will be handled as disconnected by the `future::select` below.
            }
        }
        Err(e) => {
            error!(
                "❌ ERROR: Failed to deserialize JSON: {}\n\tPayload was: {}",
                e, text
            );
        }
    }
}
