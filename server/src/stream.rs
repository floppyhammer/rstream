use gst::prelude::*;
use gstreamer as gst;

use async_std::net::{TcpListener, TcpStream};
use async_std::task;
use async_tungstenite::tungstenite::protocol::Message;
use futures::prelude::*;
use futures::{
    channel::mpsc::{unbounded, UnboundedSender},
    future, pin_mut,
};
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

// ----------------------------------------------------------------------
// --- GStreamer Functions (Now Thread-Safe) ----------------------------
// ----------------------------------------------------------------------

fn init_gstreamer() {
    // This function will initialize GStreamer only once.
    PIPELINE_INIT.call_once(|| {
        gst::init().unwrap();
        println!("GStreamer initialized.");
        gst::log::set_default_threshold(gst::DebugLevel::Info);
    });
}

fn start_gstreamer_pipeline(addr: SocketAddr) {
    // Acquire the lock for the global pipeline state
    let mut guard = PIPELINE_GUARD.lock().unwrap();

    // Check if a pipeline is already running
    if guard.is_some() {
        println!("Pipeline already running. Not restarting.");
        return;
    }

    let host = addr.ip().to_string();

    let pipeline_str = format!(
        "rtpbin name=rtpbin \
        d3d11screencapturesrc show-cursor=true ! videoconvert ! queue ! \
        x264enc name=enc tune=zerolatency sliced-threads=true speed-preset=ultrafast bframes=0 bitrate=20000 key-int-max=120 ! \
        video/x-h264,profile=main ! rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
        application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
        rtpbin.send_rtp_sink_0 \
        rtpbin. ! \
        udpsink host={} port=5601 sync=false \
        wasapi2src loopback=true low-latency=true ! \
        queue ! \
        audioconvert ! \
        audioresample ! \
        queue ! \
        opusenc perfect-timestamp=false ! \
        rtpopuspay ! \
        application/x-rtp,encoding-name=OPUS,media=audio,payload=127 !
        rtpbin.send_rtp_sink_1 \
        rtpbin. ! \
        udpsink host={} port=5602 sync=false",
        host, host
    );

    println!("Attempting to start pipeline to: {}...", addr);

    let mut context = gst::ParseContext::new();

    let pipeline = match gst::parse::launch_full(
        &pipeline_str,
        Some(&mut context),
        gst::ParseFlags::empty(),
    ) {
        Ok(pipeline) => pipeline,
        Err(err) => {
            if let Some(gst::ParseError::NoSuchElement) = err.kind::<gst::ParseError>() {
                eprintln!("Missing element(s): {:?}", context.missing_elements());
            } else {
                eprintln!("Failed to parse pipeline: {err}");
            }
            return;
        }
    };

    let pipeline = pipeline.downcast::<gst::Pipeline>().unwrap();

    // Store the running pipeline in the global Mutex
    *guard = Some(pipeline.clone());

    // Set pipeline to playing
    if let Err(e) = pipeline.set_state(gst::State::Playing) {
        eprintln!("Failed to set pipeline to Playing: {}", e);
    } else {
        println!("Pipeline started playing to {}!", addr);
    }
}

pub fn stop_gstreamer_pipeline() {
    // Acquire the lock for the global pipeline state.
    let mut guard = PIPELINE_GUARD.lock().unwrap();

    // Use `Option::take()` to extract the pipeline and replace the value with None.
    // The extracted pipeline reference will then be dropped when it goes out of scope.
    if let Some(pipeline) = guard.take() {
        println!("Stopping pipeline.");
        pipeline
            .set_state(gst::State::Null)
            .expect("Unable to set the pipeline to the `Null` state");
        println!("Pipeline stopped.");
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
    println!("Incoming TCP connection from: {}", addr);

    let ws_stream = async_tungstenite::accept_async(raw_stream)
        .await
        .expect("Error during the websocket handshake occurred");
    println!("WebSocket connection established: {}", addr);

    // --- LOGIC: Start Pipeline on First Connection ---
    let start_pipe = move || {
        init_gstreamer();
    };
    start_once.call_once(start_pipe);
    // ---------------------------------------------------

    // Spawn a task to run the blocking pipeline start function
    task::spawn_blocking(move || {
        start_gstreamer_pipeline(addr);
    });

    // Insert the write part of this peer to the peer map.
    let (tx, rx) = unbounded();
    peer_map.lock().unwrap().insert(addr, tx);

    let (outgoing, incoming) = ws_stream.split();

    let broadcast_incoming = incoming
        .try_filter(|msg| future::ready(!msg.is_close()))
        .try_for_each(|msg| {
            // Handle the incoming message/command
            if msg.is_text() {
                let text_msg = msg .clone();
                handle_text_message(text_msg);
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

    println!("{} disconnected", &addr);
    peer_map.lock().unwrap().remove(&addr);

    // Stop Pipeline if this was the last client
    if peer_map.lock().unwrap().is_empty() {
        // Spawn a task to run the blocking pipeline stop function
        task::spawn_blocking(stop_gstreamer_pipeline);
        // Reset the Once flag so the stream can be started again next time
        // NOTE: This is a complex step in real apps. The current GstPipelineControl
        // will prevent future restarts. For this example, we'll accept the limitation
        // that the process must restart to stream to a *new* first client.
    }
}

pub async fn run_websocket(port: u32) -> Result<(), IoError> {
    let addr = format!("0.0.0.0:{}", port);

    let state = PeerMap::new(Mutex::new(HashMap::new()));
    let gst_control = GstPipelineControl::new(Once::new());

    let try_socket = TcpListener::bind(&addr).await;
    let listener = try_socket.expect("Failed to bind");
    println!("Listening on: {}", addr);

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

// Video control via WebSocket.
fn handle_text_message(msg: Message) {
    if !msg.is_text() {
        return;
    }

    let text = msg.to_text().expect("Failed to get text from message");
    println!("Received command: {}", text);
}
