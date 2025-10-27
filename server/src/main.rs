// #![forbid(unsafe_code)]
#![cfg_attr(not(debug_assertions), deny(warnings))] // Forbid warnings in release builds
#![warn(clippy::all, rust_2018_idioms)]
// Hide the console window.
// #![windows_subsystem = "windows"]

mod discovery;
mod gui;
mod input;
mod stream;

use eframe::egui::{Style, Visuals};
use std::sync::Mutex;
use tray_icon::{Icon, MouseButtonState, TrayIconBuilder, TrayIconEvent};
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{ShowWindow, SW_HIDE, SW_SHOWDEFAULT};
use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};

#[allow(dead_code)]
const NAME: &str = env!("CARGO_PKG_NAME");
#[allow(dead_code)]
const VERSION: &str = env!("CARGO_PKG_VERSION");

static VISIBLE: Mutex<bool> = Mutex::new(true);

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut icon_data: Vec<u8> = Vec::with_capacity(16 * 16 * 4);
    for _ in 0..256 {
        // all red
        icon_data.extend_from_slice(&[255, 0, 0, 255]);
    }
    let icon = Icon::from_rgba(icon_data, 16, 16)?;
    let _tray_icon = TrayIconBuilder::new()
        .with_icon(icon)
        .with_tooltip("My App")
        .build()?;

    let app = gui::app::App::default();

    let native_options = eframe::NativeOptions {
        viewport: eframe::egui::ViewportBuilder::default()
            .with_position([200.0, 200.0])
            .with_inner_size([240.0, 480.0])
            .with_drag_and_drop(true),
        ..Default::default()
    };

    let _ = eframe::run_native(
        format!("{} - {}", "RStream Server", VERSION).as_str(),
        native_options,
        Box::new(|cc| {
            let style = Style {
                visuals: Visuals::dark(),
                ..Style::default()
            };
            cc.egui_ctx.set_style(style);

            let RawWindowHandle::Win32(handle) = cc.window_handle().unwrap().as_raw() else {
                panic!("Unsupported platform");
            };

            // let context = cc.egui_ctx.clone();

            TrayIconEvent::set_event_handler(Some(move |event: TrayIconEvent| {
                // println!("TrayIconEvent: {:?}", event);

                match event {
                    TrayIconEvent::Click {
                        button_state: MouseButtonState::Down,
                        ..
                    } => {
                        let mut visible = VISIBLE.lock().unwrap();

                        if *visible {
                            let window_handle = HWND(handle.hwnd.into());
                            unsafe {
                                ShowWindow(window_handle, SW_HIDE);
                            }
                            *visible = false;
                        } else {
                            let window_handle = HWND(handle.hwnd.into());
                            unsafe {
                                ShowWindow(window_handle, SW_SHOWDEFAULT);
                            }
                            *visible = true;
                        }

                        // context.request_repaint();
                    }
                    _ => return,
                }
            }));

            Box::new(app)
        }),
    );
    Ok(())
}
