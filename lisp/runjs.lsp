;;; ==========================================================================
;;;  RUNJS.LSP — AutoLISP Functions for AutoCAD JavaScript Runtime (arx01)
;;; ==========================================================================
;;;
;;;  Native Functions (from arx01.arx):
;;;    (js-eval exprOrFile)     - Evaluates code/file and RETURNS JSON string value.
;;;    (js-execute codeOrFile)  - Executes code/file with NO RETURN (void).
;;;
;;;  AutoCAD Commands:
;;;    C:JS-EVAL               - Evaluate expression/file and display JSON result.
;;;    C:JS-EXECUTE, C:RUNJS   - Pick or enter JS script/code and execute quietly.
;;; ==========================================================================

(vl-load-com)

;;; --------------------------------------------------------------------------
;;;  Helper: Detect CAD Platform
;;; --------------------------------------------------------------------------
(defun js-is-gstarcad ()
  (or
    (and (getvar "PRODUCT") (wcmatch (strcase (getvar "PRODUCT")) "*GSTAR*"))
    (and (ver) (wcmatch (strcase (ver)) "*GSTAR*"))
    (and (getvar "PROGRAM") (wcmatch (strcase (getvar "PROGRAM")) "*GCAD*"))
    (/= (vl-symbol-value 'grxload) nil)
  )
)

;;; --------------------------------------------------------------------------
;;;  Helper: Detect AutoCAD Release Year from ACADVER
;;; --------------------------------------------------------------------------
(defun js-get-cad-year ( / ver )
  (if (js-is-gstarcad)
    "2026"
    (progn
      (setq ver (getvar "ACADVER"))
      (cond
        ((wcmatch ver "21.*") "2017")
        ((wcmatch ver "22.*") "2018")
        ((wcmatch ver "23.0*") "2019")
        ((wcmatch ver "23.1*") "2020")
        ((wcmatch ver "23.*")  "2020")
        ((wcmatch ver "24.0*") "2021")
        ((wcmatch ver "24.1*") "2022")
        ((wcmatch ver "24.2*") "2023")
        ((wcmatch ver "24.3*") "2024")
        ((wcmatch ver "24.*")  "2024")
        (T "2021")
      )
    )
  )
)

;;; --------------------------------------------------------------------------
;;;  Helper: Check if any qjs / arx01 binary is already loaded
;;; --------------------------------------------------------------------------
(defun js-is-arx-loaded ( / loaded )
  (or
    (vl-some '(lambda (item) (or (wcmatch (strcase item) "*QJS*") (wcmatch (strcase item) "*QSJ*") (wcmatch (strcase item) "*ARX01*"))) (arx))
    (if (vl-symbol-value 'grx)
      (vl-some '(lambda (item) (or (wcmatch (strcase item) "*QJS*") (wcmatch (strcase item) "*QSJ*") (wcmatch (strcase item) "*ARX01*"))) (grx))
      nil
    )
  )
)

;;; --------------------------------------------------------------------------
;;;  Helper: Ensure qjs / arx01 binary is loaded in AutoCAD / GstarCAD
;;; --------------------------------------------------------------------------
(defun js-ensure-arx ( / yr lspDir cand candidates foundPath isGcad )
  (if (not (js-is-arx-loaded))
    (progn
      (setq isGcad (js-is-gstarcad))
      (setq yr (js-get-cad-year))
      ;; Determine directory of runjs.lsp if possible
      (setq lspDir (if (findfile "runjs.lsp") (vl-filename-directory (findfile "runjs.lsp")) ""))

      ;; Candidate search patterns prioritized by platform and version
      (if isGcad
        (setq candidates (list
          "qjs_2026.grx"
          "dist/gstarcad_2026/qjs_2026.grx"
          "dist/bin/qjs_2026.grx"
          "build_gstarcad_2026/bin/Release/qjs_2026.grx"
          "arx01_gstarcad_2026.grx"
          "arx01.grx"
          "qjs_2026.arx"
          "qjs.arx"
        ))
        (setq candidates (list
          (strcat "qjs_" yr ".arx")
          "qjs.arx"
          (strcat yr "/qjs_" yr ".arx")
          (strcat "dist/" yr "/qjs_" yr ".arx")
          (strcat "dist/bin/qjs_" yr ".arx")
          (strcat "build_" yr "/bin/Release/qjs_" yr ".arx")
          (strcat "build/bin/Release/qjs_" yr ".arx")
          (strcat "arx01_" yr ".arx")
          "arx01.arx"
        ))
      )

      (foreach cand candidates
        (if (not foundPath)
          (progn
            (setq foundPath (findfile cand))
            (if (not foundPath)
              (setq foundPath (findfile (strcat (getvar "DWGPREFIX") cand)))
            )
            (if (and (not foundPath) (/= lspDir ""))
              (setq foundPath (findfile (strcat lspDir "/" cand)))
            )
            (if (and (not foundPath) (/= lspDir ""))
              (setq foundPath (findfile (strcat lspDir "/../" cand)))
            )
          )
        )
      )

      (if foundPath
        (progn
          (if (and isGcad (vl-symbol-value 'grxload) (wcmatch (strcase foundPath) "*.GRX"))
            (grxload foundPath)
            (arxload foundPath)
          )
          (princ (strcat "\n[AutoLISP] Loaded " (if isGcad "GstarCAD " "AutoCAD ") yr " binary: " foundPath "\n"))
          T
        )
        (progn
          (princ (strcat "\n[AutoLISP] Warning: arx01 binary for " (if isGcad "GstarCAD " "AutoCAD ") yr " not found. Run .\\build.ps1 -Version " yr "\n"))
          nil
        )
      )
    )
    T
  )
)

;;; --------------------------------------------------------------------------
;;;  (js-eval exprOrFile)
;;;  Evaluates JS code or script file and RETURNS JSON value (string).
;;; --------------------------------------------------------------------------
;;  Note: The C++ function (js-eval ...) is registered directly by arx01.arx.
;;  This wrapper guarantees arx01 is loaded and normalizes file paths.
(defun js-eval-wrapper ( input / foundPath normalized )
  (js-ensure-arx)
  (if (and input (/= input ""))
    (progn
      ;; If input matches an existing file, pass normalized full path
      (if (setq foundPath (findfile input))
        (setq normalized (vl-string-translate "\\" "/" foundPath))
        (setq normalized input)
      )
      (if js-eval
        (js-eval normalized)
        (vl-cmdf "js:eval" normalized)
      )
    )
    nil
  )
)

;;; --------------------------------------------------------------------------
;;;  (js-execute codeOrFile)
;;;  Executes JS code or script file with NO RETURN (void).
;;; --------------------------------------------------------------------------
;;  Note: The C++ function (js-execute ...) is registered directly by arx01.arx
;;  and calls acedRetVoid().
(defun js-execute-wrapper ( input / foundPath normalized )
  (js-ensure-arx)
  (if (and input (/= input ""))
    (progn
      (if (setq foundPath (findfile input))
        (setq normalized (vl-string-translate "\\" "/" foundPath))
        (setq normalized input)
      )
      (if js-execute
        (js-execute normalized)
        (vl-cmdf "js:execute" normalized)
      )
    )
  )
  (princ)
)

;;; --------------------------------------------------------------------------
;;;  AutoCAD Command: JS-EVAL
;;; --------------------------------------------------------------------------
(defun c:js-eval ( / input jsonRes )
  (js-ensure-arx)
  (setq input (getstring T "\nJavaScript expression or script path: "))
  (if (and input (/= input ""))
    (progn
      (setq jsonRes (js-eval-wrapper input))
      (if jsonRes
        (princ (strcat "\n[JSON Result]: " jsonRes "\n"))
        (princ "\n[JS-EVAL]: null / error.\n")
      )
    )
  )
  (princ)
)

;;; --------------------------------------------------------------------------
;;;  AutoCAD Command: JS-EXECUTE (Alias: RUNJS)
;;; --------------------------------------------------------------------------
(defun c:js-execute ( / chosenFile )
  (js-ensure-arx)
  (setq chosenFile (getfiled "Select JavaScript Script" (getvar "DWGPREFIX") "js" 16))
  (if chosenFile
    (js-execute-wrapper chosenFile)
  )
  (princ)
)

(defun c:js () (c:runjs))

;;; --------------------------------------------------------------------------
;;;  AutoCAD Command: RELOAD (Alias: RL)
;;;  Unloads arx01.arx, copies latest build from project, and reloads it.
;;; --------------------------------------------------------------------------
(defun c:reload ( / isGcad yr srcCandidates src foundSrc dst tries res webviewSrc webviewDst lspDir )
  (setq isGcad (js-is-gstarcad))
  (setq yr (js-get-cad-year))
  (princ (strcat "\n[RELOAD] Initiating arx01 hot-reload for " (if isGcad "GstarCAD " "AutoCAD ") yr "..."))

  ;; 1. Unload all variations of qjs/arx01 currently loaded in AutoCAD / GstarCAD
  (foreach item (arx)
    (if (or (wcmatch (strcase item) "*QJS*") (wcmatch (strcase item) "*QSJ*") (wcmatch (strcase item) "*ARX01*"))
      (progn
        (princ (strcat "\n[RELOAD] Unloading: " item "..."))
        (vl-catch-all-apply 'arxunload (list item))
      )
    )
  )
  (if (vl-symbol-value 'grx)
    (foreach item (grx)
      (if (or (wcmatch (strcase item) "*QJS*") (wcmatch (strcase item) "*QSJ*") (wcmatch (strcase item) "*ARX01*"))
        (progn
          (princ (strcat "\n[RELOAD] Unloading (GRX): " item "..."))
          (if (vl-symbol-value 'grxunload)
            (vl-catch-all-apply 'grxunload (list item))
            (vl-catch-all-apply 'arxunload (list item))
          )
        )
      )
    )
  )

  ;; 2. Locate fresh build binary for this CAD platform and release
  (setq lspDir (if (findfile "runjs.lsp") (vl-filename-directory (findfile "runjs.lsp")) ""))
  (if isGcad
    (setq srcCandidates (list
      "qjs_2026.grx"
      "dist/gstarcad_2026/qjs_2026.grx"
      "dist/bin/qjs_2026.grx"
      "build_gstarcad_2026/bin/Release/qjs_2026.grx"
      "arx01_gstarcad_2026.grx"
      "arx01.grx"
      "qjs_2026.arx"
      "qjs.arx"
    ))
    (setq srcCandidates (list
      (strcat "qjs_" yr ".arx")
      "qjs.arx"
      (strcat yr "/qjs_" yr ".arx")
      (strcat "dist/" yr "/qjs_" yr ".arx")
      (strcat "dist/bin/qjs_" yr ".arx")
      (strcat "build_" yr "/bin/Release/qjs_" yr ".arx")
      (strcat "build/bin/Release/qjs_" yr ".arx")
      (strcat "arx01_" yr ".arx")
      "arx01.arx"
    ))
  )

  (setq foundSrc nil)
  (foreach cand srcCandidates
    (if (not foundSrc)
      (progn
        (setq foundSrc (findfile cand))
        (if (not foundSrc)
          (setq foundSrc (findfile (strcat (getvar "DWGPREFIX") cand)))
        )
        (if (and (not foundSrc) (/= lspDir ""))
          (setq foundSrc (findfile (strcat lspDir "/" cand)))
        )
        (if (and (not foundSrc) (/= lspDir ""))
          (setq foundSrc (findfile (strcat lspDir "/../" cand)))
        )
      )
    )
  )

  (if (not foundSrc)
    (princ (strcat "\n[RELOAD] Error: Fresh binary for " (if isGcad "GstarCAD " "AutoCAD ") yr " not found. Run .\\build.ps1 -Version " yr "\n"))
    (progn
      (princ (strcat "\n[RELOAD] Loading fresh binary: " foundSrc))
      (if (and isGcad (vl-symbol-value 'grxload) (wcmatch (strcase foundSrc) "*.GRX"))
        (setq res (vl-catch-all-apply 'grxload (list foundSrc)))
        (setq res (vl-catch-all-apply 'arxload (list foundSrc)))
      )
      (if (vl-catch-all-error-p res)
        (princ (strcat "\n[RELOAD] Error loading " foundSrc ": " (vl-catch-all-error-message res) "\n"))
        (princ (strcat "\n[RELOAD] Successfully reloaded arx01 (" (menucmd "M=$(edtime,$(getvar,date),HH:MM:SS)") ")!\n"))
      )
    )
  )
  (princ)
)

(defun c:rl () (c:reload))

(princ "\n[arx01] AutoLISP ready: (js-eval ...) returns JSON, (js-execute ...) no return. Type RELOAD or RL to hot-reload.\n")
(princ)

