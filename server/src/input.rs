use async_std::task;
use byteorder::{LittleEndian, ReadBytesExt};
use enigo::Coordinate::Abs;
use enigo::Direction::{Click, Press, Release};
use enigo::{Button, Enigo, Mouse, Settings};
use rusty_enet as enet;
use std::io::Cursor;
use std::io::Error as IoError;
use std::net::{SocketAddr, UdpSocket};
use std::str::FromStr;
use std::sync::{Mutex, Once};
use vigem_client::{self as vigem, Client, TargetId, XButtons, XGamepad, Xbox360Wired};

// --- ENet Configuration ---
const ENET_PORT: u16 = 7777; // Dedicated ENet port for input
const ENET_CHANNEL_INPUT: u8 = 0; // Channel 0 for reliable input commands

// A thread-safe global container for the Enigo instance.
// Mutex: Ensures exclusive access when a thread is using Enigo.
// Option: Allows Enigo to be initialized later (Lazy initialization).
pub(crate) static ENIGO_GUARD: Mutex<Option<Enigo>> = Mutex::new(None);
static ENIGO_INIT: Once = Once::new();

static VIGEM_GUARD: Mutex<Option<Xbox360Wired<Client>>> = Mutex::new(None);
static GAMEPAD_GUARD: Mutex<Option<XGamepad>> = Mutex::new(None);
static VIGEM_INIT: Once = Once::new();

// A function to initialize Enigo exactly once.
pub fn init_enigo() {
    ENIGO_INIT.call_once(|| {
        let enigo = Enigo::new(&Settings::default()).expect("Failed to initialize Enigo");
        *ENIGO_GUARD.lock().unwrap() = Some(enigo);
        println!("Enigo initialized.");
    });
}

// A function to initialize Vigem exactly once.
pub fn init_vigem() {
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

        let gamepad = XGamepad {
            ..Default::default()
        };
        *GAMEPAD_GUARD.lock().unwrap() = Some(gamepad);

        println!("Controller is ready.");
    });
}

// Function to start the ENet server host
fn start_enet_server() -> enet::Host<UdpSocket> {
    let socket =
        UdpSocket::bind(SocketAddr::from_str(format!("0.0.0.0:{}", ENET_PORT).as_str()).unwrap())
            .unwrap();

    let host = enet::Host::new(
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

// --- The Blocking ENet Server Loop ---
pub async fn run_enet_server() -> Result<(), IoError> {
    // This will run in a dedicated blocking thread, so we can use ENet's blocking service call.
    task::spawn_blocking(|| -> () {
        let mut host = start_enet_server();
        let mut received_events = false;

        println!("Running ENet loop");

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
    GamepadButtonY = 7,
    GamepadButtonA = 8,
    GamepadButtonB = 9,
    GamepadButtonL1 = 10,
    GamepadButtonR1 = 11,
    GamepadButtonL2 = 12,
    GamepadButtonR2 = 13,
    GamepadButtonUp = 14,
    GamepadButtonDown = 15,
    GamepadButtonLeft = 16,
    GamepadButtonRight = 17,
    GamepadLeftStick = 18,
    GamepadRightStick = 19,
    GamepadButtonStart = 20,
    GamepadButtonSelect = 21,
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
            7 => Ok(InputType::GamepadButtonY),
            8 => Ok(InputType::GamepadButtonA),
            9 => Ok(InputType::GamepadButtonB),
            10 => Ok(InputType::GamepadButtonL1),
            11 => Ok(InputType::GamepadButtonR1),
            12 => Ok(InputType::GamepadButtonL2),
            13 => Ok(InputType::GamepadButtonR2),
            14 => Ok(InputType::GamepadButtonUp),
            15 => Ok(InputType::GamepadButtonDown),
            16 => Ok(InputType::GamepadButtonLeft),
            17 => Ok(InputType::GamepadButtonRight),
            18 => Ok(InputType::GamepadLeftStick),
            19 => Ok(InputType::GamepadRightStick),
            20 => Ok(InputType::GamepadButtonStart),
            21 => Ok(InputType::GamepadButtonSelect),
            _ => Err("Invalid integer for MyEnum"),
        }
    }
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

    // println!("Received packet data: {:?}", packet_data);

    // // 2. Perform the UNSAFE cast. This is only safe because we enforced
    // //    #[repr(C, packed)] and checked the size.
    // let command: InputCommand = unsafe {
    //     // Create a raw pointer to the packet data
    //     let ptr = packet_data.as_ptr() as *const InputCommand;
    //     // Dereference the pointer to get the struct
    //     ptr.read_unaligned()
    // };

    // 1. Wrap the packet data in a Cursor for sequential reading
    let mut cursor = Cursor::new(packet_data);

    // 2. Read the fields manually, enforcing Little-Endian (LE) byte order
    let command = match read_command_from_cursor(&mut cursor) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("Failed to deserialize packet with byte order: {}", e);
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

    let mut gamepad_lock = GAMEPAD_GUARD.lock().unwrap();
    let gamepad = gamepad_lock.as_mut().expect("Gamepad was not initialized!");

    let mut pressed = false;
    let mut button_to_set = None;

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
            println!("Gamepad button X");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::X);
        }
        InputType::GamepadButtonY => {
            println!("Gamepad button Y");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::Y);
        }
        InputType::GamepadButtonA => {
            println!("Gamepad button A");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::A);
        }
        InputType::GamepadButtonB => {
            println!("Gamepad button B");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::B);
        }
        InputType::GamepadButtonL1 => {
            println!("Gamepad button L1");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::LB);
        }
        InputType::GamepadButtonR1 => {
            println!("Gamepad button R1");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::RB);
        }
        InputType::GamepadButtonL2 => {
            println!("Gamepad button L1");

            gamepad.left_trigger = (x * 256.0) as u8;
        }
        InputType::GamepadButtonR2 => {
            println!("Gamepad button R2");

            gamepad.right_trigger = (x * 256.0) as u8;
        }
        InputType::GamepadButtonStart => {
            println!("Gamepad button START");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::START);
        }
        InputType::GamepadButtonSelect => {
            println!("Gamepad button SELECT");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::BACK);
        }
        InputType::GamepadButtonUp => {
            println!("Gamepad button UP");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::UP);
        }
        InputType::GamepadButtonDown => {
            // Gamepad logic needs to be implemented here
            println!("Gamepad button DOWN");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::DOWN);
        }
        InputType::GamepadButtonLeft => {
            // Gamepad logic needs to be implemented here
            println!("Gamepad button LEFT");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::LEFT);
        }
        InputType::GamepadButtonRight => {
            // Gamepad logic needs to be implemented here
            println!("Gamepad button RIGHT");

            pressed = x > 0.0;
            button_to_set = Some(vigem_client::XButtons::RIGHT);
        }
        InputType::GamepadLeftStick => {
            println!("Gamepad Left Stick ({}, {})", x, y);

            gamepad.thumb_lx = (x * 32767.0) as i16;
            gamepad.thumb_ly = (y * -32767.0) as i16;
        }
        InputType::GamepadRightStick => {
            println!("Gamepad Right Stick ({}, {})", x, y);

            gamepad.thumb_rx = (x * 32767.0) as i16;
            gamepad.thumb_ry = (y * -32767.0) as i16;
        }
    }

    if let Some(button) = button_to_set {
        if pressed {
            // Set the bit for the A button (Button is pressed)
            gamepad.buttons.raw |= button;
        } else {
            // Clear the bit for the A button (Button is released)
            gamepad.buttons.raw &= !button;
        }
    }

    let mut vigem_lock = VIGEM_GUARD.lock().unwrap();
    let vigem = vigem_lock.as_mut().expect("Vigem was not initialized!");

    // Update the target
    let result = vigem.update(&gamepad);
    if let Err(e) = result {
        eprintln!("Failed to update ViGEm target: {:?}", e);
    }
}
