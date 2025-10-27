use async_std::task;
use chrono::Utc;
use std::io;
use std::io::Error as IoError;
use std::net::{Ipv4Addr, UdpSocket};
use std::thread;
use std::time::Duration;

const BROADCAST_PORT: u16 = 55555;
// Standard broadcast address for the local network.
const BROADCAST_ADDRESS: Ipv4Addr = Ipv4Addr::new(192, 168, 3, 255);
const ANNOUNCE_INTERVAL_SECONDS: u64 = 2;
const DISCOVERY_MESSAGE: &str = "GAME_STREAM_SERVER:5600";

pub(crate) async fn run_announcer() -> Result<(), IoError> {
    task::spawn_blocking(|| -> io::Result<()> {
        // 1. Create a UDP socket and bind it to a local address (0.0.0.0 for all interfaces)
        // We bind to 0.0.0.0 and port 0, letting the OS choose a free port.
        let socket = UdpSocket::bind("0.0.0.0:0")?;

        // 2. Enable broadcast functionality
        // This is required to send packets to 255.255.255.255
        socket.set_broadcast(true)?;

        println!("Game Stream Server Announcer Started.");
        println!(
            "Sending: '{}' every {} seconds to {}:{}",
            DISCOVERY_MESSAGE, ANNOUNCE_INTERVAL_SECONDS, BROADCAST_ADDRESS, BROADCAST_PORT
        );

        let broadcast_target = (BROADCAST_ADDRESS, BROADCAST_PORT);
        let message_bytes = DISCOVERY_MESSAGE.as_bytes();

        loop {
            // 3. Send the broadcast packet
            match socket.send_to(message_bytes, broadcast_target) {
                Ok(bytes_sent) => {
                    let now_utc = Utc::now();
                    // println!("[{}] Sent {} bytes.", now_utc, DISCOVERY_MESSAGE);
                }
                Err(e) => {
                    eprintln!("Error sending broadcast: {}", e);
                }
            }

            // Wait before sending the next announcement
            thread::sleep(Duration::from_secs(ANNOUNCE_INTERVAL_SECONDS));
        }
    })
    .await
    .expect("TODO: panic message");

    Ok(())
}
