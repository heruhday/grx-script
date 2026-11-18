# QTO reference UI

`qto_property_box.html` is the unmodified UI supplied from
`Heru/QTO/qto_property_box.html`. It owns the complete interface, including
projects, settings, General, Location, drawings, categories, measurement editors,
WBS, and reports.

`npm run build` and `npm run dev` generate the application's `index.html` using
`scripts/import-reference-ui.mjs`. The generator replaces the Tailwind CDN with
locally compiled Tailwind and exposes service hooks for `src/desktopBridge.js`.
The bridge connects the reference's controls to the existing Tauri backend and
stores measurement data and UI state in the selected project database.

`src/main.jsx` loads only that adapter. The former React UI and its styles are
not mounted, imported, or included in the active application bundle.

Run `npm run test:reference` to verify rendered markup parity with the supplied
HTML and exercise native project opening, cancellation, data isolation,
measurement/UI persistence, and drawing description preservation.
