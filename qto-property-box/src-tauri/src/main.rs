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
    let mut stream = TcpStream::connect_timeout(&addr, Duration::from_millis(1000))
        .map_err(|e| format!("CAD IPC not connected (127.0.0.1:{}): {}", port, e))?;

    let _ = stream.set_read_timeout(Some(Duration::from_millis(25000)));
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
                entity.set(item.handle, {{ data: item.data }}); \
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
                "editor.ssQuery({{ handles: {}, query: {} }})",
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
fn cad_get_descriptions(port: Option<u16>) -> Result<Value, String> {
    let js = r#"(() => {
        try {
            const { DatabaseSync } = require('sqlite');
            const dbPath = 'C:/Users/Heruh/source/arx-script/grx-script/qto_descriptions.db';
            const db = new DatabaseSync(dbPath);
            db.exec(`CREATE TABLE IF NOT EXISTS deskripsi (
                code TEXT PRIMARY KEY,
                items TEXT,
                short TEXT,
                full TEXT
            );`);
            const rows = db.prepare('SELECT * FROM deskripsi').all();
            const map = {};
            for (const r of rows) {
                let itemsArr = [];
                try {
                    itemsArr = JSON.parse(r.items);
                } catch {
                    itemsArr = r.items ? [r.items] : [];
                }
                map[r.code] = {
                    items: itemsArr,
                    short: r.short || '',
                    full: r.full || ''
                };
            }
            return map;
        } catch (e) {
            return { error: String(e) };
        }
    })()"#;
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), js)
}

#[tauri::command]
fn cad_save_description(
    code: String,
    items: Vec<String>,
    short: String,
    full: String,
    port: Option<u16>,
) -> Result<Value, String> {
    let items_json = serde_json::to_string(&items).unwrap_or_else(|_| "[]".to_string());
    let js = format!(
        r#"(() => {{
            try {{
                const {{ DatabaseSync }} = require('sqlite');
                const dbPath = 'C:/Users/Heruh/source/arx-script/grx-script/qto_descriptions.db';
                const db = new DatabaseSync(dbPath);
                db.exec(`CREATE TABLE IF NOT EXISTS deskripsi (
                    code TEXT PRIMARY KEY,
                    items TEXT,
                    short TEXT,
                    full TEXT
                );`);
                const stmt = db.prepare('INSERT OR REPLACE INTO deskripsi (code, items, short, full) VALUES (?, ?, ?, ?)');
                stmt.run({}, {}, {}, {});
                return {{ ok: true }};
            }} catch (e) {{
                return {{ error: String(e) }};
            }}
        }})()"#,
        serde_json::to_string(&code).unwrap(),
        serde_json::to_string(&items_json).unwrap(),
        serde_json::to_string(&short).unwrap(),
        serde_json::to_string(&full).unwrap()
    );
    eval_in_cad_ipc(port.unwrap_or(DEFAULT_IPC_PORT), &js)
}

#[tauri::command]
fn cad_delete_description(code: String, port: Option<u16>) -> Result<Value, String> {
    let js = format!(
        r#"(() => {{
            try {{
                const {{ DatabaseSync }} = require('sqlite');
                const dbPath = 'C:/Users/Heruh/source/arx-script/grx-script/qto_descriptions.db';
                const db = new DatabaseSync(dbPath);
                const stmt = db.prepare('DELETE FROM deskripsi WHERE code = ?');
                stmt.run({});
                return {{ ok: true }};
            }} catch (e) {{
                return {{ error: String(e) }};
            }}
        }})()"#,
        serde_json::to_string(&code).unwrap()
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
        let _ = std::fs::write("qto_crash.log", format!("Panic at {:?}: {}", info.location(), msg));
    }));
    if let Err(e) = tauri::Builder::default()
        .invoke_handler(tauri::generate_handler![
            cad_eval,
            cad_poll_selection,
            cad_select_layer,
            cad_select_handles,
            cad_apply_qto_xdata,
            cad_filter_entities_color,
            cad_run_ss_query,
            cad_get_descriptions,
            cad_save_description,
            cad_delete_description,
            set_always_on_top
        ])
        .run(tauri::generate_context!()) {
        let _ = std::fs::write("qto_crash.log", format!("Builder error: {:?}", e));
    }
}
