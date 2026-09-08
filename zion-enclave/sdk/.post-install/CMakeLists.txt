install(CODE "message(\"\n * Zion SDK has been installed at ${out_dir}\")")
install(CODE "message(\" * Use `make uninstall` to uninstall\")")
install(CODE "message(\" * Please add the following to your shell's start-up file (e.g., $HOME/.bashrc)\")")
install(CODE "message(\"     export ZION_SDK_DIR=${out_dir}\")")
