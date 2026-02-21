#!/usr/bin/env bash
#
# Generate a self-signed TLS certificate for Loong AI NVR.
# Usage: ./scripts/gen-cert.sh [output_dir]
#
set -euo pipefail

OUT_DIR="${1:-./certs}"
DAYS=365
SUBJECT="/CN=loong-ainvr/O=Loong AI NVR/C=CN"

mkdir -p "$OUT_DIR"

echo "Generating self-signed TLS certificate..."
echo "  Output:  $OUT_DIR"
echo "  Valid:   $DAYS days"
echo "  Subject: $SUBJECT"
echo ""

openssl req -x509 -newkey rsa:2048 \
  -keyout "$OUT_DIR/server.key" \
  -out "$OUT_DIR/server.crt" \
  -days "$DAYS" \
  -nodes \
  -subj "$SUBJECT" \
  -addext "subjectAltName=DNS:localhost,IP:127.0.0.1,IP:0.0.0.0"

echo ""
echo "Certificate generated:"
echo "  Certificate: $OUT_DIR/server.crt"
echo "  Private Key: $OUT_DIR/server.key"
echo ""
echo "To enable HTTPS, add to your config.json:"
echo '  "tls": {'
echo '    "enabled": true,'
echo "    \"cert_path\": \"$OUT_DIR/server.crt\","
echo "    \"key_path\": \"$OUT_DIR/server.key\""
echo '  }'
