use serde_json::{json, Value};
use std::fs::File;
use std::io::prelude::*;

const CONFIG_FILE: &str = "config.json";

#[derive(PartialEq, Clone)]
pub enum BuildType {
    All,
    Sdk,
}

impl std::fmt::Display for BuildType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            BuildType::All => write!(f, "all"),
            BuildType::Sdk => write!(f, "sdk"),
        }
    }
}

impl BuildType {
    fn from_u32(value: u32) -> BuildType {
        match value {
            0 => BuildType::All,
            1 => BuildType::Sdk,
            _ => panic!("Unknown value: {}", value),
        }
    }

    fn to_u32(&self) -> u32 {
        match self {
            BuildType::All => 0,
            BuildType::Sdk => 1,
        }
    }
}

pub struct Config {
    pub thread_count: u32,
    pub build_type: BuildType,
    pub ndk_dir: String,
    pub engine_dir: String,
    pub dst_dir: String,
    pub dark_mode: bool,
}

impl Config {
    pub fn new() -> Self {
        let build_type = BuildType::All;
        let ndk_dir = "D:/Env/android-ndk-r21e".to_string();
        let engine_dir = "D:/Dev/QuVideo/ces_adk".to_string();
        let dst_dir = String::new();

        Self {
            thread_count: 0,
            build_type,
            ndk_dir,
            engine_dir,
            dst_dir,
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

        self.build_type =
            BuildType::from_u32(json_value["build_type"].as_u64().unwrap_or(0) as u32);
        self.thread_count = json_value["thread_count"].as_u64().unwrap_or(4) as u32;
        self.ndk_dir = String::from(json_value["ndk_dir"].as_str().unwrap_or(""));
        self.engine_dir = String::from(json_value["engine_dir"].as_str().unwrap_or(""));
        self.dst_dir = String::from(json_value["dst_dir"].as_str().unwrap_or(""));
        self.dark_mode = json_value["dark_mode"].as_bool().unwrap_or(true);

        Ok(())
    }

    pub fn write(&mut self) -> std::io::Result<()> {
        let json_value = json!({
            "thread_count": self.thread_count,
            "build_type": self.build_type.to_u32(),
            "dark_mode": self.dark_mode,
            "ndk_dir": self.ndk_dir,
            "engine_dir": self.engine_dir,
            "dst_dir": self.dst_dir,
        });

        let json_string = serde_json::to_string_pretty(&json_value).unwrap();

        let mut file = File::create(CONFIG_FILE)?;
        file.write_all(json_string.as_ref())?;

        Ok(())
    }
}
