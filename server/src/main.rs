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
use eframe::epaint::image;
use std::env;
use std::process::id;
use std::sync::Mutex;
use tray_icon::menu::{Menu, MenuEvent, MenuItem};
use tray_icon::{Icon, MouseButton, MouseButtonState, TrayIconBuilder, TrayIconEvent, TrayIconId};
use windows::Win32::Foundation::HWND;
use windows::Win32::UI::WindowsAndMessaging::{ShowWindow, SW_HIDE, SW_SHOWDEFAULT};
use winit::raw_window_handle::{HasWindowHandle, RawWindowHandle};

#[allow(dead_code)]
const NAME: &str = env!("CARGO_PKG_NAME");
#[allow(dead_code)]
const VERSION: &str = env!("CARGO_PKG_VERSION");

static VISIBLE: Mutex<bool> = Mutex::new(true);

fn main() -> Result<(), Box<dyn std::error::Error>> {
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

    let mut app = gui::app::App::default();

    app.tray_menu_quit_id = Some(quit_id);

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
                        button: MouseButton::Left,
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

                let event_id = event.id().clone();

                // match event_id {
                //     // Compare the event's ID with the stored IDs
                //     id if id == quit_id => {
                //         println!("Quit clicked! Exiting application.");
                //         // Add your application cleanup and exit code here
                //         break;
                //     }
                //     id if id == about_id => {
                //         println!("About clicked! Showing info.");
                //         // Add logic to show an 'About' window or dialog
                //     }
                //     _ => {
                //         // Handle any other potential menu items
                //         println!("Unknown menu item clicked with ID: {:?}", event.id());
                //     }
                // }
            }));

            Box::new(app)
        }),
    );
    Ok(())
}
