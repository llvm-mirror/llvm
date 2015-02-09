;;; llvm-mode.el --- Major mode for the LLVM assembler language.

;; Maintainer:  The LLVM team, http://llvm.org/

;;; Commentary:

;; Major mode for editing LLVM IR files.

;;; Code:

(defvar llvm-mode-syntax-table nil
  "Syntax table used while in LLVM mode.")
(defvar llvm-font-lock-keywords
  (list
   ;; Comments
   '(";.*" . font-lock-comment-face)
   ;; Variables
   '("%[-a-zA-Z$\._][-a-zA-Z$\._0-9]*" . font-lock-variable-name-face)
   ;; Labels
   '("[-a-zA-Z$\._0-9]+:" . font-lock-variable-name-face)
   ;; Strings
   '("\"[^\"]+\"" . font-lock-string-face)
   ;; Unnamed variable slots
   '("%[-]?[0-9]+" . font-lock-variable-name-face)
   ;; Types
   `(,(regexp-opt '("void" "i1" "i8" "i16" "i32" "i64" "i128" "float" "double" "type" "label" "opaque") 'symbol) . font-lock-type-face)
   ;; Integer literals
   '("\\b[-]?[0-9]+\\b" . font-lock-preprocessor-face)
   ;; Floating point constants
   '("\\b[-+]?[0-9]+\.[0-9]*\([eE][-+]?[0-9]+\)?\\b" . font-lock-preprocessor-face)
   ;; Hex constants
   '("\\b0x[0-9A-Fa-f]+\\b" . font-lock-preprocessor-face)
   ;; Keywords
   `(,(regexp-opt '("begin" "end" "true" "false" "zeroinitializer" "declare"
                    "define" "global" "constant" "const" "internal" "linkonce" "linkonce_odr"
                    "weak" "weak_odr" "appending" "uninitialized" "implementation" "..."
                    "null" "undef" "to" "except" "not" "target" "endian" "little" "big"
                    "pointersize" "volatile" "fastcc" "coldcc" "cc" "personality") 'symbols) . font-lock-keyword-face)
   ;; Arithmetic and Logical Operators
   `(,(regexp-opt '("add" "sub" "mul" "sdiv" "udiv" "urem" "srem" "and" "or" "xor"
                    "setne" "seteq" "setlt" "setgt" "setle" "setge") 'symbols) . font-lock-keyword-face)
   ;; Floating-point operators
   `(,(regexp-opt '("fadd" "fsub" "fmul" "fdiv" "frem") 'symbols) . font-lock-keyword-face)
   ;; Special instructions
   `(,(regexp-opt '("phi" "tail" "call" "select" "to" "shl" "lshr" "ashr" "fcmp" "icmp" "va_arg" "landingpad") 'symbols) . font-lock-keyword-face)
   ;; Control instructions
   `(,(regexp-opt '("ret" "br" "switch" "invoke" "resume" "unwind" "unreachable" "indirectbr") 'symbols) . font-lock-keyword-face)
   ;; Memory operators
   `(,(regexp-opt '("malloc" "alloca" "free" "load" "store" "getelementptr" "fence" "cmpxchg" "atomicrmw") 'symbols) . font-lock-keyword-face)
   ;; Casts
   `(,(regexp-opt '("bitcast" "inttoptr" "ptrtoint" "trunc" "zext" "sext" "fptrunc" "fpext" "fptoui" "fptosi" "uitofp" "sitofp" "addrspacecast") 'symbols) . font-lock-keyword-face)
   ;; Vector ops
   `(,(regexp-opt '("extractelement" "insertelement" "shufflevector") 'symbols) . font-lock-keyword-face)
   ;; Aggregate ops
   `(,(regexp-opt '("extractvalue" "insertvalue") 'symbols) . font-lock-keyword-face)
   ;; Metadata types
   `(,(regexp-opt '("distinct") 'symbols) . font-lock-keyword-face)
   ;; Use-list order directives
   `(,(regexp-opt '("uselistorder" "uselistorder_bb") 'symbols) . font-lock-keyword-face)

   )
  "Syntax highlighting for LLVM."
  )

;; ---------------------- Syntax table ---------------------------
;; Shamelessly ripped from jasmin.el
;; URL: http://www.neilvandyke.org/jasmin-emacs/jasmin.el.html

(if (not llvm-mode-syntax-table)
    (progn
      (setq llvm-mode-syntax-table (make-syntax-table))
      (mapc (function (lambda (n)
                        (modify-syntax-entry (aref n 0)
                                             (aref n 1)
                                             llvm-mode-syntax-table)))
            '(
              ;; whitespace (` ')
              [?\^m " "]
              [?\f  " "]
              [?\n  " "]
              [?\t  " "]
              [?\   " "]
              ;; word constituents (`w')
              ;;[?<  "w"]
              ;;[?>  "w"]
              [?%  "w"]
              ;;[?_  "w  "]
              ;; comments
              [?\;  "< "]
              [?\n  "> "]
              ;;[?\r  "> "]
              ;;[?\^m "> "]
              ;; symbol constituents (`_')
              ;; punctuation (`.')
              ;; open paren (`(')
              ;; close paren (`)')
              ;; string quote ('"')
              [?\" "\""]))))

;; --------------------- Abbrev table -----------------------------

(defvar llvm-mode-abbrev-table nil
  "Abbrev table used while in LLVM mode.")
(define-abbrev-table 'llvm-mode-abbrev-table ())

(defvar llvm-mode-hook nil)
(defvar llvm-mode-map nil)   ; Create a mode-specific keymap.

(if (not llvm-mode-map)
    ()  ; Do not change the keymap if it is already set up.
  (setq llvm-mode-map (make-sparse-keymap))
  (define-key llvm-mode-map "\t" 'tab-to-tab-stop)
  (define-key llvm-mode-map "\es" 'center-line)
  (define-key llvm-mode-map "\eS" 'center-paragraph))

;;;###autoload
(defun llvm-mode ()
  "Major mode for editing LLVM source files.
\\{llvm-mode-map}
  Runs `llvm-mode-hook' on startup."
  (interactive)
  (kill-all-local-variables)
  (use-local-map llvm-mode-map)         ; Provides the local keymap.
  (setq major-mode 'llvm-mode)

  (make-local-variable 'font-lock-defaults)
  (setq major-mode 'llvm-mode           ; This is how describe-mode
                                        ;   finds the doc string to print.
  mode-name "LLVM"                      ; This name goes into the modeline.
  font-lock-defaults `(llvm-font-lock-keywords))

  (setq local-abbrev-table llvm-mode-abbrev-table)
  (set-syntax-table llvm-mode-syntax-table)
  (setq comment-start ";")
  (run-hooks 'llvm-mode-hook))          ; Finally, this permits the user to
                                        ;   customize the mode with a hook.

;; Associate .ll files with llvm-mode
;;;###autoload
(add-to-list 'auto-mode-alist (cons (purecopy "\\.ll\\'")  'llvm-mode))

(provide 'llvm-mode)

;;; llvm-mode.el ends here
