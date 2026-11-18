use crate::drawing_details::DrawingEntry;
use rusqlite::{params, Connection, OpenFlags, OptionalExtension};
use serde::{Deserialize, Serialize};
use serde_json::Value;
use std::collections::BTreeMap;
use std::path::{Path, PathBuf};

/// A portable project keeps its complete application database in this file.
pub const DB_FILENAME: &str = "qto-project.sqlite3";
const SCHEMA_VERSION: i64 = 1;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct ProjectInfo {
    pub path: String,
    pub name: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Drawing {
    pub id: i64,
    pub path: String,
    pub name: String,
    pub short: String,
    pub latest_entry: Option<DrawingEntry>,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct Description {
    pub items: Vec<String>,
    pub short: String,
    pub full: String,
}

fn string_path(path: &Path) -> Result<String, String> {
    let text = path.to_str().ok_or("The file path is not valid Unicode")?;
    // Windows canonicalization returns an extended path prefix. Ordinary paths
    // are preferable in the UI and work with the CAD application's file APIs.
    Ok(if let Some(unc) = text.strip_prefix(r"\\?\UNC\") {
        format!(r"\\{}", unc)
    } else {
        text.strip_prefix(r"\\?\").unwrap_or(text).to_owned()
    })
}

fn canonical_directory(directory: &Path) -> Result<PathBuf, String> {
    let directory = directory.canonicalize().map_err(|error| format!("Cannot access project folder: {error}"))?;
    if !directory.is_dir() {
        return Err("Project path must be a folder".to_owned());
    }
    Ok(directory)
}

fn open_existing(directory: &Path) -> Result<(Connection, PathBuf, String), String> {
    let directory = canonical_directory(directory)?;
    let path = directory.join(DB_FILENAME);
    if !path.is_file() {
        return Err(format!("This folder does not contain a QTO project ({DB_FILENAME})"));
    }
    let db = Connection::open_with_flags(&path, OpenFlags::SQLITE_OPEN_READ_WRITE)
        .map_err(|error| format!("Cannot open project database: {error}"))?;
    db.pragma_update(None, "foreign_keys", "ON").map_err(|error| error.to_string())?;
    db.busy_timeout(std::time::Duration::from_secs(5)).map_err(|error| error.to_string())?;
    let (name, version): (String, i64) = db.query_row(
        "SELECT name, schema_version FROM project_metadata WHERE id = 1", [],
        |row| Ok((row.get(0)?, row.get(1)?)),
    ).map_err(|error| format!("Invalid QTO project database: {error}"))?;
    if version != SCHEMA_VERSION {
        return Err(format!("Unsupported QTO project version: {version}"));
    }
    if name.trim().is_empty() {
        return Err("Project name is missing from the database".to_owned());
    }
    Ok((db, directory, name))
}

/// Validate before calling a separate subsystem such as general_states.
pub fn database_path(directory: &Path) -> Result<PathBuf, String> {
    let (_, directory, _) = open_existing(directory)?;
    Ok(directory.join(DB_FILENAME))
}

pub fn create(directory: &Path, name: &str) -> Result<ProjectInfo, String> {
    let name = name.trim();
    if name.is_empty() || name.chars().count() > 100 {
        return Err("Project name must contain 1–100 characters".to_owned());
    }
    std::fs::create_dir_all(directory).map_err(|error| format!("Cannot create project folder: {error}"))?;
    let directory = canonical_directory(directory)?;
    let path = directory.join(DB_FILENAME);
    // create_new prevents accidentally overwriting any existing project/database.
    std::fs::OpenOptions::new().write(true).create_new(true).open(&path)
        .map_err(|error| format!("Cannot create project database; open an existing project instead if this folder already contains one: {error}"))?;
    let mut db = Connection::open_with_flags(&path, OpenFlags::SQLITE_OPEN_READ_WRITE)
        .map_err(|error| error.to_string())?;
    db.pragma_update(None, "foreign_keys", "ON").map_err(|error| error.to_string())?;
    let tx = db.transaction().map_err(|error| error.to_string())?;
    tx.execute_batch("
        CREATE TABLE project_metadata (
            id INTEGER PRIMARY KEY CHECK(id = 1), name TEXT NOT NULL, schema_version INTEGER NOT NULL
        );
        CREATE TABLE drawings (
            id INTEGER PRIMARY KEY, path TEXT NOT NULL, path_key TEXT NOT NULL UNIQUE, name TEXT NOT NULL
        );
        CREATE TABLE project_settings (key TEXT PRIMARY KEY NOT NULL, payload TEXT NOT NULL);
        CREATE TABLE descriptions (
            code TEXT PRIMARY KEY NOT NULL, items TEXT NOT NULL, short TEXT NOT NULL, full TEXT NOT NULL
        );
    ").map_err(|error| error.to_string())?;
    tx.execute("INSERT INTO project_metadata (id, name, schema_version) VALUES (1, ?1, ?2)",
        params![name, SCHEMA_VERSION]).map_err(|error| error.to_string())?;
    tx.commit().map_err(|error| error.to_string())?;
    Ok(ProjectInfo { path: string_path(&directory)?, name: name.to_owned() })
}

pub fn load(directory: &Path) -> Result<ProjectInfo, String> {
    let (_, directory, name) = open_existing(directory)?;
    Ok(ProjectInfo { path: string_path(&directory)?, name })
}

fn drawing_rows(db: &Connection) -> Result<Vec<Drawing>, String> {
    // Annotation tables are created only when drawing details are first opened.
    // Listing a legacy project must neither fail nor change its database.
    let (has_details, has_entries): (bool, bool) = db.query_row(
        "SELECT EXISTS(SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'drawing_details'),
                EXISTS(SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'drawing_entries')",
        [], |row| Ok((row.get(0)?, row.get(1)?)),
    ).map_err(|error| error.to_string())?;
    let (short, details_join) = if has_details {
        ("COALESCE(details.short, '')", "LEFT JOIN drawing_details AS details ON details.drawing_id = drawing.id")
    } else {
        ("''", "")
    };
    let (entry_fields, entries_join) = if has_entries {
        (
            "entry.id, entry.kind, entry.body, entry.created_at, entry.created_by",
            "LEFT JOIN drawing_entries AS entry ON entry.id = (
                SELECT id FROM drawing_entries WHERE drawing_id = drawing.id
                ORDER BY created_at DESC, id DESC LIMIT 1
            )",
        )
    } else {
        ("NULL, NULL, NULL, NULL, NULL", "")
    };
    // Fetch one latest comment or note per drawing, without loading histories.
    let sql = format!(
        "SELECT drawing.id, drawing.path, drawing.name, {short}, {entry_fields}
         FROM drawings AS drawing {details_join} {entries_join}
         ORDER BY drawing.name COLLATE NOCASE, drawing.path"
    );
    let mut stmt = db.prepare(&sql)
        .map_err(|error| error.to_string())?;
    let rows = stmt.query_map([], |row| {
        let latest_entry = match row.get::<_, Option<i64>>(4)? {
            Some(id) => Some(DrawingEntry {
                id,
                kind: row.get(5)?,
                body: row.get(6)?,
                created_at: row.get(7)?,
                created_by: row.get(8)?,
            }),
            None => None,
        };
        Ok(Drawing {
            id: row.get(0)?, path: row.get(1)?, name: row.get(2)?,
            short: row.get(3)?, latest_entry,
        })
    })
        .map_err(|error| error.to_string())?;
    rows.collect::<Result<Vec<_>, _>>().map_err(|error| error.to_string())
}

pub fn list_drawings(directory: &Path) -> Result<Vec<Drawing>, String> {
    let (db, _, _) = open_existing(directory)?;
    drawing_rows(&db)
}

pub fn add_drawings(directory: &Path, paths: &[String]) -> Result<Vec<Drawing>, String> {
    let (mut db, _, _) = open_existing(directory)?;
    // Validate the complete selection before inserting any records.
    let mut drawings = Vec::with_capacity(paths.len());
    for path in paths {
        let canonical = Path::new(path).canonicalize().map_err(|error| format!("Cannot access drawing {path}: {error}"))?;
        if !canonical.is_file() || !canonical.extension().and_then(|value| value.to_str())
            .map(|value| value.eq_ignore_ascii_case("dwg")).unwrap_or(false) {
            return Err(format!("Choose a DWG file: {path}"));
        }
        let name = canonical.file_name().and_then(|value| value.to_str()).ok_or("Invalid drawing file name")?.to_owned();
        let path = string_path(&canonical)?;
        drawings.push((path.to_lowercase(), path, name));
    }
    let tx = db.transaction().map_err(|error| error.to_string())?;
    for (key, path, name) in drawings {
        tx.execute("INSERT INTO drawings (path_key, path, name) VALUES (?1, ?2, ?3)
            ON CONFLICT(path_key) DO NOTHING", params![key, path, name]).map_err(|error| error.to_string())?;
    }
    tx.commit().map_err(|error| error.to_string())?;
    drawing_rows(&db)
}

pub fn remove_drawings(directory: &Path, ids: &[i64]) -> Result<Vec<Drawing>, String> {
    let (mut db, _, _) = open_existing(directory)?;
    let tx = db.transaction().map_err(|error| error.to_string())?;
    for id in ids {
        tx.execute("DELETE FROM drawings WHERE id = ?1", params![id]).map_err(|error| error.to_string())?;
    }
    tx.commit().map_err(|error| error.to_string())?;
    drawing_rows(&db)
}

pub fn get_setting(directory: &Path, key: &str) -> Result<Option<Value>, String> {
    let (db, _, _) = open_existing(directory)?;
    let payload: Option<String> = db.query_row("SELECT payload FROM project_settings WHERE key = ?1", params![key], |row| row.get(0))
        .optional().map_err(|error| error.to_string())?;
    payload.map(|payload| serde_json::from_str(&payload).map_err(|error| error.to_string())).transpose()
}

pub fn save_setting(directory: &Path, key: &str, value: &Value) -> Result<(), String> {
    let (db, _, _) = open_existing(directory)?;
    if key.trim().is_empty() || key.chars().count() > 200 {
        return Err("Setting key must contain 1–200 characters".to_owned());
    }
    let payload = serde_json::to_string(value).map_err(|error| error.to_string())?;
    db.execute("INSERT INTO project_settings (key, payload) VALUES (?1, ?2)
        ON CONFLICT(key) DO UPDATE SET payload = excluded.payload", params![key, payload]).map_err(|error| error.to_string())?;
    Ok(())
}

pub fn list_descriptions(directory: &Path) -> Result<BTreeMap<String, Description>, String> {
    let (db, _, _) = open_existing(directory)?;
    let mut stmt = db.prepare("SELECT code, items, short, full FROM descriptions ORDER BY code")
        .map_err(|error| error.to_string())?;
    let rows = stmt.query_map([], |row| Ok((row.get::<_, String>(0)?, row.get::<_, String>(1)?, row.get::<_, String>(2)?, row.get::<_, String>(3)?)))
        .map_err(|error| error.to_string())?;
    let mut descriptions = BTreeMap::new();
    for row in rows {
        let (code, items, short, full) = row.map_err(|error| error.to_string())?;
        descriptions.insert(code, Description { items: serde_json::from_str(&items).map_err(|error| error.to_string())?, short, full });
    }
    Ok(descriptions)
}

pub fn save_description(directory: &Path, code: &str, description: &Description) -> Result<(), String> {
    let (db, _, _) = open_existing(directory)?;
    if code.trim().is_empty() {
        return Err("Description code must not be empty".to_owned());
    }
    let items = serde_json::to_string(&description.items).map_err(|error| error.to_string())?;
    db.execute("INSERT INTO descriptions (code, items, short, full) VALUES (?1, ?2, ?3, ?4)
        ON CONFLICT(code) DO UPDATE SET items = excluded.items, short = excluded.short, full = excluded.full",
        params![code, items, description.short, description.full]).map_err(|error| error.to_string())?;
    Ok(())
}

pub fn delete_description(directory: &Path, code: &str) -> Result<(), String> {
    let (db, _, _) = open_existing(directory)?;
    db.execute("DELETE FROM descriptions WHERE code = ?1", params![code]).map_err(|error| error.to_string())?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    struct TestDirectory(PathBuf);
    impl TestDirectory {
        fn new() -> Self {
            let path = std::env::temp_dir().join(format!("qto-project-{}-{}", std::process::id(),
                std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
            std::fs::create_dir_all(&path).unwrap();
            Self(path)
        }
    }
    impl Drop for TestDirectory {
        fn drop(&mut self) { let _ = std::fs::remove_dir_all(&self.0); }
    }

    fn register_drawing(directory: &Path, name: &str) -> i64 {
        let path = directory.join(name);
        std::fs::write(&path, b"dwg test").unwrap();
        add_drawings(directory, &[path.to_string_lossy().into_owned()]).unwrap()
            .into_iter().find(|drawing| drawing.name == name).unwrap().id
    }

    #[test]
    fn project_creation_reopen_and_validation_never_overwrite_existing_database() {
        let temp = TestDirectory::new();
        assert!(load(&temp.0).is_err());
        assert!(list_drawings(&temp.0).is_err());
        assert!(!temp.0.join(DB_FILENAME).exists());
        let info = create(&temp.0, "  Hotel  ").unwrap();
        assert_eq!(info.name, "Hotel");
        assert_eq!(load(&temp.0).unwrap(), info);
        assert!(create(&temp.0, "Replacement").is_err());
        assert_eq!(load(&temp.0).unwrap().name, "Hotel");
        assert_eq!(database_path(&temp.0).unwrap().file_name().unwrap(), DB_FILENAME);
        let blank = temp.0.join("blank");
        assert!(create(&blank, "  ").is_err());
        assert!(!blank.exists());
    }

    #[test]
    fn drawing_registration_is_atomic_deduplicated_and_removal_keeps_files() {
        let temp = TestDirectory::new();
        create(&temp.0, "Project").unwrap();
        let first = temp.0.join("First.dwg");
        let second = temp.0.join("Second.DWG");
        let invalid = temp.0.join("notes.txt");
        for path in [&first, &second, &invalid] { std::fs::write(path, b"test").unwrap(); }
        let strings = |paths: &[&Path]| paths.iter().map(|path| path.to_string_lossy().into_owned()).collect::<Vec<_>>();
        assert!(add_drawings(&temp.0, &strings(&[&first, &invalid])).is_err());
        assert!(list_drawings(&temp.0).unwrap().is_empty());
        let drawings = add_drawings(&temp.0, &strings(&[&first, &second, &first])).unwrap();
        assert_eq!(drawings.len(), 2);
        assert_eq!(add_drawings(&temp.0, &strings(&[&first])).unwrap(), drawings);
        std::fs::remove_file(&second).unwrap();
        assert_eq!(list_drawings(&temp.0).unwrap().len(), 2); // Missing files remain manageable.
        let remaining = remove_drawings(&temp.0, &[drawings[0].id]).unwrap();
        assert_eq!(remaining.len(), 1);
        assert!(first.exists());
        remove_drawings(&temp.0, &[drawings[1].id, 9999]).unwrap();
        assert!(list_drawings(&temp.0).unwrap().is_empty());
    }

    #[test]
    fn drawing_summaries_allow_legacy_projects_without_annotation_tables() {
        let temp = TestDirectory::new();
        create(&temp.0, "Legacy project").unwrap();
        let id = register_drawing(&temp.0, "First.dwg");
        let drawings = list_drawings(&temp.0).unwrap();
        assert_eq!(drawings.len(), 1);
        assert_eq!(drawings[0].id, id);
        assert!(drawings[0].short.is_empty());
        assert_eq!(drawings[0].latest_entry, None);
        let serialized = serde_json::to_value(&drawings[0]).unwrap();
        assert_eq!(serialized["short"], "");
        assert!(serialized["latest_entry"].is_null());
        let (db, _, _) = open_existing(&temp.0).unwrap();
        let annotation_tables: i64 = db.query_row(
            "SELECT count(*) FROM sqlite_master WHERE type = 'table'
             AND name IN ('drawing_details', 'drawing_entries')",
            [], |row| row.get(0),
        ).unwrap();
        assert_eq!(annotation_tables, 0);
        assert_eq!(load(&temp.0).unwrap().name, "Legacy project");
    }

    #[test]
    fn drawing_summaries_reopen_with_latest_combined_entry_without_leaking_between_drawings() {
        use crate::drawing_details;
        let temp = TestDirectory::new();
        create(&temp.0, "Project").unwrap();
        let first = register_drawing(&temp.0, "First.dwg");
        let second = register_drawing(&temp.0, "Second.dwg");
        drawing_details::save(&temp.0, first, "Tile's finish", "Full description", "Heru").unwrap();
        let first_comment = drawing_details::add_entry(&temp.0, first, "comment", "Older comment", "Alice")
            .unwrap().entries[0].id;
        let note = drawing_details::add_entry(&temp.0, first, "note", "Latest\ncoordination note", "Bob")
            .unwrap().entries.into_iter().find(|entry| entry.kind == "note").unwrap().id;
        drawing_details::add_entry(&temp.0, first, "comment", "Backdated comment", "Carol").unwrap();
        {
            let (db, _, _) = open_existing(&temp.0).unwrap();
            db.execute("UPDATE drawing_entries SET created_at = '2026-01-01T00:00:00.000Z' WHERE drawing_id = ?1",
                params![first]).unwrap();
            db.execute("UPDATE drawing_entries SET created_at = '2026-01-02T00:00:00.000Z' WHERE id = ?1",
                params![first_comment]).unwrap();
            db.execute("UPDATE drawing_entries SET created_at = '2026-01-03T00:00:00.000Z' WHERE id = ?1",
                params![note]).unwrap();
        }
        let drawings = list_drawings(&temp.0).unwrap();
        let summary = drawings.iter().find(|drawing| drawing.id == first).unwrap();
        assert_eq!(summary.short, "Tile's finish");
        assert_eq!(summary.latest_entry, Some(DrawingEntry {
            id: note, kind: "note".into(), body: "Latest\ncoordination note".into(),
            created_at: "2026-01-03T00:00:00.000Z".into(), created_by: "Bob".into(),
        }));
        let untouched = drawings.iter().find(|drawing| drawing.id == second).unwrap();
        assert!(untouched.short.is_empty());
        assert_eq!(untouched.latest_entry, None);
        load(&temp.0).unwrap();
        assert_eq!(list_drawings(&temp.0).unwrap(), drawings);
        let details = drawing_details::get(&temp.0, first).unwrap();
        assert_eq!(details.full, "Full description");
        assert_eq!(details.entries.len(), 3);
        assert_eq!(summary.latest_entry.as_ref(), details.entries.first());
        assert_eq!(std::fs::read(temp.0.join("First.dwg")).unwrap(), b"dwg test");
    }

    #[test]
    fn drawing_summary_breaks_equal_entry_timestamps_by_descending_id() {
        use crate::drawing_details;
        let temp = TestDirectory::new();
        create(&temp.0, "Project").unwrap();
        let id = register_drawing(&temp.0, "First.dwg");
        drawing_details::add_entry(&temp.0, id, "comment", "First", "Alice").unwrap();
        let expected_id = drawing_details::add_entry(&temp.0, id, "note", "Second", "Bob")
            .unwrap().entries.into_iter().map(|entry| entry.id).max().unwrap();
        {
            let (db, _, _) = open_existing(&temp.0).unwrap();
            db.execute("UPDATE drawing_entries SET created_at = '2026-01-01T00:00:00.000Z' WHERE drawing_id = ?1",
                params![id]).unwrap();
        }
        let latest = list_drawings(&temp.0).unwrap().remove(0).latest_entry.unwrap();
        assert_eq!(latest.id, expected_id);
        assert_eq!(latest.kind, "note");
        assert_eq!(latest.body, "Second");
        assert_eq!(latest.created_by, "Bob");
        assert_eq!(latest.created_at, "2026-01-01T00:00:00.000Z");
        assert_eq!(drawing_details::get(&temp.0, id).unwrap().entries[0], latest);
    }

    #[test]
    fn settings_and_descriptions_are_persistent_and_isolated_per_project() {
        let temp = TestDirectory::new();
        let a = temp.0.join("A");
        let b = temp.0.join("B");
        create(&a, "A").unwrap();
        create(&b, "B").unwrap();
        let setting = serde_json::json!({ "columns": ["Floor", "Type"], "active": false });
        save_setting(&a, "sort", &setting).unwrap();
        assert_eq!(get_setting(&a, "sort").unwrap(), Some(setting));
        assert_eq!(get_setting(&b, "sort").unwrap(), None);
        save_setting(&a, "sort", &Value::Null).unwrap();
        assert_eq!(get_setting(&a, "sort").unwrap(), Some(Value::Null));
        let description = Description { items: vec!["Interior".into()], short: "Tile's finish".into(), full: "Full description".into() };
        save_description(&a, "FloorFin/CT1", &description).unwrap();
        assert_eq!(list_descriptions(&a).unwrap().get("FloorFin/CT1"), Some(&description));
        assert!(list_descriptions(&b).unwrap().is_empty());
        let replacement = Description { short: "Replacement".into(), ..description };
        save_description(&a, "FloorFin/CT1", &replacement).unwrap();
        assert_eq!(list_descriptions(&a).unwrap().len(), 1);
        assert_eq!(list_descriptions(&a).unwrap().get("FloorFin/CT1"), Some(&replacement));
        delete_description(&a, "FloorFin/CT1").unwrap();
        assert!(list_descriptions(&a).unwrap().is_empty());
    }

    #[test]
    fn general_states_share_each_project_database_and_remain_isolated() {
        use crate::general_states::{self, Action, GeneralActions, GeneralState, GeneralValues};
        let temp = TestDirectory::new();
        let a = temp.0.join("A");
        let b = temp.0.join("B");
        create(&a, "A").unwrap();
        create(&b, "B").unwrap();
        let state = |color: &str| GeneralState {
            name: "Typical".into(),
            values: GeneralValues { clr: color.into(), lyr: "Walls".into(), trp: "0".into(), visible: "true".into() },
            actions: GeneralActions { clr: Action::Update, lyr: Action::Nothing, trp: Action::Nothing, visible: Action::Update },
        };
        general_states::save(&database_path(&a).unwrap(), state("1")).unwrap();
        assert!(general_states::list(&database_path(&b).unwrap()).unwrap().is_empty());
        general_states::save(&database_path(&b).unwrap(), state("2")).unwrap();
        save_setting(&a, "activeGeneralState", &serde_json::json!("Typical")).unwrap();
        assert_eq!(general_states::list(&database_path(&a).unwrap()).unwrap(), vec![state("1")]);
        assert_eq!(general_states::list(&database_path(&b).unwrap()).unwrap(), vec![state("2")]);
        for folder in [&a, &b] {
            let path = database_path(folder).unwrap();
            let db = Connection::open(path).unwrap();
            let tables: i64 = db.query_row(
                "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name IN ('project_metadata', 'general_states', 'project_settings')",
                [], |row| row.get(0),
            ).unwrap();
            assert_eq!(tables, 3);
            assert_eq!(std::fs::read_dir(folder).unwrap().count(), 1); // No app-global or per-feature DB file.
        }
        general_states::delete(&database_path(&a).unwrap(), "Typical").unwrap();
        assert!(general_states::list(&database_path(&a).unwrap()).unwrap().is_empty());
        assert_eq!(general_states::list(&database_path(&b).unwrap()).unwrap(), vec![state("2")]);
        let unopened = temp.0.join("Unopened");
        std::fs::create_dir(&unopened).unwrap();
        assert!(database_path(&unopened).is_err());
        assert!(!unopened.join(DB_FILENAME).exists());
    }
}
