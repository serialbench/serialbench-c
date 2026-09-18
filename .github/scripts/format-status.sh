#!/usr/bin/env bash
# Exit 0 if this leg's format results are already in the data repo, 1 if not.
set -euo pipefail

FMT="$1"
C_VERSION="$2"
PLATFORM="$3"
DATA_TOKEN="${DATA_REPO_TOKEN:-}"
DATE="${DATA_RUN_DATE:-$(date -u +%Y-%m-%d)}"

[ -n "$DATA_TOKEN" ] || exit 1

TARGET="runs/${DATE}/${PLATFORM}-c-${C_VERSION}.${FMT}.yaml"
HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" \
  -H "Authorization: Bearer ${DATA_TOKEN}" \
  "https://api.github.com/repos/serialbench/data/contents/${TARGET}")

[ "$HTTP_CODE" = "200" ] && exit 0
exit 1
