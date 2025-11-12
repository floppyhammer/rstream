use crate::discovery::run_announcer;
use crate::gui::config::{Config, PeerManagementType};
use crate::input::{init_enigo, init_vigem, run_enet_server};
use crate::stream::{run_websocket, StreamingState, STREAMING_STATE_GUARD};
use async_std::task;
use eframe::egui;
use eframe::egui::{CollapsingHeader, ViewportCommand, Visuals};
use eframe::glow::Context;
use egui::containers::ScrollArea;
use egui::ecolor::Color32;
use egui::widgets::TextEdit;
use local_ip_address::list_afinet_netifas;
use std::sync::mpsc::{Receiver, Sender};
use std::sync::{mpsc, Arc, Mutex};
use tray_icon::menu::{MenuEvent, MenuId};

enum BuildStatus {
    None,
    Success,
    Fail,
}

pub struct App {
    config: Config,

    // Extra options.
    option1_enabled: bool,
    option2_enabled: bool,

    terminal_output: String,

    build_status: BuildStatus,

    pending_cmd_count: i32,

    _sender: Arc<Mutex<Sender<(String, bool)>>>,
    receiver: Receiver<(String, bool)>,

    pub(crate) tray_menu_quit_id: Option<MenuId>,
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

        {
            let mut guard = STREAMING_STATE_GUARD.lock().unwrap();
            let streaming_state = StreamingState {
                peers: [].into(),
                dpi_scale: 1.0,
                native_resolution: (1920, 1080),
                stream_config: None,
            };
            *guard = Some(streaming_state);
        }

        // Initialize Enigo here, guaranteeing it happens before any messages are processed.
        init_enigo();

        init_vigem();

        let _ws_handle = task::spawn(run_websocket(5600));

        let _enet_handle = task::spawn(run_enet_server());

        let network_interfaces = list_afinet_netifas().unwrap();

        for (_name, ip) in network_interfaces.iter() {
            if ip.is_ipv4() {
                let local_ip = ip.to_string();
                if local_ip.starts_with("192.168.") || local_ip.starts_with("10.11.") {
                    let _announcer_handle = task::spawn(run_announcer(local_ip));
                }
            }
        }

        Self {
            config,

            option1_enabled: false,
            option2_enabled: false,

            terminal_output: String::new(),

            build_status: BuildStatus::None,

            pending_cmd_count: 0,

            _sender: Arc::new(Mutex::new(sender)),
            receiver,
            tray_menu_quit_id: None,
        }
    }
}

fn get_scale_factor(ctx: &egui::Context) -> f32 {
    // The `input` method provides read-only access to the current InputState.
    ctx.input(|i| {
        // The pixels_per_point field is part of the InputState.
        i.pixels_per_point
    })
}

// fn get_window_logical_resolution(ctx: &egui::Context) -> egui::Vec2 {
//     ctx.input(|i| {
//         // The screen_rect is the full size of the viewport/window in logical points.
//         i.screen_rect.size()
//     })
// }

impl eframe::App for App {
    /// Called each time the UI needs repainting, which may be many times per second.
    /// Put your widgets into a `SidePanel`, `TopPanel`, `CentralPanel`, `Window` or `Area`.
    fn update(&mut self, ctx: &egui::Context, _frame: &mut eframe::Frame) {
        {
            let scale_factor = get_scale_factor(ctx);
            if let Some(mut monitor_logical_size) = ctx.input(|i| i.viewport().monitor_size) {
                let mut state_lock = STREAMING_STATE_GUARD.lock().unwrap();
                let state = state_lock
                    .as_mut()
                    .expect("Streaming state was not initialized!");
                monitor_logical_size *= scale_factor;

                state.dpi_scale = scale_factor;
                state.native_resolution =
                    (monitor_logical_size.x as u32, monitor_logical_size.y as u32);
            }
        }

        let menu_channel = MenuEvent::receiver();
        // Use try_recv() for non-blocking check
        if let Ok(event) = menu_channel.try_recv() {
            // Handle the menu click event

            // event.id() returns a reference (&MenuId), so we compare it to a reference
            match event.id() {
                id if id == self.tray_menu_quit_id.as_ref().unwrap() => {
                    println!("Tray Menu Event: Quit selected. Shutting down.");
                    ctx.send_viewport_cmd(egui::ViewportCommand::Close);
                }
                &_ => {}
            }
        }

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

                CollapsingHeader::new("Stream Info")
                    .default_open(true)
                    .show(ui, |ui| {
                        ui.vertical(|ui| {
                            let guard = STREAMING_STATE_GUARD.lock().unwrap();
                            if let Some(state) = guard.as_ref() {
                                if let Some(config) = state.stream_config.as_ref() {
                                    ui.label(format!(
                                        "Resolution: {}x{}",
                                        config.resolution.0, config.resolution.1
                                    ));
                                    ui.label(format!("Framerate (Hz): {}", config.framerate));
                                    ui.label(format!("Bitrate (Mbps): {}", config.bitrate));
                                } else {
                                    ui.label("Disconnected");
                                }
                            }
                        });
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
                        ui.checkbox(option1_enabled, "option1");
                        ui.checkbox(option2_enabled, "option2");
                    });

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

                // ui.add_space(8.0);

                // The central panel the region left after adding TopPanel's and SidePanel's
                // ui.heading("Logs");
                //
                // ScrollArea::vertical().show(ui, |ui| {
                //     let output = TextEdit::multiline(terminal_output).interactive(true);
                //     ui.add(output);
                // });

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
