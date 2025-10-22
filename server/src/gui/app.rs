use crate::gui::config::{BuildType, Config};
use crate::input::{init_enigo, init_vigem, run_enet_server};
use crate::stream::run_websocket;
use async_std::task;
use chrono;
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

    // Render engine related.
    render_engine_enabled: bool,
    qe_sprite_enabled: bool,
    motion_tile_enabled: bool,
    cube3d_enabled: bool,
    qe_vg2d_enabled: bool,

    // Video editor related.
    video_editor_enabled: bool,
    text_draw_enabled: bool,
    animation_text_enabled: bool,
    frame_reader_enabled: bool,
    et_effect_enabled: bool,
    xml_engine_lib_enabled: bool,
    et_effect_template_utils: bool,
    et_text_utils: bool,

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

        // Initialize Enigo here, guaranteeing it happens before any messages are processed.
        init_enigo();

        init_vigem();

        let ws_handle = task::spawn(run_websocket(5600));

        let enet_handle = task::spawn(run_enet_server());

        Self {
            config,

            render_engine_enabled: false,
            qe_sprite_enabled: false,
            motion_tile_enabled: false,
            cube3d_enabled: false,
            qe_vg2d_enabled: false,

            video_editor_enabled: false,
            text_draw_enabled: false,
            animation_text_enabled: false,
            frame_reader_enabled: false,
            et_effect_enabled: false,
            xml_engine_lib_enabled: false,
            et_effect_template_utils: false,
            et_text_utils: false,

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
            video_editor_enabled,
            text_draw_enabled,
            animation_text_enabled,
            render_engine_enabled,
            qe_sprite_enabled,
            motion_tile_enabled,
            cube3d_enabled,
            qe_vg2d_enabled,
            frame_reader_enabled,
            et_effect_enabled,
            xml_engine_lib_enabled,
            et_effect_template_utils,
            et_text_utils,
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

        egui::SidePanel::left("side_panel")
            .resizable(false)
            .show(ctx, |ui| {
                ScrollArea::vertical()
                    .auto_shrink([true; 2])
                    .show_viewport(ui, |ui, _| {
                        ui.add_space(8.0);
                        ui.heading("Peer");

                        ui.add_space(8.0);

                        ui.horizontal(|ui| {
                            ui.label("(1) IP: xxxx");
                            ui.label("Last time operated: xxxx");
                            // ui.add(
                            //     DragValue::new(&mut self.config.thread_count)
                            //         .clamp_range(RangeInclusive::new(1, 16)),
                            // );
                            ui.button("Disconnect");
                        });

                        ui.add_space(8.0);

                        ui.horizontal(|ui| {
                            ui.label("Build type:");
                            ui.radio_value(&mut self.config.build_type, BuildType::All, "All");
                            ui.radio_value(&mut self.config.build_type, BuildType::Sdk, "Sdk");

                            // Add tooltip.
                            if ui.ui_contains_pointer() {
                                egui::show_tooltip(
                                    ui.ctx(),
                                    egui::Id::new("build_type_tooltip"),
                                    |ui| {
                                        ui.label("In most cases, ALL is for Android and SDK is for iOS");
                                    });
                            }
                        });

                        ui.add_space(8.0);

                        ui.label("NDK directory:");

                        ui.horizontal(|ui| {
                            if ui.text_edit_singleline(&mut self.config.ndk_dir).changed() {}

                            if ui.button("Open…").clicked() {
                                if let Some(path) = rfd::FileDialog::new().pick_folder() {
                                    self.config.ndk_dir = path.display().to_string();
                                }
                            }
                        });

                        ui.add_space(8.0);

                        ui.label("Engine directory:");

                        ui.horizontal(|ui| {
                            if ui.text_edit_singleline(&mut self.config.engine_dir).changed() {}

                            if ui.button("Open…").clicked() {
                                if let Some(path) = rfd::FileDialog::new().pick_folder() {
                                    self.config.engine_dir = path.display().to_string();
                                }
                            }
                        });

                        ui.add_space(8.0);

                        ui.label("Copy built libraries to:");

                        ui.horizontal(|ui| {
                            if ui.text_edit_singleline(&mut self.config.dst_dir).changed() {}

                            if ui.button("Open…").clicked() {
                                if let Some(path) = rfd::FileDialog::new().pick_folder() {
                                    self.config.dst_dir = path.display().to_string();
                                }
                            }
                        });

                        ui.add_space(8.0);

                        CollapsingHeader::new("Video Editor Submodules")
                            .default_open(true)
                            .show(ui, |ui| {
                                ui.checkbox(text_draw_enabled, "TextDraw");
                                ui.checkbox(animation_text_enabled, "AnimationText");
                                ui.checkbox(frame_reader_enabled, "FrameReader");
                                ui.checkbox(motion_tile_enabled, "MotionTile");
                                ui.checkbox(cube3d_enabled, "3dCube");
                                ui.checkbox(et_effect_enabled, "EtEffect");
                                ui.checkbox(xml_engine_lib_enabled, "XmlEngineLib");
                                ui.checkbox(et_effect_template_utils, "EtEffectTemplateUtils");
                                ui.checkbox(et_text_utils, "EtTextUtils");

                                // We must rebuild VideoEditor if we try to rebuild any of its submodules.
                                if *text_draw_enabled || *animation_text_enabled || *frame_reader_enabled || *motion_tile_enabled || *cube3d_enabled || *et_effect_enabled || *xml_engine_lib_enabled || *et_effect_template_utils || *et_text_utils {
                                    *video_editor_enabled = true;
                                }

                                if ui.checkbox(video_editor_enabled, "VideoEditor").changed() {
                                    // Disabling VideoEditor disables any of its submodules.
                                    if !*video_editor_enabled {
                                        *text_draw_enabled = false;
                                        *animation_text_enabled = false;
                                        *frame_reader_enabled = false;
                                        *motion_tile_enabled = false;
                                        *cube3d_enabled = false;
                                        *et_effect_enabled = false;
                                        *xml_engine_lib_enabled = false;
                                        *et_effect_template_utils = false;
                                        *et_text_utils = false;
                                    }
                                }
                            });

                        ui.add_space(8.0);

                        if ui.button("Open output folder").clicked() {
                            // Using "/" in the directory here will cause the Explorer open a wrong directory.
                            Command::new("explorer")
                                .arg(format!("{}/videoeditor/makefile/android_so/libs/arm64-v8a", self.config.engine_dir).replace("/", "\\"))
                                .spawn()
                                .unwrap();
                        }

                        ui.add_space(8.0);

                        if self.pending_cmd_count <= 0 {
                            if ui.button("Build").clicked() {
                                terminal_output.clear();

                                // Show current time.
                                terminal_output.push_str(
                                    &*(chrono::offset::Local::now().to_string() + "\n\n"),
                                );

                                *build_status = BuildStatus::None;

                                // Batches run independently in different processes.
                                let mut cmd_line_batches: Vec<Vec<String>> = Vec::new();

                                let build_line = format!("\nndk-build t={} -j{} APP_ABI=arm64-v8a", self.config.build_type.to_string(), self.config.thread_count);
                                let env_line = format!("\n$env:Path += ';{}'", self.config.ndk_dir);

                                if *qe_sprite_enabled {
                                    add_cmd_line_batch("qesprite".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                if *qe_vg2d_enabled {
                                    add_cmd_line_batch("qevg2d".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                // RenderEngine. Should be done before compiling VideoEditor's shared library.
                                if *render_engine_enabled {
                                    cmd_line_batches.push(vec!["\n'Building RenderEngine (static & shared libraries)...'".to_owned()]);

                                    // Static lib.
                                    let mut cmd_lines = Vec::new();

                                    cmd_lines.push(env_line.clone());

                                    cmd_lines.push(
                                        format!("\nSet-Location '{}/RenderEngine/makefile/android_a/jni'", self.config.engine_dir)
                                    );

                                    cmd_lines.push(build_line.clone());

                                    cmd_lines.push(
                                        format!("\nCopy-Item '../obj/local/arm64-v8a/*.a' '{}/lib/android_arm64-v8a'", self.config.engine_dir)
                                    );

                                    cmd_line_batches.push(cmd_lines);

                                    // Shared lib (with linked submodules).
                                    let mut cmd_lines = Vec::new();

                                    cmd_lines.push(env_line.clone());

                                    cmd_lines.push(
                                        format!("\nSet-Location '{}/RenderEngine/makefile/android/jni'", self.config.engine_dir)
                                    );

                                    cmd_lines.push(build_line.clone());

                                    cmd_lines.push(
                                        format!("\nCopy-Item '../libs/arm64-v8a/*.so' '{}/lib/android_arm64-v8a'", self.config.engine_dir)
                                    );

                                    cmd_line_batches.push(cmd_lines);
                                }

                                if *text_draw_enabled {
                                    add_cmd_line_batch("textdraw".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                if *animation_text_enabled {
                                    add_cmd_line_batch("animationtext".into(), &env_line, &self.config.engine_dir, &build_line, Some(self.config.build_type.clone()), &mut cmd_line_batches);
                                }

                                if *frame_reader_enabled {
                                    add_cmd_line_batch("framereader".into(), &env_line, &self.config.engine_dir, &build_line, Some(self.config.build_type.clone()), &mut cmd_line_batches);
                                }

                                if *motion_tile_enabled {
                                    add_cmd_line_batch("motion_tile".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                if *cube3d_enabled {
                                    add_cmd_line_batch("3Dcube".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                if *et_effect_enabled {
                                    add_cmd_line_batch("eteffect".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                if *xml_engine_lib_enabled {
                                    add_cmd_line_batch("xml_engine_lib".into(), &env_line, &self.config.engine_dir, &build_line, Some(self.config.build_type.clone()), &mut cmd_line_batches);
                                }

                                if *et_effect_template_utils {
                                    add_cmd_line_batch("eteffecttemplateutils".into(), &env_line, &self.config.engine_dir, &build_line, Some(self.config.build_type.clone()), &mut cmd_line_batches);
                                }

                                if *et_text_utils {
                                    add_cmd_line_batch("textutils".into(), &env_line, &self.config.engine_dir, &build_line, None, &mut cmd_line_batches);
                                }

                                // VideoEditor. Will copy the compiled RenderEngine shared library to its output folder.
                                if *video_editor_enabled {
                                    cmd_line_batches.push(vec![
                                        "\n'Building VideoEditor (shared library)...'".to_owned(),
                                    ]);

                                    let mut cmd_lines = Vec::new();

                                    cmd_lines.push(env_line.clone());

                                    cmd_lines.push(
                                        format!("\nSet-Location '{}/videoeditor/makefile/android_so/jni'", self.config.engine_dir),
                                    );

                                    cmd_lines.push(build_line.clone());

                                    cmd_line_batches.push(cmd_lines);
                                }

                                // Copy built libraries to the destination folder.
                                if !self.config.dst_dir.is_empty() {
                                    cmd_line_batches.push(vec![
                                        format!(
                                            "\nCopy-Item '{}/videoeditor/makefile/android_so/libs/arm64-v8a/*.so' '{}'", self.config.engine_dir, self.config.dst_dir
                                        )
                                    ]);
                                }

                                self.pending_cmd_count = cmd_line_batches.len() as i32;

                                if self.pending_cmd_count != 0 {
                                    let sender = self.sender.clone();

                                    thread::spawn(move || {
                                        for mut batch in cmd_line_batches {
                                            // Execute a cmd and wait.
                                            let (output_string, res) =
                                                run_powershell_cmd(&mut batch);

                                            // Send results.
                                            sender
                                                .lock()
                                                .expect("Sending result to channel failed!")
                                                .send((output_string, res))
                                                .unwrap();

                                            // If some cmd failed, stop the thread.
                                            if !res {
                                                break;
                                            }
                                        }
                                    });
                                }
                            }
                        } else {
                            let _ = ui.button("Running");
                            *build_status = BuildStatus::None;
                        }

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

        egui::CentralPanel::default().show(ctx, |ui| {
            // The central panel the region left after adding TopPanel's and SidePanel's
            ui.heading("Terminal Output");
            ui.add_space(8.0);

            ScrollArea::vertical().show(ui, |ui| {
                let output = TextEdit::multiline(terminal_output)
                    .desired_width(f32::INFINITY)
                    .interactive(true);
                ui.add(output);
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

fn add_cmd_line_batch(
    module: String,
    env_line: &String,
    engine_dir: &String,
    build_line: &String,
    build_type: Option<BuildType>,
    cmd_line_batches: &mut Vec<Vec<String>>,
) {
    let mut cmd_lines = Vec::new();

    // We have to add the ndk dir to env path for each batch.
    cmd_lines.push(env_line.clone());

    cmd_lines.push(format!(
        "\nSet-Location '{}/{}/makefile/android/jni'",
        engine_dir, module
    ));

    cmd_lines.push(build_line.clone());

    // If this module needs a build type.
    if let Some(build_type) = build_type {
        cmd_lines.push(format!(
            "\nCopy-Item '../obj/local/arm64-v8a/*.a' '{}/lib/android_arm64-v8a/{}'",
            engine_dir,
            build_type.to_string()
        ));
    } else {
        cmd_lines.push(format!(
            "\nCopy-Item '../obj/local/arm64-v8a/*.a' '{}/lib/android_arm64-v8a'",
            engine_dir
        ));
    }

    cmd_line_batches.push(vec![format!("\n'Building {}...'", module).to_owned()]);
    cmd_line_batches.push(cmd_lines);
}
