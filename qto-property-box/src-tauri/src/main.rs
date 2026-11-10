#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::io::{Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, TcpStream};
use std::time::Duration;

const DEFAULT_IPC_PORT: u16 = 8123;

/// Evaluates a JavaScript snippet inside AutoCAD or GstarCAD via the grx-script TCP IPC server
fn eval_in_cad_ipc(port: u16, script: &str) -> Result<Value, String> {
    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), port);
    let mut stream = TcpStream::connect_timeout(&addr, Duration::from_millis(350))
        .map_err(|e| format!("CAD IPC not connected (127.0.0.1:{}): {}", port, e))?;

    let _ = stream.set_read_timeout(Some(Duration::from_millis(3000)));
    let _ = stream.set_write_timeout(Some(Duration::from_millis(1500)));

    stream
        .write_all(script.as_bytes())
        .map_err(|e| format!("Failed to send script to CAD IPC: {}", e))?;
    let _ = stream.flush();

    let mut response = String::new();
    stream
        .read_to_string(&mut response)
        .map_err(|e| format!("Failed to read response from CAD IPC: {}", e))?;

    if response.trim().is_empty() {
        return Ok(Value::Null);
    }

    serde_json::from_str::<Value>(&response)
        .map_err(|e| format!("Invalid JSON from CAD IPC: {} (raw: {})", e, response))
}

#[tauri::command]
fn cad_eval(script: String, port: Option<u16>) -> Result<Value, String> {
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &script)
}

#[tauri::command]
fn cad_poll_selection(last_version: i64, port: Option<u16>) -> Result<Value, String> {
    let js = format!("editor.qtoPoll({})", last_version);
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[tauri::command]
fn cad_select_layer(layer: String, port: Option<u16>) -> Result<Value, String> {
    let escaped = serde_json::to_string(&layer).unwrap_or_else(|_| "\"0\"".to_string());
    let js = format!(
        "(() => {{ editor.ssQuery({{ lyr: {}, implied: true }}); return editor.qtoPoll(0); }})()",
        escaped
    );
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[tauri::command]
fn cad_select_handles(handles: Vec<String>, zoom: bool, port: Option<u16>) -> Result<Value, String> {
    let handles_json = serde_json::to_string(&handles).unwrap_or_else(|_| "[]".to_string());
    let js = format!(
        "editor.qtoSelectHandles({}, {})",
        handles_json,
        if zoom { "true" } else { "false" }
    );
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[derive(Debug, Deserialize, Serialize)]
struct EntityXdataPatch {
    handle: String,
    data: Value,
}

#[tauri::command]
fn cad_apply_qto_xdata(patches: Vec<EntityXdataPatch>, port: Option<u16>) -> Result<Value, String> {
    let patches_json = serde_json::to_string(&patches).map_err(|e| e.to_string())?;
    let js = format!(
        "(() => {{ \
            const items = {}; \
            const updated = []; \
            for (const item of items) {{ \
                entity.set(item.handle, {{ data: null }}); \
                const res = entity.set(item.handle, {{ data: item.data }}); \
                if (res) updated.push(res.hand || item.handle); \
            }} \
            return {{ ok: true, count: updated.length, handles: updated }}; \
        }})()",
        patches_json
    );
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[tauri::command]
fn set_always_on_top(window: tauri::Window, always_on_top: bool) -> Result<(), String> {
    window
        .set_always_on_top(always_on_top)
        .map_err(|e| e.to_string())
}

fn main() {
    std::env::set_var("WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS", "--lang=en-US");
    std::panic::set_hook(Box::new(|info| {
        let msg = match info.payload().downcast_ref::<&'static str>() {
            Some(s) => s.to_string(),
            None => match info.payload().downcast_ref::<String>() {
                Some(s) => s.clone(),
                None => "Box<Any>".to_string(),
            },
        };
        let _ = std::fs::write("C:\\Users\\qsten\\OneDrive - PT. Roket Jaya Abadi\\Heru\\temp\\grx-script\\qto_crash.log", format!("Panic at {:?}: {}", info.location(), msg));
    }));
    if let Err(e) = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            cad_eval,
            cad_poll_selection,
            cad_select_layer,
            cad_select_handles,
            cad_apply_qto_xdata,
            set_always_on_top
        ])
        .run(tauri::generate_context!()) {
        let _ = std::fs::write("C:\\Users\\qsten\\OneDrive - PT. Roket Jaya Abadi\\Heru\\temp\\grx-script\\qto_crash.log", format!("Builder error: {:?}", e));
    }
}
