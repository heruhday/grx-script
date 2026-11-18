# QTO projects

The QTO application starts at the project list. Use **New Project** to enter a name
and choose its folder, or **Open Project Folder** to open an existing project.
Recent projects can be opened directly from the list.

Each project stores its database in `qto-project.sqlite3` inside the chosen folder.
It contains the drawing list, General states, descriptions, report queries and
scope, workspace preferences and sort levels, and saved QTO schedules. General
states and descriptions can be saved without a CAD connection. The first new
project imports previously saved General states; existing report queries are
imported on its first visit to Report. The original legacy copies are kept.

Use **Drawings** in the project workspace to open Drawing Manager:

- **Add Files** accepts one or multiple DWG files and ignores duplicates.
- **Add Active DWG** registers the active saved drawing from the connected CAD
  session. **Add All Open DWGs** registers all saved drawings open in that session.
  Untitled or unavailable files are skipped; save them in CAD before adding them.
- Check multiple drawings and use **Remove Selected** to remove their project
  references. The source DWG files are not deleted.
- Double-click a drawing, press Enter on its row, or select one and use
  **Open in CAD** to open it in the connected CAD session. Already-open drawings
  are activated instead of opened again. Finish any active CAD command first.
- Use **Desc** on a drawing row for its Short Description and Description,
  plus append-only Comments and Notes. Saves and entries record the local
  Windows user and database-generated UTC timestamp; the dialog displays WIB
  times. These details live in the project SQLite database and do not modify
  the DWG. Removing a drawing from the project removes its associated details.
  Rows show the short description above the filename and preview the latest
  comment or note with its author and WIB time. Saving details updates that
  row immediately; the preview is also restored when the manager is reopened.

The drawing manager registers existing file paths; it does not copy DWGs into the
project folder. If a DWG is moved, remove its old entry and add the new path.
Keep the project database when moving or backing up project data.

After updating, load the matching updated CAD plugin. The DWG-open request uses
native application context and must not be sent through an older plugin. The
AutoCAD 2021 build is `dist/2021/qjs_2021.arx`; install it beside the matching
`qto-property-box.exe`, then restart CAD before running **QTO**.

Startup scripts should load the plugin once rather than run **RELOAD**
automatically. `lisp/qto-startup.lsp` provides an idempotent AutoCAD 2021 loader.
The IPC bridge/server starts when **QTO** or **js:server** is requested, so
loading the plugin at CAD startup does not start background server threads.
