#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${1:-docs}"

if ! command -v mmdc >/dev/null 2>&1; then
  echo "Error: mmdc (mermaid-cli) is not installed or not in PATH." >&2
  exit 1
fi

PUPPETEER_CFG="$(mktemp /tmp/puppeteer-config.XXXXXX.json)"
trap 'rm -f "${PUPPETEER_CFG}"' EXIT

cat > "${PUPPETEER_CFG}" <<'EOF'
{
  "args": ["--no-sandbox", "--disable-setuid-sandbox"]
}
EOF

find "${ROOT_DIR}" -type f -name '*.mmd' -print0 | while IFS= read -r -d '' mmd_file; do
  svg_file="${mmd_file%.mmd}.svg"
  echo "Rendering ${mmd_file} -> ${svg_file}"
  mmdc -p "${PUPPETEER_CFG}" -i "${mmd_file}" -o "${svg_file}"
done
