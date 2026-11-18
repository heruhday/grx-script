use rusqlite::{params, Connection, OpenFlags, OptionalExtension, TransactionBehavior};
use serde::{Deserialize, Serialize};
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct DrawingEntry {
    pub id: i64,
    pub kind: String,
    pub body: String,
    pub created_at: String,
    pub created_by: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub struct DrawingDetails {
    pub drawing_id: i64,
    pub short: String,
    pub full: String,
    pub updated_at: Option<String>,
    pub updated_by: Option<String>,
    pub entries: Vec<DrawingEntry>,
}

fn open(directory: &Path) -> Result<Connection, String> {
    // Validation and READ_WRITE keep an unopened folder from becoming a project.
    let path = crate::project_store::database_path(directory)?;
    let db = Connection::open_with_flags(path, OpenFlags::SQLITE_OPEN_READ_WRITE)
        .map_err(|error| format!("Cannot open project database: {error}"))?;
    db.busy_timeout(std::time::Duration::from_secs(5))
        .map_err(|error| error.to_string())?;
    db.execute_batch(
        "
        PRAGMA foreign_keys = ON;
        CREATE TABLE IF NOT EXISTS drawing_details (
            drawing_id INTEGER PRIMARY KEY REFERENCES drawings(id) ON DELETE CASCADE,
            short TEXT NOT NULL CHECK(length(short) <= 500),
            full TEXT NOT NULL CHECK(length(full) <= 50000),
            updated_at TEXT NOT NULL,
            updated_by TEXT NOT NULL CHECK(length(trim(updated_by)) BETWEEN 1 AND 200)
        );
        CREATE TABLE IF NOT EXISTS drawing_entries (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            drawing_id INTEGER NOT NULL REFERENCES drawings(id) ON DELETE CASCADE,
            kind TEXT NOT NULL CHECK(kind IN ('comment', 'note')),
            body TEXT NOT NULL CHECK(length(trim(body)) BETWEEN 1 AND 20000),
            created_at TEXT NOT NULL,
            created_by TEXT NOT NULL CHECK(length(trim(created_by)) BETWEEN 1 AND 200)
        );
        CREATE INDEX IF NOT EXISTS drawing_entries_by_drawing
            ON drawing_entries(drawing_id, created_at DESC, id DESC);
    ",
    )
    .map_err(|error| error.to_string())?;
    Ok(db)
}

fn author_name(author: &str) -> Result<&str, String> {
    let author = author.trim();
    if author.is_empty() || author.chars().count() > 200 {
        return Err("Author must contain 1-200 characters".to_owned());
    }
    Ok(author)
}

fn require_drawing(db: &Connection, drawing_id: i64) -> Result<(), String> {
    let exists: bool = db
        .query_row(
            "SELECT EXISTS(SELECT 1 FROM drawings WHERE id = ?1)",
            params![drawing_id],
            |row| row.get(0),
        )
        .map_err(|error| error.to_string())?;
    if !exists {
        return Err("The selected drawing is no longer in this project".to_owned());
    }
    Ok(())
}

fn read_details(db: &Connection, drawing_id: i64) -> Result<DrawingDetails, String> {
    require_drawing(db, drawing_id)?;
    let saved = db
        .query_row(
            "SELECT short, full, updated_at, updated_by FROM drawing_details WHERE drawing_id = ?1",
            params![drawing_id],
            |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    row.get::<_, String>(1)?,
                    row.get::<_, String>(2)?,
                    row.get::<_, String>(3)?,
                ))
            },
        )
        .optional()
        .map_err(|error| error.to_string())?;
    let (short, full, updated_at, updated_by) = match saved {
        Some((short, full, at, by)) => (short, full, Some(at), Some(by)),
        None => (String::new(), String::new(), None, None),
    };
    let mut stmt = db
        .prepare(
            "SELECT id, kind, body, created_at, created_by FROM drawing_entries
         WHERE drawing_id = ?1 ORDER BY created_at DESC, id DESC",
        )
        .map_err(|error| error.to_string())?;
    let entries = stmt
        .query_map(params![drawing_id], |row| {
            Ok(DrawingEntry {
                id: row.get(0)?,
                kind: row.get(1)?,
                body: row.get(2)?,
                created_at: row.get(3)?,
                created_by: row.get(4)?,
            })
        })
        .map_err(|error| error.to_string())?
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| error.to_string())?;
    Ok(DrawingDetails {
        drawing_id,
        short,
        full,
        updated_at,
        updated_by,
        entries,
    })
}

/// Read descriptions and immutable entries for an existing drawing.
/// A drawing without saved details has empty text and no description audit data.
pub fn get(directory: &Path, drawing_id: i64) -> Result<DrawingDetails, String> {
    let mut db = open(directory)?;
    let tx = db.transaction().map_err(|error| error.to_string())?;
    let details = read_details(&tx, drawing_id)?;
    tx.commit().map_err(|error| error.to_string())?;
    Ok(details)
}

/// Replace this drawing's descriptions; entry history is retained.
pub fn save(
    directory: &Path,
    drawing_id: i64,
    short: &str,
    full: &str,
    author: &str,
) -> Result<DrawingDetails, String> {
    if short.chars().count() > 500 {
        return Err("Short description must contain at most 500 characters".to_owned());
    }
    if full.chars().count() > 50_000 {
        return Err("Full description must contain at most 50000 characters".to_owned());
    }
    let author = author_name(author)?;
    let mut db = open(directory)?;
    let tx = db
        .transaction_with_behavior(TransactionBehavior::Immediate)
        .map_err(|error| error.to_string())?;
    require_drawing(&tx, drawing_id)?;
    tx.execute(
        "INSERT INTO drawing_details (drawing_id, short, full, updated_at, updated_by)
         VALUES (?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), ?4)
         ON CONFLICT(drawing_id) DO UPDATE SET short = excluded.short, full = excluded.full,
             updated_at = excluded.updated_at, updated_by = excluded.updated_by",
        params![drawing_id, short, full, author],
    )
    .map_err(|error| error.to_string())?;
    let details = read_details(&tx, drawing_id)?;
    tx.commit().map_err(|error| error.to_string())?;
    Ok(details)
}

/// Append a comment or note. This module intentionally exposes no edit/delete API.
pub fn add_entry(
    directory: &Path,
    drawing_id: i64,
    kind: &str,
    body: &str,
    author: &str,
) -> Result<DrawingDetails, String> {
    if !matches!(kind, "comment" | "note") {
        return Err("Entry kind must be comment or note".to_owned());
    }
    let body = body.trim();
    if body.is_empty() || body.chars().count() > 20_000 {
        return Err("Entry must contain 1-20000 characters".to_owned());
    }
    let author = author_name(author)?;
    let mut db = open(directory)?;
    let tx = db
        .transaction_with_behavior(TransactionBehavior::Immediate)
        .map_err(|error| error.to_string())?;
    require_drawing(&tx, drawing_id)?;
    tx.execute(
        "INSERT INTO drawing_entries (drawing_id, kind, body, created_at, created_by)
         VALUES (?1, ?2, ?3, strftime('%Y-%m-%dT%H:%M:%fZ', 'now'), ?4)",
        params![drawing_id, kind, body, author],
    )
    .map_err(|error| error.to_string())?;
    let details = read_details(&tx, drawing_id)?;
    tx.commit().map_err(|error| error.to_string())?;
    Ok(details)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::project_store;
    use std::path::PathBuf;

    struct TestDirectory(PathBuf);

    impl TestDirectory {
        fn new() -> Self {
            let path = std::env::temp_dir().join(format!(
                "qto-drawing-details-{}-{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .unwrap()
                    .as_nanos()
            ));
            std::fs::create_dir_all(&path).unwrap();
            Self(path)
        }
    }

    impl Drop for TestDirectory {
        fn drop(&mut self) {
            let _ = std::fs::remove_dir_all(&self.0);
        }
    }

    fn register(directory: &Path, name: &str) -> i64 {
        let path = directory.join(name);
        std::fs::write(&path, b"dwg test").unwrap();
        project_store::add_drawings(directory, &[path.to_string_lossy().into_owned()])
            .unwrap()
            .into_iter()
            .find(|drawing| drawing.name == name)
            .unwrap()
            .id
    }

    fn assert_utc_timestamp(value: &str) {
        // SQLite generated RFC 3339 UTC, with millisecond precision.
        assert_eq!(value.len(), 24);
        assert_eq!(&value[10..11], "T");
        assert!(value.ends_with('Z'));
        let db = Connection::open_in_memory().unwrap();
        let parsed: Option<String> = db
            .query_row("SELECT datetime(?1)", params![value], |row| row.get(0))
            .unwrap();
        assert!(parsed.is_some());
    }

    #[test]
    fn details_reopen_and_preserve_history_with_server_audit_data() {
        let temp = TestDirectory::new();
        project_store::create(&temp.0, "Project").unwrap();
        let id = register(&temp.0, "First.dwg");
        let empty = get(&temp.0, id).unwrap();
        assert_eq!(
            empty,
            DrawingDetails {
                drawing_id: id,
                short: String::new(),
                full: String::new(),
                updated_at: None,
                updated_by: None,
                entries: vec![]
            }
        );

        let saved = save(
            &temp.0,
            id,
            " Tile's finish ",
            "Full\nmultiline description",
            "  Heru  ",
        )
        .unwrap();
        assert_eq!(saved.short, " Tile's finish ");
        assert_eq!(saved.updated_by.as_deref(), Some("Heru"));
        assert_utc_timestamp(saved.updated_at.as_deref().unwrap());
        let first = add_entry(
            &temp.0,
            id,
            "comment",
            "  Architect's comment\nwith more detail  ",
            "  Alice  ",
        )
        .unwrap();
        let second = add_entry(&temp.0, id, "note", "Coordination note", "Bob").unwrap();
        assert_eq!(second.entries.len(), 2);
        assert!(second.entries[0].id > second.entries[1].id);
        assert_eq!(second.entries[0].kind, "note");
        assert_eq!(second.entries[0].created_by, "Bob");
        assert_eq!(
            second.entries[1].body,
            "Architect's comment\nwith more detail"
        );
        assert_eq!(second.entries[1].created_by, "Alice");
        assert_eq!(second.entries[1], first.entries[0]);
        for entry in &second.entries {
            assert_utc_timestamp(&entry.created_at);
        }
        assert_eq!(second.updated_at, saved.updated_at);
        project_store::load(&temp.0).unwrap();
        assert_eq!(get(&temp.0, id).unwrap(), second);
        let replaced = save(&temp.0, id, "Replacement", "", "Carol").unwrap();
        assert_eq!(replaced.entries, second.entries);
        assert_eq!(replaced.updated_by.as_deref(), Some("Carol"));
        assert_eq!(replaced.short, "Replacement");
        assert_eq!(std::fs::read_dir(&temp.0).unwrap().count(), 2);
    }

    #[test]
    fn drawings_and_projects_are_isolated_from_each_other_and_general_descriptions() {
        let temp = TestDirectory::new();
        let a = temp.0.join("A");
        let b = temp.0.join("B");
        project_store::create(&a, "A").unwrap();
        project_store::create(&b, "B").unwrap();
        let first = register(&a, "First.dwg");
        let second = register(&a, "Second.dwg");
        let other_project = register(&b, "First.dwg");
        assert_eq!(first, other_project);
        save(&a, first, "Drawing A", "Full A", "Heru").unwrap();
        add_entry(&a, first, "note", "A's note", "Heru").unwrap();
        let general = project_store::Description {
            items: vec!["Walls".into()],
            short: "General".into(),
            full: "General full".into(),
        };
        project_store::save_description(&a, "W1", &general).unwrap();
        assert_eq!(get(&a, first).unwrap().short, "Drawing A");
        for (directory, id) in [(&a, second), (&b, other_project)] {
            let details = get(directory, id).unwrap();
            assert!(details.short.is_empty());
            assert!(details.entries.is_empty());
        }
        assert_eq!(
            project_store::list_descriptions(&a).unwrap().get("W1"),
            Some(&general)
        );
        assert!(project_store::list_descriptions(&b).unwrap().is_empty());
    }

    #[test]
    fn invalid_inputs_do_not_modify_details_or_create_orphans() {
        let temp = TestDirectory::new();
        let unopened = temp.0.join("Unopened");
        std::fs::create_dir(&unopened).unwrap();
        assert!(get(&unopened, 1).is_err());
        assert!(save(&unopened, 1, "", "", "Heru").is_err());
        assert!(add_entry(&unopened, 1, "note", "Text", "Heru").is_err());
        assert!(!unopened.join(project_store::DB_FILENAME).exists());

        project_store::create(&temp.0, "Project").unwrap();
        assert!(get(&temp.0, 1).is_err());
        assert!(save(&temp.0, 1, "", "", "Heru").is_err());
        assert!(add_entry(&temp.0, 1, "note", "Text", "Heru").is_err());
        let id = register(&temp.0, "First.dwg");
        let baseline = save(&temp.0, id, "Saved", "Full", "Heru").unwrap();
        for kind in ["", "Comment", "update", " note "] {
            assert!(add_entry(&temp.0, id, kind, "Text", "Heru").is_err());
        }
        for body in ["".to_owned(), " \r\n\t ".to_owned(), "x".repeat(20_001)] {
            assert!(add_entry(&temp.0, id, "comment", &body, "Heru").is_err());
        }
        for author in ["".to_owned(), " \t ".to_owned(), "x".repeat(201)] {
            assert!(save(&temp.0, id, "Changed", "", &author).is_err());
            assert!(add_entry(&temp.0, id, "note", "Text", &author).is_err());
        }
        assert!(save(&temp.0, id, &"x".repeat(501), "", "Heru").is_err());
        assert!(save(&temp.0, id, "", &"x".repeat(50_001), "Heru").is_err());
        assert!(get(&temp.0, id + 1).is_err());
        assert!(save(&temp.0, id + 1, "Orphan", "", "Heru").is_err());
        assert!(add_entry(&temp.0, id + 1, "note", "Orphan", "Heru").is_err());
        assert_eq!(get(&temp.0, id).unwrap(), baseline);
        let db = open(&temp.0).unwrap();
        let counts: (i64, i64) = db.query_row(
            "SELECT (SELECT count(*) FROM drawing_details), (SELECT count(*) FROM drawing_entries)",
            [], |row| Ok((row.get(0)?, row.get(1)?)),
        ).unwrap();
        assert_eq!(counts, (1, 0));
        assert_eq!(
            db.query_row("PRAGMA foreign_keys", [], |row| row.get::<_, i64>(0))
                .unwrap(),
            1
        );
    }

    #[test]
    fn limits_count_unicode_characters_and_accept_the_boundary() {
        let temp = TestDirectory::new();
        project_store::create(&temp.0, "Project").unwrap();
        let id = register(&temp.0, "First.dwg");
        let short = "\u{754c}".repeat(500);
        let full = "\u{754c}".repeat(50_000);
        let body = "\u{754c}".repeat(20_000);
        let author = "\u{754c}".repeat(200);
        let saved = save(&temp.0, id, &short, &full, &author).unwrap();
        assert_eq!(saved.short, short);
        assert_eq!(saved.full, full);
        let added = add_entry(&temp.0, id, "comment", &body, &author).unwrap();
        assert_eq!(added.entries[0].body, body);
        assert_eq!(added.entries[0].created_by, author);
    }

    #[test]
    fn deleting_and_readding_a_drawing_does_not_reuse_its_metadata() {
        let temp = TestDirectory::new();
        project_store::create(&temp.0, "Project").unwrap();
        let id = register(&temp.0, "First.dwg");
        save(
            &temp.0,
            id,
            "Old description",
            "Old full description",
            "Heru",
        )
        .unwrap();
        add_entry(&temp.0, id, "comment", "Old comment", "Heru").unwrap();
        project_store::remove_drawings(&temp.0, &[id]).unwrap();
        assert!(get(&temp.0, id).is_err());
        let reused_id = register(&temp.0, "Replacement.dwg");
        assert_eq!(reused_id, id); // SQLite reuses the only INTEGER PRIMARY KEY.
        let replacement = get(&temp.0, reused_id).unwrap();
        assert_eq!(replacement.short, "");
        assert_eq!(replacement.full, "");
        assert!(replacement.updated_at.is_none());
        assert!(replacement.updated_by.is_none());
        assert!(replacement.entries.is_empty());
    }

    #[test]
    fn entries_with_equal_timestamps_are_ordered_by_descending_id() {
        let temp = TestDirectory::new();
        project_store::create(&temp.0, "Project").unwrap();
        let id = register(&temp.0, "First.dwg");
        add_entry(&temp.0, id, "comment", "First", "Heru").unwrap();
        add_entry(&temp.0, id, "note", "Second", "Heru").unwrap();
        let db = open(&temp.0).unwrap();
        db.execute("UPDATE drawing_entries SET created_at = '2026-01-01T00:00:00.000Z' WHERE drawing_id = ?1", params![id]).unwrap();
        let details = get(&temp.0, id).unwrap();
        assert_eq!(
            details
                .entries
                .iter()
                .map(|entry| entry.body.as_str())
                .collect::<Vec<_>>(),
            vec!["Second", "First"]
        );
    }
}
