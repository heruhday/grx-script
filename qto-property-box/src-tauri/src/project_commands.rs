use crate::{general_states, project_store};
use project_store::{Drawing, ProjectInfo};
use serde_json::Value;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};
use tauri::Manager;

fn recents_path(app: &tauri::AppHandle) -> Result<PathBuf, String> {
    let folder = app.path().app_data_dir().map_err(|e| e.to_string())?;
    std::fs::create_dir_all(&folder).map_err(|e| e.to_string())?;
    Ok(folder.join("recent-projects.json"))
}

#[tauri::command]
pub fn list_recent_projects(app: tauri::AppHandle) -> Result<Vec<ProjectInfo>, String> {
    let path = recents_path(&app)?;
    if !path.exists() { return Ok(Vec::new()); }
    let paths: Vec<String> = serde_json::from_slice(&std::fs::read(path).map_err(|e| e.to_string())?)
        .unwrap_or_default();
    Ok(paths.into_iter().filter_map(|path| project_store::load(Path::new(&path)).ok()).collect())
}

fn remember(app: &tauri::AppHandle, project: &ProjectInfo) -> Result<(), String> {
    let mut paths: Vec<String> = list_recent_projects(app.clone())?.into_iter()
        .map(|p| p.path).filter(|p| !p.eq_ignore_ascii_case(&project.path)).collect();
    paths.insert(0, project.path.clone());
    paths.truncate(30);
    std::fs::write(recents_path(app)?, serde_json::to_vec_pretty(&paths).map_err(|e| e.to_string())?)
        .map_err(|e| e.to_string())
}

#[tauri::command]
pub async fn create_project(app: tauri::AppHandle, window: tauri::Window, name: String) -> Result<Option<Value>, String> {
    if name.trim().is_empty() || name.trim().chars().count() > 100 { return Err("Enter a project name (1–100 characters)".into()); }
    tauri::async_runtime::spawn_blocking(move || {
        let Some(folder) = rfd::FileDialog::new().set_parent(&window).set_title("Choose a folder for the new QTO project").pick_folder() else { return Ok(None); };
        let first_project = list_recent_projects(app.clone()).unwrap_or_default().is_empty();
        let project = project_store::create(&folder, &name)?;
        project_store::save_setting(&folder, "legacy_import_available", &Value::Bool(first_project))?;
        // Preserve previously saved General states in the first project only.
        let legacy = app.path().app_data_dir().map_err(|e| e.to_string())?.join("qto_states.sqlite3");
        let import_result = (|| -> Result<(), String> {
            if first_project && legacy.is_file() {
                let database = project_store::database_path(&folder)?;
                for state in general_states::list(&legacy)? { general_states::save(&database, state)?; }
            }
            Ok(())
        })();
        let warning = import_result.err().map(|error| format!("Project created, but existing General states could not be imported: {error}. The original database was kept."));
        let _ = remember(&app, &project);
        Ok(Some(serde_json::json!({ "path": project.path, "name": project.name, "warning": warning })))
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub async fn open_project(app: tauri::AppHandle, window: tauri::Window) -> Result<Option<ProjectInfo>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let Some(folder) = rfd::FileDialog::new().set_parent(&window).set_title("Open QTO project folder").pick_folder() else { return Ok(None); };
        let project = project_store::load(&folder)?;
        let _ = remember(&app, &project);
        Ok(Some(project))
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn load_project(app: tauri::AppHandle, project_path: String) -> Result<ProjectInfo, String> {
    let project = project_store::load(Path::new(&project_path))?;
    let _ = remember(&app, &project);
    Ok(project)
}

#[tauri::command]
pub fn list_project_drawings(project_path: String) -> Result<Vec<Drawing>, String> {
    project_store::list_drawings(Path::new(&project_path))
}

#[tauri::command]
pub async fn add_project_drawings(window: tauri::Window, project_path: String) -> Result<Option<Vec<Drawing>>, String> {
    tauri::async_runtime::spawn_blocking(move || {
        project_store::load(Path::new(&project_path))?;
        let Some(files) = rfd::FileDialog::new().set_parent(&window).set_title("Add DWG drawings")
            .set_directory(&project_path).add_filter("CAD drawings", &["dwg"]).pick_files() else { return Ok(None); };
        let paths: Vec<String> = files.iter().map(|p| p.to_string_lossy().into_owned()).collect();
        project_store::add_drawings(Path::new(&project_path), &paths).map(Some)
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn remove_project_drawings(project_path: String, ids: Vec<i64>) -> Result<Vec<Drawing>, String> {
    project_store::remove_drawings(Path::new(&project_path), &ids)
}

#[derive(Debug, Deserialize)]
struct CadDrawingList {
    paths: Vec<String>,
    #[serde(rename = "activePath")]
    active_path: Option<String>,
    skipped: usize,
}

#[derive(Debug, Serialize)]
pub struct AddedCadDrawings {
    drawings: Vec<Drawing>,
    added: usize,
    skipped: usize,
}

fn register_cad_drawings(directory: &Path, snapshot: CadDrawingList, active_only: bool) -> Result<AddedCadDrawings, String> {
    let existing = project_store::list_drawings(directory)?;
    let paths = if active_only {
        vec![snapshot.active_path.ok_or("No saved active DWG was found. Save the active drawing in CAD, then try again.")?]
    } else {
        if snapshot.paths.is_empty() { return Err("No saved DWGs are open in CAD. Save any untitled drawings, then try again.".into()); }
        snapshot.paths
    };
    let drawings = project_store::add_drawings(directory, &paths)?;
    Ok(AddedCadDrawings {
        added: drawings.len().saturating_sub(existing.len()), drawings,
        skipped: if active_only { 0 } else { snapshot.skipped },
    })
}

#[tauri::command]
pub async fn add_open_cad_drawings(project_path: String, active_only: bool) -> Result<AddedCadDrawings, String> {
    tauri::async_runtime::spawn_blocking(move || {
        project_store::load(Path::new(&project_path))?;
        let result = crate::eval_in_cad_ipc(crate::DEFAULT_IPC_PORT, "QTO_LIST_DWGS")
            .map_err(|error| if error.contains("ReferenceError") {
                "Load the updated QTO CAD plugin to add drawings from the open CAD session.".to_owned()
            } else { error })?;
        let snapshot = serde_json::from_value::<CadDrawingList>(result)
            .map_err(|_| "CAD did not return its drawing list. Load the updated QTO CAD plugin and retry.".to_owned())?;
        register_cad_drawings(Path::new(&project_path), snapshot, active_only)
    }).await.map_err(|error| error.to_string())?
}

#[tauri::command]
pub async fn open_project_drawing(project_path: String, drawing_id: i64) -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        let drawings = project_store::list_drawings(Path::new(&project_path))?;
        let drawing = drawings.iter().find(|d| d.id == drawing_id).ok_or("Drawing is not registered in this project")?;
        if !Path::new(&drawing.path).is_file() { return Err(format!("Drawing file was not found: {}", drawing.path)); }
        if drawing.path.contains(['\r', '\n', '\0']) { return Err("Invalid drawing path".into()); }
        let result = crate::eval_in_cad_ipc(crate::DEFAULT_IPC_PORT, &format!("QTO_OPEN_DWG\n{}", drawing.path))?;
        if result.get("ok").and_then(Value::as_bool) != Some(true) {
            return Err("CAD did not confirm opening the drawing. Load the updated QTO CAD plugin and retry.".into());
        }
        Ok(result)
    }).await.map_err(|e| e.to_string())?
}

#[tauri::command]
pub fn get_project_setting(project_path: String, key: String) -> Result<Option<Value>, String> {
    project_store::get_setting(Path::new(&project_path), &key)
}

#[tauri::command]
pub fn save_project_setting(project_path: String, key: String, value: Value) -> Result<(), String> {
    project_store::save_setting(Path::new(&project_path), &key, &value)
}

#[tauri::command]
pub fn list_project_descriptions(project_path: String) -> Result<std::collections::BTreeMap<String, project_store::Description>, String> {
    project_store::list_descriptions(Path::new(&project_path))
}

#[tauri::command]
pub fn save_project_description(project_path: String, code: String, description: project_store::Description) -> Result<(), String> {
    project_store::save_description(Path::new(&project_path), &code, &description)
}

#[tauri::command]
pub fn delete_project_description(project_path: String, code: String) -> Result<(), String> {
    project_store::delete_description(Path::new(&project_path), &code)
}

fn drawing_author() -> String {
    let user = std::env::var("USERNAME").or_else(|_| std::env::var("USER"))
        .ok().filter(|name| !name.trim().is_empty()).unwrap_or_else(|| "Local user".to_owned());
    match std::env::var("USERDOMAIN").ok().filter(|domain| !domain.trim().is_empty()) {
        Some(domain) => format!("{domain}\\{user}"),
        None => user,
    }
}

#[tauri::command]
pub fn get_drawing_details(project_path: String, drawing_id: i64) -> Result<crate::drawing_details::DrawingDetails, String> {
    crate::drawing_details::get(Path::new(&project_path), drawing_id)
}

#[tauri::command]
pub fn save_drawing_details(project_path: String, drawing_id: i64, short: String, full: String) -> Result<crate::drawing_details::DrawingDetails, String> {
    crate::drawing_details::save(Path::new(&project_path), drawing_id, &short, &full, &drawing_author())
}

#[tauri::command]
pub fn add_drawing_entry(project_path: String, drawing_id: i64, kind: String, body: String) -> Result<crate::drawing_details::DrawingDetails, String> {
    crate::drawing_details::add_entry(Path::new(&project_path), drawing_id, &kind, &body, &drawing_author())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn add_active_and_all_cad_drawings_persists_and_deduplicates() {
        let directory = std::env::temp_dir().join(format!("qto-open-cad-{}-{}", std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        project_store::create(&directory, "CAD drawings").unwrap();
        let first = directory.join("First.dwg");
        let second = directory.join("Second.dwg");
        std::fs::write(&first, b"drawing test").unwrap();
        std::fs::write(&second, b"drawing test").unwrap();
        let paths = vec![first.to_string_lossy().into_owned(), second.to_string_lossy().into_owned()];
        let snapshot = || CadDrawingList { paths: paths.clone(), active_path: Some(paths[1].clone()), skipped: 1 };
        let active = register_cad_drawings(&directory, snapshot(), true).unwrap();
        assert_eq!(active.added, 1);
        assert_eq!(active.drawings[0].name, "Second.dwg");
        assert_eq!(active.skipped, 0);
        let all = register_cad_drawings(&directory, snapshot(), false).unwrap();
        assert_eq!(all.added, 1);
        assert_eq!(all.drawings.len(), 2);
        assert_eq!(all.skipped, 1);
        assert_eq!(register_cad_drawings(&directory, snapshot(), false).unwrap().added, 0);
        let unsaved = || CadDrawingList { paths: Vec::new(), active_path: None, skipped: 1 };
        assert!(register_cad_drawings(&directory, unsaved(), true).unwrap_err().contains("Save the active drawing"));
        assert!(register_cad_drawings(&directory, unsaved(), false).unwrap_err().contains("No saved DWGs"));
        assert_eq!(project_store::list_drawings(&directory).unwrap().len(), 2);
        assert!(first.is_file() && second.is_file());
        assert!(directory.starts_with(std::env::temp_dir()));
        std::fs::remove_dir_all(directory).unwrap();
    }
}
