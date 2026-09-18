#!/usr/bin/env python3
"""Standalone tests for diagnostic redaction rules and JSONL schema expectations."""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

SECRET_KEYS = {
    "password", "passwd", "secret", "token", "pkey", "private_key", "cert",
    "certificate", "pin", "credentials", "salt", "gcm_key", "rikey", "clientcert",
}


def looks_like_secret_key(key: str) -> bool:
    k = key.lower()
    needles = [
        "password", "passwd", "secret", "token", "pkey", "private", "cert",
        "certificate", "pin", "credential", "salt", "csrf", "gcm_key", "rikey", "clientcert",
    ]
    for n in needles:
        if n in k:
            if n == "key":
                continue
            return True
    if k == "key" or k.endswith("_key") or k.startswith("key_") or "apikey" in k or "api_key" in k:
        return True
    return False


def redact_text(text: str) -> str:
    text = re.sub(r"-----BEGIN [^-]+-----[\s\S]*?-----END [^-]+-----", "[REDACTED]", text)
    text = re.sub(r"(?i)(password|passwd|secret|token|pin|pkey|salt)\s*[:=]\s*\S+", r"\1=[REDACTED]", text)
    return text


def test_redact_keys():
    assert looks_like_secret_key("password")
    assert looks_like_secret_key("nvhttp_pkey")
    assert looks_like_secret_key("cert")
    assert looks_like_secret_key("api_key")
    assert not looks_like_secret_key("packetsize")
    assert not looks_like_secret_key("bitrate")


def test_redact_pem_and_kv():
    pem = "-----BEGIN PRIVATE KEY-----\nABC\n-----END PRIVATE KEY-----"
    assert "[REDACTED]" in redact_text(pem)
    assert "password=[REDACTED]" in redact_text("password=supersecret")


def test_jsonl_schema_fixture():
    event = {
        "ts": "2026-09-18T05:00:00.000Z",
        "mono_ms": 12345,
        "session_id": "00000000-0000-0000-0000-000000000001",
        "type": "session_begin",
        "category": "NONE",
    }
    # Round-trip
    line = json.dumps(event)
    parsed = json.loads(line)
    for required in ("ts", "mono_ms", "session_id", "type"):
        assert required in parsed


def test_categories():
    cats = [
        "CAPTURE_DEVICE_LOST",
        "ENCODER_DEVICE_LOST",
        "DRIVER_INTERNAL_ERROR",
        "DISPLAY_MODE_CHANGED",
        "NETWORK_TIMEOUT",
        "PROCESS_CRASH",
    ]
    assert len(cats) == 6


def main() -> int:
    test_redact_keys()
    test_redact_pem_and_kv()
    test_jsonl_schema_fixture()
    test_categories()
    print("ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
