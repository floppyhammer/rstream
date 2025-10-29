use serde_json::{json, Value};
use std::fs::File;
use std::io::prelude::*;

const CONFIG_FILE: &str = "config.json";

const DEFAULT_BITRATE: u32 = 8;

use rand::Rng;

pub(crate) fn generate_pin(length: usize) -> String {
    let mut rng = rand::thread_rng();
    let mut pin = String::new();

    for _ in 0..length {
        let digit = rng.gen_range(0..=9); // Generates a digit between 0 and 9 (inclusive)
        pin.push_str(&digit.to_string());
    }
    pin
}

#[derive(PartialEq, Clone)]
pub enum PeerManagementType {
    SinglePeer,
    MultiplePeersSingleControl,
    MultiplePeersMultipleControl,
}

impl std::fmt::Display for PeerManagementType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            PeerManagementType::SinglePeer => write!(f, "SinglePeer"),
            PeerManagementType::MultiplePeersSingleControl => {
                write!(f, "MultiplePeersSingleControl")
            }
            PeerManagementType::MultiplePeersMultipleControl => {
                write!(f, "MultiplePeersMultipleControl")
            }
        }
    }
}

impl PeerManagementType {
    fn from_u32(value: u32) -> PeerManagementType {
        match value {
            0 => PeerManagementType::SinglePeer,
            1 => PeerManagementType::MultiplePeersSingleControl,
            2 => PeerManagementType::MultiplePeersMultipleControl,
            _ => panic!("Unknown value: {}", value),
        }
    }

    fn to_u32(&self) -> u32 {
        match self {
            PeerManagementType::SinglePeer => 0,
            PeerManagementType::MultiplePeersSingleControl => 1,
            PeerManagementType::MultiplePeersMultipleControl => 2,
        }
    }
}

pub struct Config {
    pub bitrate: u32,
    pub peer_management_type: PeerManagementType,
    pub pin: String,
    pub dark_mode: bool,
}

impl Config {
    pub fn new() -> Self {
        let peer_management_type = PeerManagementType::SinglePeer;
        let pin = generate_pin(4);

        Self {
            bitrate: DEFAULT_BITRATE,
            peer_management_type,
            pin,
            dark_mode: true,
        }
    }

    pub fn read(&mut self) -> std::io::Result<()> {
        let mut file = File::open(CONFIG_FILE)?;

        let mut contents = String::new();
        file.read_to_string(&mut contents)?;

        // Parse the string of data into serde_json::Value.
        let json_value: Value = serde_json::from_str(&*contents)?;

        // Beautify json string.
        let json_string = serde_json::to_string_pretty(&json_value).unwrap();

        println!(
            "======== Config ========\n{}\n========================",
            json_string,
        );

        self.peer_management_type = PeerManagementType::from_u32(
            json_value["peer_management_type"].as_u64().unwrap_or(0) as u32,
        );
        self.bitrate = json_value["bitrate"]
            .as_u64()
            .unwrap_or(DEFAULT_BITRATE as u64) as u32;
        self.pin = String::from(json_value["pin"].as_str().unwrap_or(""));
        self.dark_mode = json_value["dark_mode"].as_bool().unwrap_or(true);

        Ok(())
    }

    pub fn write(&mut self) -> std::io::Result<()> {
        let json_value = json!({
            "bitrate": self.bitrate,
            "peer_management_type": self.peer_management_type.to_u32(),
            "dark_mode": self.dark_mode,
            "pin": self.pin,
        });

        let json_string = serde_json::to_string_pretty(&json_value).unwrap();

        let mut file = File::create(CONFIG_FILE)?;
        file.write_all(json_string.as_ref())?;

        Ok(())
    }
}
