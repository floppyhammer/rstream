use crate::discovery::run_announcer;
use crate::gui::config::{Config, PeerManagementType};
use crate::input::{init_enigo, init_vigem, run_enet_server};
use crate::stream::{run_websocket, Peer, StreamingState, STREAMING_STATE_GUARD};
use async_std::task;
use chrono;
use chrono::Utc;
use eframe::egui;
use eframe::egui::{CollapsingHeader, DragValue, ViewportCommand, Visuals};
use eframe::glow::Context;
use egui::containers::ScrollArea;
use egui::ecolor::Color32;
use egui::widgets::TextEdit;
use futures::future;
use std::ops::RangeInclusive;
use std::os::windows::process::CommandExt;
use std::process::Command;
use std::sync::mpsc::{Receiver, Sender};
use std::sync::{mpsc, Arc, Mutex};
use std::{str, thread};

enum BuildStatus {
    None,
    Success,
    Fail,
}

/// We derive Deserialize/Serialize so we can persist app state on shutdown.
#[cfg_attr(feature = "persistence", derive(serde::Deserialize, serde::Serialize))]
#[cfg_attr(feature = "persistence", serde(default))] // if we add new fields, give them default values when deserializing old state
pub struct App {
    config: Config,

    // Extra options.
    option1_enabled: bool,
    option2_enabled: bool,

    terminal_output: String,

    build_status: BuildStatus,

    pending_cmd_count: i32,

    sender: Arc<Mutex<Sender<(String, bool)>>>,
    receiver: Receiver<(String, bool)>,
}

impl Default for App {
    fn default() -> Self {
        let mut config = Config::new();
        match config.read() {
            Ok(_) => {
                println!("Loaded config file.")
            }
            Err(_) => {
                println!("No config file found, created a new one.")
            }
        }

        let (sender, receiver) = mpsc::channel();

        let mut guard = STREAMING_STATE_GUARD.lock().unwrap();
        let streaming_state = StreamingState { peers: [].into() };
        *guard = Some(streaming_state);

        // Initialize Enigo here, guaranteeing it happens before any messages are processed.
        init_enigo();

        init_vigem();

        let ws_handle = task::spawn(run_websocket(5600));

        let enet_handle = task::spawn(run_enet_server());

        let announcer_handle = task::spawn(run_announcer());

        Self {
            config,

            option1_enabled: false,
            option2_enabled: false,

            terminal_output: String::new(),

            build_status: BuildStatus::None,

            pending_cmd_count: 0,

            sender: Arc::new(Mutex::new(sender)),
            receiver,
        }
    }
}

impl eframe::App for App {
    /// Called each time the UI needs repainting, which may be many times per second.
    /// Put your widgets into a `SidePanel`, `TopPanel`, `CentralPanel`, `Window` or `Area`.
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        let Self {
            option1_enabled,
            option2_enabled,
            terminal_output,
            build_status,
            ..
        } = self;

        if self.config.dark_mode {
            ctx.set_visuals(Visuals::dark());
        } else {
            ctx.set_visuals(Visuals::light());
        }

        egui::TopBottomPanel::top("top_panel").show(ctx, |ui| {
            // The top panel is often a good place for a menu bar:
            egui::menu::bar(ui, |ui| {
                egui::menu::menu_button(ui, "File", |ui| {
                    ui.checkbox(&mut self.config.dark_mode, "Dark Mode");

                    if ui.button("Quit").clicked() {
                        ctx.send_viewport_cmd(ViewportCommand::Close)
                    }
                });
            });
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            ScrollArea::vertical().show_viewport(ui, |ui, _| {
                ui.horizontal(|ui| {
                    ui.label("PIN");

                    ui.add_enabled(
                        false,
                        TextEdit::singleline(&mut self.config.pin).desired_width(32.0),
                    );

                    if ui.button("Regenerate").clicked() {
                        self.config.pin = crate::gui::config::generate_pin(4);
                    }

                    if ui.ui_contains_pointer() {
                        egui::show_tooltip(ui.ctx(), egui::Id::new("pin_tooltip"), |ui| {
                            ui.label("Enter this when connecting from client side.");
                        });
                    }
                });

                ui.add_space(8.0);

                ui.horizontal(|ui| {
                    ui.label(format!("Bitrate (Mbps): {}", self.config.bitrate));

                    if ui.ui_contains_pointer() {
                        egui::show_tooltip(ui.ctx(), egui::Id::new("bitrate_tooltip"), |ui| {
                            ui.label("Change bitrate from client side.");
                        });
                    }
                });

                ui.add_space(8.0);

                CollapsingHeader::new("Peer management type")
                    .default_open(true)
                    .show(ui, |ui| {
                        ui.radio_value(
                            &mut self.config.peer_management_type,
                            PeerManagementType::SinglePeer,
                            PeerManagementType::SinglePeer.to_string(),
                        );
                        ui.radio_value(
                            &mut self.config.peer_management_type,
                            PeerManagementType::MultiplePeersSingleControl,
                            PeerManagementType::MultiplePeersSingleControl.to_string(),
                        );
                        ui.radio_value(
                            &mut self.config.peer_management_type,
                            PeerManagementType::MultiplePeersMultipleControl,
                            PeerManagementType::MultiplePeersMultipleControl.to_string(),
                        );

                        // Add tooltip.
                        if ui.ui_contains_pointer() {
                            egui::show_tooltip(
                                ui.ctx(),
                                egui::Id::new("peer_management_tooltip"),
                                |ui| {
                                    ui.label("Manage peers");
                                },
                            );
                        }
                    });

                ui.add_space(8.0);

                CollapsingHeader::new("Host settings")
                    .default_open(true)
                    .show(ui, |ui| {
                        ui.checkbox(option1_enabled, "Start hosting upon app startup");
                        ui.checkbox(option2_enabled, "option2");
                    });

                ui.add_space(8.0);

                if ui.button("Host").clicked() {
                    println!("Host");
                }

                ui.add_space(8.0);

                CollapsingHeader::new("Connected Peers")
                    .default_open(true)
                    .show(ui, |ui| {
                        let mut guard = STREAMING_STATE_GUARD.lock().unwrap();

                        if let Some(state) = guard.as_mut() {
                            for p in &state.peers {
                                ui.horizontal(|ui| {
                                    ui.label(format!("(1) IP: {}", p.1.ip));
                                    ui.label(format!("Time connected: {}", p.1.time_connected));
                                    if ui.button("Disconnect").clicked() {
                                        println!("Disconnect");
                                    };
                                });
                            }
                        }
                    });

                ui.add_space(8.0);

                // The central panel the region left after adding TopPanel's and SidePanel's
                ui.heading("Logs");

                ScrollArea::vertical().show(ui, |ui| {
                    let output = TextEdit::multiline(terminal_output).interactive(true);
                    ui.add(output);
                });

                match self.receiver.try_recv() {
                    Ok(value) => {
                        let (output_string, res) = value;

                        if res {
                            // Update the count of the pending cmds.
                            self.pending_cmd_count -= 1;

                            // All cmds have finished successfully. Build succeeds.
                            if self.pending_cmd_count == 0 {
                                *build_status = BuildStatus::Success;
                            }
                        } else {
                            // If any cmd fails, the build fails.
                            // And clear pending cmds.
                            *build_status = BuildStatus::Fail;
                            self.pending_cmd_count = 0;
                        }

                        terminal_output.push_str(&output_string);
                    }
                    Err(_) => {}
                }

                match build_status {
                    BuildStatus::None => {
                        ui.colored_label(Color32::YELLOW, "");
                    }
                    BuildStatus::Success => {
                        ui.colored_label(Color32::GREEN, "Build succeeded.");
                    }
                    BuildStatus::Fail => {
                        ui.colored_label(Color32::RED, "Build failed!");
                    }
                }
            });
        });

        // Override reactive mode.
        // See https://github.com/emilk/egui/issues/1691.
        // Do not use request_repaint_after() as it causes panic when being used along with rfd.
        ctx.request_repaint();
    }

    fn on_exit(&mut self, _gl: Option<&Context>) {
        self.config
            .write()
            .expect("Failed to write the config file!");

        println!("Saved config file.");

        // // Block the main thread to keep the async runtime and the WS server alive.
        // if let (Err(e0), Err(e1)) = task::block_on(future::join(ws_handle, enet_handle)) {
        //     eprintln!("WS server task failed: {}", e0);
        //     eprintln!("WS server task failed: {}", e1);
        // }

        // Cleanup when the async task somehow exits (e.g., Ctrl+C, though this might be hard)
        // Running a final stop ensures cleanup if possible.
        crate::stream::stop_gstreamer_pipeline()
    }
}

fn run_powershell_cmd(cmd_lines: &mut Vec<String>) -> (String, bool) {
    let output = if cfg!(target_os = "windows") {
        // See https://stackoverflow.com/questions/60750113/how-do-i-hide-the-console-window-for-a-process-started-with-stdprocesscomman
        const CREATE_NO_WINDOW: u32 = 0x08000000;

        Command::new("powershell")
            .args(cmd_lines)
            .creation_flags(CREATE_NO_WINDOW)
            .output()
            .expect("failed to execute process")
    } else {
        panic!("Only Window is supported!");
    };

    let out = output.stdout;
    let err = output.stderr;

    let (output_str, res) = if err.is_empty() {
        (str::from_utf8(&out).unwrap(), true)
    } else {
        (str::from_utf8(&err).unwrap(), false)
    };

    (output_str.to_owned(), res)
}
