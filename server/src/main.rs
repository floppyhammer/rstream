use gst::prelude::*;
use gstreamer as gst;
use std::process;

use std::{
    collections::HashMap,
    env,
    io::Error as IoError,
    net::SocketAddr,
    sync::{Arc, Mutex, Once},
};

use futures::prelude::*;
use futures::{
    channel::mpsc::{UnboundedSender, unbounded},
    future, pin_mut,
};

use async_std::net::{TcpListener, TcpStream};
use async_std::task;
use async_tungstenite::tungstenite::protocol::Message;
use enigo::Coordinate::Abs;
use enigo::Direction::{Click, Press, Release};
use enigo::{Button, Enigo, Keyboard, Mouse, Settings};

// --- FIXED: Use a thread-safe Mutex for the global pipeline ---
// The `Mutex` provides safe, exclusive access to the GStreamer pipeline.
// `Option<gst::Pipeline>` allows the pipeline to be present or absent (Null state).
static PIPELINE_GUARD: Mutex<Option<gst::Pipeline>> = Mutex::new(None);
static PIPELINE_INIT: Once = Once::new();

// A thread-safe global container for the Enigo instance.
// Mutex: Ensures exclusive access when a thread is using Enigo.
// Option: Allows Enigo to be initialized later (Lazy initialization).
static ENIGO_GUARD: Mutex<Option<Enigo>> = Mutex::new(None);
static ENIGO_INIT: Once = Once::new();

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
    });
}

// A function to initialize Enigo exactly once.
fn init_enigo() {
    ENIGO_INIT.call_once(|| {
        let enigo = Enigo::new(&Settings::default()).expect("Failed to initialize Enigo");
        *ENIGO_GUARD.lock().unwrap() = Some(enigo);
        println!("Enigo initialized.");
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
    let port = 5600; // Fixed port for UDP stream

    let pipeline_str = format!(
        "d3d11screencapturesrc ! videoconvert ! queue ! \
        x264enc name=enc tune=zerolatency sliced-threads=true speed-preset=ultrafast bframes=0 bitrate=16000 key-int-max=120 ! \
        video/x-h264,profile=main ! rtph264pay config-interval=-1 aggregate-mode=zero-latency ! \
        application/x-rtp,encoding-name=H264,clock-rate=90000,media=video,payload=96 ! \
        udpsink host={} port={}",
        host, port
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

fn stop_gstreamer_pipeline() {
    // Acquire the lock for the global pipeline state
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
            // --- NEW: Handle the incoming message/command ---
            // The `enigo` operations are blocking, so we use `task::spawn_blocking`
            // to run the handler without blocking the WebSocket message loop.
            if msg.is_text() {
                let command_msg = msg.clone();
                task::spawn_blocking(move || {
                    handle_message(command_msg);
                });
            }
            // -----------------------------------------------

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

    // --- LOGIC: Stop Pipeline if this was the last client ---
    if peer_map.lock().unwrap().is_empty() {
        // Spawn a task to run the blocking pipeline stop function
        task::spawn_blocking(stop_gstreamer_pipeline);
        // Reset the Once flag so the stream can be started again next time
        // NOTE: This is a complex step in real apps. The current GstPipelineControl
        // will prevent future restarts. For this example, we'll accept the limitation
        // that the process must restart to stream to a *new* first client.
    }
}

// Define a simple structure for the commands we expect via WebSocket
use serde::{Deserialize, Serialize};

#[derive(Debug, Serialize, Deserialize)]
pub struct InputMessage {
    pub msg: String,

    #[serde(rename = "type")]
    pub input_type: u8, // Assuming 'type' 2 is an unsigned 8-bit integer

    pub x: f64, // Floating-point number for coordinates
    pub y: f64,
}

fn handle_message(msg: Message) {
    if msg.is_text() {
        let text = msg.to_text().expect("Failed to get text from message");
        // println!("Received command: {}", text);

        let mut enigo_lock = ENIGO_GUARD.lock().unwrap();
        let enigo = enigo_lock.as_mut().expect("Enigo was not initialized!");

        match serde_json::from_str::<InputMessage>(text) {
            Ok(msg) => {
                // let mut enigo = Enigo::new(&Settings::default()).unwrap();
                println!("Received message: {:?}", msg.msg);
                println!("Received message type: {:?}", msg.input_type);
                println!("Received message pos: {:?}, {:?}", msg.x, msg.y);

                match msg.input_type {
                    0 => {
                        enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                        enigo.button(Button::Left, Press).unwrap();
                    }
                    1 => {
                        enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                    }
                    2_u8..=u8::MAX => {
                        enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                        enigo.button(Button::Left, Release).unwrap();
                    }
                }

                // enigo.button(Button::Left, Click).unwrap();
                // enigo
                //     .text("Hello World! here is a lot of text  ❤️")
                //     .unwrap();
            }
            Err(e) => {
                eprintln!(
                    "Failed to parse command JSON: {}. Original message: {}",
                    e, text
                );
            }
        }
    }
    // Ignore binary, ping, pong, and close messages here
}

async fn run_ws() -> Result<(), IoError> {
    let addr = env::args()
        .nth(1)
        .unwrap_or_else(|| "0.0.0.0:5601".to_string());

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

fn main() {
    // Initialize Enigo here, guaranteeing it happens before any messages are processed.
    init_enigo();

    let ws_handle = task::spawn(run_ws());

    // Block the main thread to keep the async runtime and the WS server alive.
    if let Err(e) = task::block_on(ws_handle) {
        eprintln!("WS server task failed: {}", e);
    }

    // Cleanup when the async task somehow exits (e.g., Ctrl+C, though this might be hard)
    // Running a final stop ensures cleanup if possible.
    stop_gstreamer_pipeline();
}
