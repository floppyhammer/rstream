// #![forbid(unsafe_code)]
#![cfg_attr(not(debug_assertions), deny(warnings))] // Forbid warnings in release builds
#![warn(clippy::all, rust_2018_idioms)]
// Hide the console window.
// #![windows_subsystem = "windows"]

mod discovery;
mod gui;
mod input;
mod stream;

use eframe::egui;
use eframe::egui::{Style, Visuals};
use std::env;
use std::sync::Mutex;
use tray_icon::menu::{Menu, MenuItem};
use tray_icon::{Icon, MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{ShowWindow, SW_HIDE, SW_SHOWDEFAULT};
use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};

#[allow(dead_code)]
const NAME: &str = env!("CARGO_PKG_NAME");
#[allow(dead_code)]
const VERSION: &str = env!("CARGO_PKG_VERSION");

static VISIBLE: Mutex<bool> = Mutex::new(true);

fn main() -> Result<(), Box<dyn std::error::Error>> {
    env_logger::init();

    let args: Vec<String> = env::args().collect();
    let start_minimized = args.iter().any(|arg| arg == "--minimized");

    if start_minimized {
        let mut visible = VISIBLE.lock().unwrap();
        *visible = false;
    }

    let asset_dir = std::path::Path::new(env!("OUT_DIR")).join("assets");
    let icon = Icon::from_path(asset_dir.join("favicon.ico"), None)?;

    let quit_item = MenuItem::new("Quit", true, None);
    // Store the MenuIds for easy comparison later
    let quit_id = quit_item.id().clone();

    let tray_menu = Menu::new();
    tray_menu.append(&quit_item).unwrap();

    let _tray_icon = TrayIconBuilder::new()
        .with_icon(icon)
        .with_tooltip("RStream Client")
        .with_menu(Box::new(tray_menu))
        .build()?;

    let app = gui::app::App::default();

    let icon_image_bytes = include_bytes!("../assets/icon.png");
    let image = image::load_from_memory(icon_image_bytes)
        .unwrap()
        .to_rgba8();
    let (img_width, img_height) = image.dimensions();
    let icon_data = egui::IconData {
        rgba: image.into_raw(),
        width: img_width,
        height: img_height,
    };

    let native_options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder {
            icon: Some(std::sync::Arc::new(icon_data)),
            ..Default::default()
        }
        .with_position([200.0, 200.0])
        .with_inner_size([480.0, 360.0])
        .with_drag_and_drop(true),
        ..Default::default()
    };

    let _ = eframe::run_native(
        format!("{} - {}", "RStream Server", VERSION).as_str(),
        native_options,
        Box::new(move |cc| {
            let style = Style {
                visuals: Visuals::dark(),
                ..Style::default()
            };
            cc.egui_ctx.set_style(style);

            let RawWindowHandle::Win32(handle) = cc.window_handle().unwrap().as_raw() else {
                panic!("Unsupported platform");
            };

            let context = cc.egui_ctx.clone();
            let quit_id_cloned = quit_id.clone();
            let handle_hwnd = handle.hwnd;

            tray_icon::menu::MenuEvent::set_event_handler(Some(move |event: tray_icon::menu::MenuEvent| {
                if event.id() == &quit_id_cloned {
                    log::info!("Tray Menu Event: Quit selected. Shutting down.");

                    // Show hidden window before sending quit command
                    let window_handle = HWND(handle_hwnd.into());
                    unsafe {
                        ShowWindow(window_handle, SW_SHOWDEFAULT);
                    }
                    context.send_viewport_cmd(egui::ViewportCommand::Close);
                    context.request_repaint();
                }
            }));

            {
                let visible = VISIBLE.lock().unwrap();
                if !*visible {
                    let window_handle = HWND(handle.hwnd.into());
                    unsafe {
                        ShowWindow(window_handle, SW_HIDE);
                    }
                }
            }

            TrayIconEvent::set_event_handler(Some(move |event: TrayIconEvent| {
                match event {
                    TrayIconEvent::Click {
                        button_state: MouseButtonState::Down,
                        button: MouseButton::Left,
                        ..
                    } => {
                        let mut visible = VISIBLE.lock().unwrap();
                        let window_handle = HWND(handle.hwnd.into());

                        if *visible {
                            unsafe {
                                ShowWindow(window_handle, SW_HIDE);
                            }
                            *visible = false;
                        } else {
                            unsafe {
                                ShowWindow(window_handle, SW_SHOWDEFAULT);
                            }
                            *visible = true;
                        }
                    }
                    _ => return,
                }
            }));

            Box::new(app)
        }),
    );
    Ok(())
}
