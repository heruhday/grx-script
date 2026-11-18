use rusqlite::{params, Connection};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GeneralValues {
    pub clr: String,
    pub lyr: String,
    pub trp: String,
    pub visible: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
#[serde(rename_all = "lowercase")]
pub enum Action { Nothing, Update, Remove }

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GeneralActions {
    pub clr: Action,
    pub lyr: Action,
    pub trp: Action,
    pub visible: Action,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct GeneralState {
    pub name: String,
    pub values: GeneralValues,
    pub actions: GeneralActions,
}

fn open(path: &Path) -> Result<Connection, String> {
    let db = Connection::open(path).map_err(|error| error.to_string())?;
    db.busy_timeout(std::time::Duration::from_secs(5)).map_err(|error| error.to_string())?;
    db.execute_batch("CREATE TABLE IF NOT EXISTS general_states (
        name TEXT PRIMARY KEY COLLATE NOCASE NOT NULL,
        payload TEXT NOT NULL
    );").map_err(|error| error.to_string())?;
    Ok(db)
}

pub fn list(path: &Path) -> Result<Vec<GeneralState>, String> {
    let db = open(path)?;
    let mut stmt = db.prepare("SELECT payload FROM general_states ORDER BY name COLLATE NOCASE")
        .map_err(|error| error.to_string())?;
    let rows = stmt.query_map([], |row| row.get::<_, String>(0)).map_err(|error| error.to_string())?;
    rows.map(|row| {
        serde_json::from_str(&row.map_err(|error| error.to_string())?).map_err(|error| error.to_string())
    }).collect()
}

pub fn save(path: &Path, mut state: GeneralState) -> Result<GeneralState, String> {
    state.name = state.name.trim().to_owned();
    if state.name.is_empty() || state.name.chars().count() > 100 {
        return Err("State name must contain 1–100 characters".to_owned());
    }
    let payload = serde_json::to_string(&state).map_err(|error| error.to_string())?;
    open(path)?.execute("INSERT INTO general_states (name, payload) VALUES (?1, ?2)
        ON CONFLICT(name) DO UPDATE SET name = excluded.name, payload = excluded.payload",
        params![state.name, payload]).map_err(|error| error.to_string())?;
    Ok(state)
}

pub fn delete(path: &Path, name: &str) -> Result<(), String> {
    open(path)?.execute("DELETE FROM general_states WHERE name = ?1", params![name])
        .map_err(|error| error.to_string())?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn states_persist_replace_and_delete_without_changing_other_states() {
        let path = std::env::temp_dir().join(format!("qto-state-{}-{}.sqlite3", std::process::id(),
            std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        let make_state = |name: &str| GeneralState {
            name: name.to_owned(),
            values: GeneralValues { clr: "#FF0000".into(), lyr: "Walls' Finish".into(), trp: "50".into(), visible: "false".into() },
            actions: GeneralActions { clr: Action::Update, lyr: Action::Nothing, trp: Action::Remove, visible: Action::Update },
        };
        assert!(list(&path).unwrap().is_empty());
        let first = save(&path, make_state("  Walls  ")).unwrap();
        assert_eq!(first.name, "Walls");
        assert_eq!(list(&path).unwrap(), vec![first]);
        save(&path, make_state("Doors")).unwrap();
        let mut replacement = make_state("walls");
        replacement.values.visible = "true".into();
        save(&path, replacement.clone()).unwrap();
        let saved = list(&path).unwrap();
        assert_eq!(saved.len(), 2);
        assert_eq!(saved[0].name, "Doors");
        assert_eq!(saved[1], replacement);
        assert!(save(&path, make_state("  ")).is_err());
        delete(&path, "WALLS").unwrap();
        assert_eq!(list(&path).unwrap().len(), 1);
        std::fs::remove_file(path).unwrap();
    }
}
