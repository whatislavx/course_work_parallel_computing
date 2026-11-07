#!/bin/sh
# Завантажує модель під час запуску (AD-06, TCK-004). Назва моделі — плейсхолдер до рішення OQ-13.
set -eu
: "${OLLAMA_MODEL:?OLLAMA_MODEL не задано}"
export OLLAMA_HOST="${OLLAMA_HOST:-http://ollama:11434}"

until ollama list >/dev/null 2>&1; do
    echo "waiting for ollama at ${OLLAMA_HOST}..."
    sleep 2
done

if ollama list | awk 'NR>1 {print $1}' | grep -qx "${OLLAMA_MODEL}"; then
    echo "model ${OLLAMA_MODEL} already present"
else
    echo "pulling model ${OLLAMA_MODEL}"
    ollama pull "${OLLAMA_MODEL}"
fi
echo "model ${OLLAMA_MODEL} ready"
