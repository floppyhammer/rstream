use crate::discovery::run_announcer;
use crate::gui::config::AppConfig;
use crate::input::{init_enigo, run_enet_server};
use crate::stream::{run_websocket, ConnectionStatus, StreamingState, STREAMING_STATE_GUARD};
use async_std::task;
use eframe::egui;
use eframe::egui::{CollapsingHeader, RichText, ViewportCommand, Visuals};
use eframe::glow::Context;
use egui::containers::ScrollArea;
use egui::ecolor::Color32;
use egui::widgets::TextEdit;
use local_ip_address::list_afinet_netifas;
use log::{error, info};
use std::process::Command;

pub struct App {
    config: AppConfig,
}

impl Default for App {
    fn default() -> Self {
        let mut config = AppConfig::new();
        match config.read() {
            Ok(_) => {
                info!("Loaded config file.")
            }
            Err(_) => {
                info!("No config file found, created a new one.")
            }
        }

        {
            let mut guard = STREAMING_STATE_GUARD.lock().unwrap();
            let streaming_state = StreamingState {
                peers: [].into(),
                dpi_scale: 1.0,
                native_resolution: (1920, 1080),
                stream_config: None,
                connection_status: ConnectionStatus::Ready,
                pin: config.pin.clone(),
            };
            *guard = Some(streaming_state);
        }

        // Initialize Enigo here, guaranteeing it happens before any messages are processed.
        init_enigo();

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
        // let close_requested = ctx.input(|i| i.viewport().close_requested());
        // if close_requested {
        //     let mut visible = VISIBLE.lock().unwrap();
        //
        //     if *visible {
        //         ctx.send_viewport_cmd(egui::ViewportCommand::CancelClose);
        //         ctx.send_viewport_cmd(egui::ViewportCommand::Visible(false));
        //         *visible = false;
        //     }
        // }

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

                    if ui.checkbox(&mut self.config.auto_start, "Auto Start").changed() {
                        if let Err(e) = set_auto_start(self.config.auto_start) {
                            error!("Failed to set auto start: {}", e);
                        }
                    }

                    if ui.button("Quit").clicked() {
                        ctx.send_viewport_cmd(ViewportCommand::Close)
                    }
                });
            });
        });

        egui::CentralPanel::default().show(ctx, |ui| {
            ScrollArea::vertical().show_viewport(ui, |ui, _| {
                let mut connection_status = ConnectionStatus::Error;
                {
                    let guard = STREAMING_STATE_GUARD.lock().unwrap();
                    if let Some(state) = guard.as_ref() {
                        connection_status = state.connection_status;
                    }
                }

                let styled_label;
                match connection_status {
                    ConnectionStatus::Ready => {
                        let label_text = RichText::new("READY");
                        styled_label = label_text.color(Color32::YELLOW);
                    }
                    ConnectionStatus::Connected => {
                        let label_text = RichText::new("CONNECTED");
                        styled_label = label_text.color(Color32::GREEN);
                    }
                    ConnectionStatus::Error => {
                        let label_text = RichText::new("ERROR");
                        styled_label = label_text.color(Color32::RED);
                    }
                }

                let styled_label = styled_label.size(24.0).strong();
                ui.label(styled_label);

                ui.horizontal(|ui| {
                    ui.label("PIN");

                    ui.add_enabled(
                        false,
                        TextEdit::singleline(&mut self.config.pin).desired_width(32.0),
                    );

                    let enable_pin_change;

                    match connection_status {
                        ConnectionStatus::Ready => {
                            enable_pin_change = true;
                        }
                        ConnectionStatus::Connected => {
                            enable_pin_change = false;
                        }
                        ConnectionStatus::Error => {
                            enable_pin_change = true;
                        }
                    }

                    let button_response =
                        ui.add_enabled(enable_pin_change, egui::Button::new("Regenerate"));

                    if button_response.clicked() {
                        self.config.pin = crate::gui::config::generate_pin(4);

                        {
                            let mut state_lock = STREAMING_STATE_GUARD.lock().unwrap();
                            let state = state_lock
                                .as_mut()
                                .expect("Streaming state was not initialized!");

                            state.pin = self.config.pin.clone();
                        }
                    }

                    if ui.ui_contains_pointer() {
                        egui::show_tooltip(ui.ctx(), egui::Id::new("pin_tooltip"), |ui| {
                            ui.label("Enter this when connecting from client side.");
                        });
                    }
                });
                //
                // ui.add_space(8.0);
                //
                // CollapsingHeader::new("Peer management type")
                //     .default_open(true)
                //     .show(ui, |ui| {
                //         ui.radio_value(
                //             &mut self.config.peer_management_type,
                //             PeerManagementType::SinglePeer,
                //             PeerManagementType::SinglePeer.to_string(),
                //         );
                //         ui.radio_value(
                //             &mut self.config.peer_management_type,
                //             PeerManagementType::MultiplePeersSingleControl,
                //             PeerManagementType::MultiplePeersSingleControl.to_string(),
                //         );
                //         ui.radio_value(
                //             &mut self.config.peer_management_type,
                //             PeerManagementType::MultiplePeersMultipleControl,
                //             PeerManagementType::MultiplePeersMultipleControl.to_string(),
                //         );
                //
                //         // Add tooltip.
                //         if ui.ui_contains_pointer() {
                //             egui::show_tooltip(
                //                 ui.ctx(),
                //                 egui::Id::new("peer_management_tooltip"),
                //                 |ui| {
                //                     ui.label("Manage peers");
                //                 },
                //             );
                //         }
                //     });

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
                                    ui.label("Not Available");
                                }
                            }
                        });
                    });

                ui.add_space(8.0);

                CollapsingHeader::new("Client Info")
                    .default_open(true)
                    .show(ui, |ui| {
                        let mut guard = STREAMING_STATE_GUARD.lock().unwrap();

                        if let Some(state) = guard.as_mut() {
                            if state.peers.is_empty() {
                                ui.label("Not Available");
                            }

                            for p in &state.peers {
                                ui.horizontal(|ui| {
                                    if ui.button("Disconnect").clicked() {
                                        println!("Disconnect");
                                    };
                                    ui.label(format!(
                                        "(1) {} connected at: {}",
                                        p.1.ip, p.1.time_connected
                                    ));
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

        info!("Saved config file.");

        // // Block the main thread to keep the async runtime and the WS server alive.
        // if let (Err(e0), Err(e1)) = task::block_on(future::join(ws_handle, enet_handle)) {
        //     eprintln!("WS server task failed: {}", e0);
        //     eprintln!("WS server task failed: {}", e1);
        // }

        // Cleanup when the async task somehow exits (e.g., Ctrl+C, though this might be hard)
        // Running a final stop ensures cleanup if possible.
        crate::input::deinit_vigem();
        crate::stream::stop_gstreamer_pipeline()
    }
}

fn set_auto_start(enabled: bool) -> std::io::Result<()> {
    let app_name = "RStreamServer";
    if enabled {
        let exe_path = std::env::current_exe()?;
        let exe_path_str = exe_path
            .to_str()
            .ok_or(std::io::Error::new(
                std::io::ErrorKind::Other,
                "Invalid exe path",
            ))?;
        Command::new("reg")
            .args(&[
                "add",
                "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                "/v",
                app_name,
                "/t",
                "REG_SZ",
                "/d",
                &format!("\"{}\" --minimized", exe_path_str),
                "/f",
            ])
            .output()?;
        info!("Auto-start enabled.");
    } else {
        let _ = Command::new("reg")
            .args(&[
                "delete",
                "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                "/v",
                app_name,
                "/f",
            ])
            .output();
        info!("Auto-start disabled.");
    }
    Ok(())
}
