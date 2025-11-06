use async_std::task;
use chrono::Utc;
use local_ip_address::local_ip;
use std::io;
use std::io::Error as IoError;
use std::net::{Ipv4Addr, UdpSocket};
use std::thread;
use std::time::Duration;

const BROADCAST_PORT: u16 = 55555;
// Standard broadcast address for the local network.
const BROADCAST_ADDRESS: Ipv4Addr = Ipv4Addr::new(255, 255, 255, 255);
const ANNOUNCE_INTERVAL_SECONDS: u64 = 2;

pub(crate) async fn run_announcer() -> Result<(), IoError> {
    task::spawn_blocking(|| -> io::Result<()> {
        let my_local_ip = local_ip().expect("Error getting local IP");
        let ip_str = my_local_ip.to_string();
        println!("Local IP address: {:?}", ip_str);

        // 1. Create a UDP socket and bind it to a local address (0.0.0.0 for all interfaces)
        // We bind to 0.0.0.0 and port 0, letting the OS choose a free port.
        let socket = UdpSocket::bind(format!("{}:0", ip_str))?;

        // 2. Enable broadcast functionality
        // This is required to send packets to 255.255.255.255
        socket.set_broadcast(true)?;

        let broadcast_target = (BROADCAST_ADDRESS, BROADCAST_PORT);

        let hostname = gethostname::gethostname();
        let message = format!("{}:5600", hostname.to_str().unwrap());

        println!("Game Stream Server Announcer Started.");
        println!(
            "Sending: '{}' every {} seconds to {}:{}",
            message, ANNOUNCE_INTERVAL_SECONDS, BROADCAST_ADDRESS, BROADCAST_PORT
        );

        let message_bytes = message.as_bytes();

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
