#![cfg_attr(not(debug_assertions), windows_subsystem = "windows")]

use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::io::{Read, Write};
use std::net::{IpAddr, Ipv4Addr, SocketAddr, TcpStream};
use std::time::Duration;
mod general_states;

mod project_store;
mod project_commands;
mod drawing_details;

#[tauri::command]
fn list_general_states(project_path: String) -> Result<Vec<general_states::GeneralState>, String> {
    general_states::list(&project_store::database_path(std::path::Path::new(&project_path))?)
}

#[tauri::command]
fn save_general_state(project_path: String, state: general_states::GeneralState) -> Result<general_states::GeneralState, String> {
    general_states::save(&project_store::database_path(std::path::Path::new(&project_path))?, state)
}

#[tauri::command]
fn delete_general_state(project_path: String, name: String) -> Result<(), String> {
    general_states::delete(&project_store::database_path(std::path::Path::new(&project_path))?, &name)
}

const DEFAULT_IPC_PORT: u16 = 8123;

/// Evaluates a JavaScript snippet inside AutoCAD or GstarCAD via the grx-script TCP IPC server
fn eval_in_cad_ipc(port: u16, script: &str) -> Result<Value, String> {
    let addr = SocketAddr::new(IpAddr::V4(Ipv4Addr::new(127, 0, 0, 1)), port);
    let mut stream = TcpStream::connect_timeout(&addr, Duration::from_millis(1000))
        .map_err(|e| format!("CAD IPC not connected (127.0.0.1:{}): {}", port, e))?;

    let read_timeout_ms = if script.starts_with("QTO_OPEN_DWG\n") { 185000 } else { 25000 };
    let _ = stream.set_read_timeout(Some(Duration::from_millis(read_timeout_ms)));
    let _ = stream.set_write_timeout(Some(Duration::from_millis(5000)));

    stream
        .write_all(script.as_bytes())
        .map_err(|e| format!("Failed to send script to CAD IPC: {}", e))?;
    let _ = stream.flush();
    let _ = stream.shutdown(std::net::Shutdown::Write);

    let mut response = String::new();
    stream
        .read_to_string(&mut response)
        .map_err(|e| format!("Failed to read response from CAD IPC: {}", e))?;

    if response.trim().is_empty() {
        return Ok(Value::Null);
    }

    let value = serde_json::from_str::<Value>(&response)
        .map_err(|e| format!("Invalid JSON from CAD IPC: {} (raw: {})", e, response))?;
    if let Some(error) = value.get("error") {
        return Err(error.as_str().map(str::to_owned).unwrap_or_else(|| error.to_string()));
    }
    Ok(value)
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
    #[serde(default, skip_serializing_if = "Option::is_none")]
    data: Option<Value>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    properties: Option<Value>,
}

#[tauri::command]
fn cad_apply_qto_xdata(patches: Vec<EntityXdataPatch>, port: Option<u16>) -> Result<Value, String> {
    if patches.is_empty() || patches.iter().any(|patch| patch.handle.trim().is_empty()) {
        return Err("XData updates require a valid entity handle for every selected object".to_string());
    }
    let patches_json = serde_json::to_string(&patches).map_err(|e| e.to_string())?;
    let js = format!(
        "(() => {{ \
            const items = {}; \
            const updated = []; \
            for (const item of items) {{ \
                const patch = {{ hand: item.handle }}; \
                if (item.properties && typeof item.properties === 'object') {{ \
                    for (const [k, v] of Object.entries(item.properties)) {{ \
                        if (v !== undefined) patch[k] = v; \
                    }} \
                }} \
                if (item.data !== undefined) {{ \
                    patch.data = item.data; \
                }} \
                entity.set(patch); \
                updated.push(item.handle); \
            }} \
            return {{ ok: true, count: updated.length, handles: updated }}; \
        }})()",
        patches_json
    );
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[tauri::command]
fn cad_filter_entities_color(
    checked_handles: Vec<String>,
    all_handles: Vec<String>,
    is_filter_on: bool,
    port: Option<u16>,
) -> Result<Value, String> {
    let checked_json = serde_json::to_string(&checked_handles).unwrap_or_else(|_| "[]".to_string());
    let all_json = serde_json::to_string(&all_handles).unwrap_or_else(|_| "[]".to_string());
    let is_on = if is_filter_on { "true" } else { "false" };

    let js = format!(
        "(() => {{ \
            const checked = new Set({}); \
            const all = {}; \
            const filterOn = {}; \
            const darkGrey = 8; \
            let dimmed = 0; \
            let restored = 0; \
            for (const h of all) {{ \
                const ent = entity.get(h); \
                if (!ent) continue; \
                if (!filterOn) {{ \
                    if (ent.data && ent.data._clr !== undefined && ent.data._clr !== null) {{ \
                        entity.set(h, {{ clr: ent.data._clr, data: {{ _clr: null }} }}); \
                        restored++; \
                    }} \
                }} else if (checked.has(h)) {{ \
                    if (ent.data && ent.data._clr !== undefined && ent.data._clr !== null) {{ \
                        entity.set(h, {{ clr: ent.data._clr, data: {{ _clr: null }} }}); \
                        restored++; \
                    }} \
                }} else {{ \
                    const origClr = (ent.data && ent.data._clr !== undefined && ent.data._clr !== null) ? ent.data._clr : ent.clr; \
                    entity.set(h, {{ clr: darkGrey, data: {{ _clr: origClr }} }}); \
                    dimmed++; \
                }} \
            }} \
            return {{ ok: true, dimmed, restored }}; \
        }})()",
        checked_json, all_json, is_on
    );

    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[tauri::command]
fn cad_run_ss_query(
    query: String,
    handles: Option<Vec<String>>,
    port: Option<u16>,
) -> Result<Value, String> {
    let js = match handles {
        Some(ref h_list) if !h_list.is_empty() => {
            format!(
                "editor.ssQuery({{ entities: {}, query: {} }})",
                serde_json::to_string(h_list).unwrap_or_else(|_| "[]".to_string()),
                serde_json::to_string(&query).unwrap_or_default()
            )
        }
        _ => {
            format!(
                "editor.ssQuery({})",
                serde_json::to_string(&query).unwrap_or_default()
            )
        }
    };
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
        let _ = std::fs::write("qto_crash.log", format!("Panic at {:?}: {}", info.location(), msg));
    }));
    if let Err(e) = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            project_commands::list_recent_projects,
            project_commands::create_project,
            project_commands::open_project,
            project_commands::load_project,
            project_commands::list_project_drawings,
            project_commands::add_project_drawings,
            project_commands::add_open_cad_drawings,
            project_commands::remove_project_drawings,
            project_commands::open_project_drawing,
            project_commands::get_project_setting,
            project_commands::save_project_setting,
            project_commands::list_project_descriptions,
            project_commands::save_project_description,
            project_commands::delete_project_description,
            project_commands::get_drawing_details,
            project_commands::save_drawing_details,
            project_commands::add_drawing_entry,
            list_general_states,
            save_general_state,
            delete_general_state,
            cad_eval,
            cad_poll_selection,
            cad_select_layer,
            cad_select_handles,
            cad_apply_qto_xdata,
            cad_filter_entities_color,
            cad_run_ss_query,
            set_always_on_top
        ])
        .run(tauri::generate_context!()) {
        let _ = std::fs::write("qto_crash.log", format!("Builder error: {:?}", e));
    }
}
