use gst::prelude::*;
use gstreamer as gst;

use async_std::net::{TcpListener, TcpStream};
use async_std::task;
use async_tungstenite::tungstenite::protocol::Message;
use byteorder::{LittleEndian, ReadBytesExt};
use enigo::Button::ScrollDown;
use enigo::Coordinate::Abs;
use enigo::Direction::{Click, Press, Release};
use enigo::{Button, Direction, Enigo, Keyboard, Mouse, Settings};
use futures::prelude::*;
use futures::{
    channel::mpsc::{UnboundedSender, unbounded},
    future, pin_mut,
};
use gstreamer::glib::VariantClass::DictEntry;
use rusty_enet as enet;
use rusty_enet::Socket;
use std::io::Cursor;
use std::net::UdpSocket;
use std::str::FromStr;
use std::{
    collections::HashMap,
    env,
    io::Error as IoError,
    net::SocketAddr,
    sync::{Arc, Mutex, Once},
};
use vigem_client::{self as vigem, Client, TargetId, XButtons, XGamepad, Xbox360Wired};

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

static VIGEM_GUARD: Mutex<Option<Xbox360Wired<Client>>> = Mutex::new(None);
static GAMEPAD_GUARD: Mutex<Option<XGamepad>> = Mutex::new(None);
static VIGEM_INIT: Once = Once::new();

// --- ENet Configuration ---
const ENET_PORT: u16 = 7777; // Dedicated ENet port for input
const ENET_CHANNEL_INPUT: u8 = 0; // Channel 0 for reliable input commands

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

// A function to initialize Enigo exactly once.
fn init_enigo() {
    ENIGO_INIT.call_once(|| {
        let enigo = Enigo::new(&Settings::default()).expect("Failed to initialize Enigo");
        *ENIGO_GUARD.lock().unwrap() = Some(enigo);
        println!("Enigo initialized.");
    });
}

fn init_vigem() {
    VIGEM_INIT.call_once(|| {
        // 1. Connect to the ViGEmBus driver service
        let client = vigem::Client::connect().unwrap();

        println!("Vigem initialized.");

        // 2. Create the virtual controller target (Xbox 360 Wired)
        let id = TargetId::XBOX360_WIRED;
        let mut target = vigem::Xbox360Wired::new(client, id);

        // 3. Plug in the virtual controller
        println!("Plugging in virtual Xbox 360 controller...");
        target.plugin().unwrap();

        // 4. Wait for the virtual controller to be ready to accept updates
        println!("Waiting for controller to be ready...");
        target.wait_ready().unwrap();

        *VIGEM_GUARD.lock().unwrap() = Some(target);

        let mut gamepad = XGamepad {
            ..Default::default()
        };
        *GAMEPAD_GUARD.lock().unwrap() = Some(gamepad);

        println!("Controller is ready.");
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
            // Handle the incoming message/command
            if msg.is_text() {
                let text_msg = msg.clone();
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

use serde::{Deserialize, Serialize};

// Define a simple structure for the input events we expect via WebSocket
#[derive(Debug, Serialize, Deserialize)]
pub struct InputMessage {
    #[serde(rename = "msg-type")]
    pub msg_type: String,

    #[serde(rename = "input-type")]
    pub input_type: u8,

    pub x: f64, // Floating-point number for coordinates
    pub y: f64,
}

#[repr(u8)]
#[derive(Debug, PartialEq)]
enum InputType {
    CursorLeftDown = 0,
    CursorLeftUp = 1,
    CursorLeftClick = 2,
    CursorRightClick = 3,
    CursorMove = 4,
    CursorScroll = 5,
    GamepadButtonX = 6,
    GamepadLeftStick = 7,
    GamepadRightStick = 8,
}

impl TryFrom<u8> for InputType {
    type Error = &'static str;

    fn try_from(value: u8) -> Result<Self, Self::Error> {
        match value {
            0 => Ok(InputType::CursorLeftDown),
            1 => Ok(InputType::CursorLeftUp),
            2 => Ok(InputType::CursorLeftClick),
            3 => Ok(InputType::CursorRightClick),
            4 => Ok(InputType::CursorMove),
            5 => Ok(InputType::CursorScroll),
            6 => Ok(InputType::GamepadButtonX),
            7 => Ok(InputType::GamepadLeftStick),
            8 => Ok(InputType::GamepadRightStick),
            _ => Err("Invalid integer for MyEnum"),
        }
    }
}

fn handle_text_message(msg: Message) {
    if !msg.is_text() {
        return;
    }

    let text = msg.to_text().expect("Failed to get text from message");
    // println!("Received command: {}", text);

    let mut enigo_lock = ENIGO_GUARD.lock().unwrap();
    let enigo = enigo_lock.as_mut().expect("Enigo was not initialized!");

    match serde_json::from_str::<InputMessage>(text) {
        Ok(msg) => {
            let input_type = InputType::try_from(msg.input_type).unwrap();

            // println!("Received message type: {:?}", msg.msg_type);
            // println!("Received input type: {:?}", input_type);
            // println!("Received input position: {:?}, {:?}", msg.x, msg.y);

            match input_type {
                InputType::CursorLeftDown => {
                    enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                    enigo.button(Button::Left, Press).unwrap();
                }
                InputType::CursorLeftUp => {
                    enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                    enigo.button(Button::Left, Release).unwrap();
                }
                InputType::CursorMove => {
                    enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                }
                InputType::CursorScroll => {
                    if msg.y.abs() > 0.1 {
                        enigo.scroll(-msg.y as i32, enigo::Axis::Vertical).unwrap();
                    }
                }
                InputType::CursorLeftClick => {
                    enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                }
                InputType::CursorRightClick => {
                    enigo.move_mouse(msg.x as i32, msg.y as i32, Abs).unwrap();
                    enigo.button(Button::Right, Click).unwrap();
                }
                InputType::GamepadButtonX => {
                    println!("Gamepad Button X");
                }
                InputType::GamepadLeftStick => {
                    println!("Gamepad Left Stick ({}, {})", msg.x, msg.y);
                }
                InputType::GamepadRightStick => {
                    println!("Gamepad Right Stick ({}, {})", msg.x, msg.y);
                }
            }
        }
        Err(e) => {
            eprintln!(
                "Failed to parse command JSON: {}. Original message: {}",
                e, text
            );
        }
    }
}

async fn run_ws() -> Result<(), IoError> {
    let addr = env::args()
        .nth(1)
        .unwrap_or_else(|| "0.0.0.0:5600".to_string());

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

// Function to start the ENet server host
fn start_enet_server() -> enet::Host<UdpSocket> {
    let socket = UdpSocket::bind(SocketAddr::from_str("0.0.0.0:7777").unwrap()).unwrap();

    let mut host = enet::Host::new(
        socket,
        enet::HostSettings {
            peer_limit: 1,
            channel_limit: 2,
            ..Default::default()
        },
    )
    .unwrap();

    host
}

#[repr(C, packed)] // Crucial for cross-language compatibility
struct InputCommand {
    input_type: u8,
    data0: u32,
    data1: u32,
}

// Helper function to handle the IO operations
fn read_command_from_cursor(cursor: &mut Cursor<&[u8]>) -> Result<InputCommand, std::io::Error> {
    // 1. Read u8 (1 byte) - Endianness doesn't matter for single bytes
    let input_type = cursor.read_u8()?;

    // 2. Read i32 (4 bytes) - MUST enforce Little-Endian (LE)
    let data0 = cursor.read_u32::<LittleEndian>()?;

    // 3. Read i32 (4 bytes) - MUST enforce Little-Endian (LE)
    let data1 = cursor.read_u32::<LittleEndian>()?;

    Ok(InputCommand {
        input_type,
        data0,
        data1,
    })
}

// --- ENet Input Handling Function ---
fn handle_enet_packet(packet: &enet::Packet) {
    // 1. Check if the packet size matches the struct size.
    let packet_data = packet.data();
    if packet_data.len() != size_of::<InputCommand>() {
        eprintln!(
            "Received packet size mismatch! Expected {} bytes, got {}",
            size_of::<InputCommand>(),
            packet_data.len()
        );
        return;
    }

    println!("Received packet data: {:?}", packet_data);

    // 2. Perform the UNSAFE cast. This is only safe because we enforced
    //    #[repr(C, packed)] and checked the size.
    let command: InputCommand = unsafe {
        // Create a raw pointer to the packet data
        let ptr = packet_data.as_ptr() as *const InputCommand;
        // Dereference the pointer to get the struct
        ptr.read_unaligned()
    };

    // 1. Wrap the packet data in a Cursor for sequential reading
    let mut cursor = Cursor::new(packet_data);

    // 2. Read the fields manually, enforcing Little-Endian (LE) byte order
    let command = match read_command_from_cursor(&mut cursor) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Failed to deserialize packet with byteorder: {}", e);
            return;
        }
    };

    let x: f32 = f32::from_bits(command.data0);
    let y: f32 = f32::from_bits(command.data1);

    // println!("Received input type: {:?}", command.input_type);
    // println!("Received input position: {:?}, {:?}", x, y);

    let input_type = InputType::try_from(command.input_type).unwrap();

    let mut enigo_lock = ENIGO_GUARD.lock().unwrap();
    let enigo = enigo_lock.as_mut().expect("Enigo was not initialized!");

    match input_type {
        InputType::CursorLeftDown => {
            enigo.move_mouse(x as i32, y as i32, Abs).unwrap();
            enigo.button(Button::Left, Press).unwrap();
        }
        InputType::CursorLeftUp => {
            enigo.move_mouse(x as i32, y as i32, Abs).unwrap();
            enigo.button(Button::Left, Release).unwrap();
        }
        InputType::CursorMove => {
            enigo.move_mouse(x as i32, y as i32, Abs).unwrap();
        }
        InputType::CursorScroll => {
            if x.abs() > 0.1 {
                enigo
                    .scroll((-x / 10.0) as i32, enigo::Axis::Horizontal)
                    .unwrap();
            }
            if y.abs() > 0.1 {
                enigo
                    .scroll((-y / 10.0) as i32, enigo::Axis::Vertical)
                    .unwrap();
            }
        }
        InputType::CursorLeftClick => {
            enigo.move_mouse(x as i32, y as i32, Abs).unwrap();
            // NOTE: You may want to add enigo.button(Button::Left, Click).unwrap(); here
        }
        InputType::CursorRightClick => {
            enigo.move_mouse(x as i32, y as i32, Abs).unwrap();
            enigo.button(Button::Right, Click).unwrap();
        }
        InputType::GamepadButtonX => {
            // Gamepad logic needs to be implemented here
            println!("Gamepad Button X");
        }
        InputType::GamepadLeftStick => {
            // Gamepad logic needs to be implemented here
            println!("Gamepad Left Stick ({}, {})", x, y);
        }
        InputType::GamepadRightStick => {
            // Gamepad logic needs to be implemented here
            println!("Gamepad Right Stick ({}, {})", x, y);
        }
    }
}

// --- The Blocking ENet Server Loop ---
async fn run_enet_server() -> Result<(), IoError> {
    // This will run in a dedicated blocking thread, so we can use ENet's blocking service call.
    task::spawn_blocking(|| -> () {
        let mut host = start_enet_server();
        let mut received_events = false;

        loop {
            while let Some(event) = host.service().unwrap() {
                match event {
                    enet::Event::Connect { peer, .. } => {
                        println!("ENet peer {} connected", peer.id().0);
                    }
                    enet::Event::Disconnect { peer, .. } => {
                        println!("ENet peer {} disconnected", peer.id().0);
                    }
                    enet::Event::Receive {
                        peer,
                        channel_id,
                        packet,
                    } => {
                        handle_enet_packet(&packet);

                        received_events = true;
                    }
                }
            }

            // Only sleep if no events were processed in the last cycle,
            // allowing fast reaction when traffic is high.
            if !received_events {
                // Sleep for a significant duration (e.g., 10 milliseconds)
                std::thread::sleep(std::time::Duration::from_millis(10));
            }
        }
    })
    .await;

    Ok(())
}

fn main() {
    // Initialize Enigo here, guaranteeing it happens before any messages are processed.
    init_enigo();

    init_vigem();

    let ws_handle = task::spawn(run_ws());

    let enet_handle = task::spawn(run_enet_server());

    // Block the main thread to keep the async runtime and the WS server alive.
    if let (Err(e0), Err(e1)) = task::block_on(future::join(ws_handle, enet_handle)) {
        eprintln!("WS server task failed: {}", e0);
        eprintln!("WS server task failed: {}", e1);
    }

    // Cleanup when the async task somehow exits (e.g., Ctrl+C, though this might be hard)
    // Running a final stop ensures cleanup if possible.
    stop_gstreamer_pipeline()
}
