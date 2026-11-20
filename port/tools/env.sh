# Sourced by the tools. PORT = this checkout's port/, BTVS_MAIN = the main
# checkout (where game_files/, the generated code and the disassembly live;
# a git worktree has none of those).
PORT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
_common="$(git -C "$PORT" rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
if [ -n "$_common" ] && [ -d "$(dirname "$_common")/game_files" ]; then
    BTVS_MAIN="$(dirname "$_common")"
else
    BTVS_MAIN="$(cd "$PORT/.." && pwd)"
fi
export BTVS_MAIN
