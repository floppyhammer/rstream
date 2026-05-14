use log::debug;
use serde_json::{json, Value};
use std::fs::File;
use std::io::prelude::*;

const CONFIG_FILE: &str = "config.json";

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

pub struct AppConfig {
    pub dark_mode: bool,
    pub pin: String,
    pub auto_start: bool,
}

impl AppConfig {
    pub fn new() -> Self {
        let pin = generate_pin(4);

        Self {
            dark_mode: true,
            pin,
            auto_start: false,
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

        debug!(
            "======== Config ========\n{}\n========================",
            json_string,
        );

        self.pin = String::from(json_value["pin"].as_str().unwrap_or(""));
        self.dark_mode = json_value["dark_mode"].as_bool().unwrap_or(true);
        self.auto_start = json_value["auto_start"].as_bool().unwrap_or(false);

        Ok(())
    }

    pub fn write(&mut self) -> std::io::Result<()> {
        let json_value = json!({
            "dark_mode": self.dark_mode,
            "pin": self.pin,
            "auto_start": self.auto_start,
        });

        let json_string = serde_json::to_string_pretty(&json_value).unwrap();

        let mut file = File::create(CONFIG_FILE)?;
        file.write_all(json_string.as_ref())?;

        Ok(())
    }
}
