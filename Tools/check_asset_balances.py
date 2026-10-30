#!/usr/bin/env python3
"""Read asset-service balances without creating jobs or exposing credentials.

Usage:
    python Tools/check_asset_balances.py
    python Tools/check_asset_balances.py --providers Tripo Hyper3D --json

Credentials stay in the desktop All_api_keys.txt file, one provider per line:
    Tripo: <key>
    V2fun: <key>
    Hyper3D: <key>
The only network requests are authenticated GETs to the three fixed endpoints.
Redirects are rejected, and response bodies / exception strings are never printed.
"""
from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

ENDPOINTS = {
    "Tripo": "https://openapi.tripo3d.ai/v3/account/balance",
    "V2fun": "https://api.v2fun.art/api/v1/balance",
    "Hyper3D": "https://api.hyper3d.com/api/v2/check_balance",
}
KEY_NAMES = {name.lower(): name for name in ENDPOINTS}
MAX_RESPONSE_BYTES = 65536
KEY_LINE = re.compile(r"^\s*(Tripo|V2fun|Hyper3D)\s*[:\uff1a]\s*(.*?)\s*$", re.I)


class RejectRedirects(urllib.request.HTTPRedirectHandler):
    """Never forward an Authorization header to a redirect destination."""
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def desktop_key_path() -> Path:
    desktop = Path.home() / "Desktop"
    if os.name == "nt":
        try:
            import winreg
            with winreg.OpenKey(
                winreg.HKEY_CURRENT_USER,
                r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders",
            ) as shell_folders:
                value, _ = winreg.QueryValueEx(shell_folders, "Desktop")
                desktop = Path(os.path.expandvars(value))
        except (ImportError, OSError, TypeError):
            pass
    return desktop / "All_api_keys.txt"


def load_keys(path: Path) -> tuple[dict[str, str], set[str]]:
    """Only requested provider labels are recognized; all other lines are ignored."""
    raw = path.read_bytes()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        content = raw.decode("utf-16")
    else:
        content = raw.decode("utf-8-sig")
    keys: dict[str, str] = {}
    invalid: set[str] = set()
    for line in content.splitlines():
        match = KEY_LINE.fullmatch(line)
        if not match:
            continue
        provider = KEY_NAMES[match.group(1).lower()]
        key = match.group(2).strip()
        if len(key) >= 2 and key[0] == key[-1] and key[0] in ("'", '"'):
            key = key[1:-1]
        if provider in keys or provider in invalid:
            keys.pop(provider, None)
            invalid.add(provider)
            continue
        # Prevent malformed headers and avoid guessing how ambiguous lines should parse.
        if not key or len(key) > 4096 or any(ord(c) < 33 or ord(c) > 126 for c in key):
            invalid.add(provider)
            continue
        keys[provider] = key
    return keys, invalid


def numeric(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        return None
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value


def summarize_response(provider: str, status: int, body: object) -> dict:
    result = {"provider": provider, "http_status": status}
    if not isinstance(body, dict):
        return {**result, "status": "invalid_response"}
    # Only explicitly known, non-secret error identifiers are retained.
    if provider == "Tripo":
        if body.get("code") != 0:
            code = body.get("code")
            safe_code = int(code) if isinstance(code, int) and not isinstance(code, bool) else None
            kind = "authentication_error" if safe_code in (2, 1002) else "application_error"
            error = {**result, "status": kind}
            if safe_code is not None:
                error["error_code"] = safe_code
            return error
        data = body.get("data")
        if not isinstance(data, dict):
            return {**result, "status": "invalid_response"}
    else:
        if body.get("error"):
            kind = "application_error"
            if body.get("error") == "API_SUBSCRIPTION_PLAN_TOO_LOW":
                kind = "api_plan_permission_error"
            return {**result, "status": kind}
        data = body
    balance = numeric(data.get("balance"))
    if balance is None:
        return {**result, "status": "missing_numeric_balance"}
    result.update(status="ok", balance=balance)
    if provider == "Tripo":
        frozen = numeric(data.get("frozen"))
        if frozen is not None:
            result["frozen"] = frozen
    return result


def query_balance(provider: str, key: str, timeout: float) -> dict:
    endpoint = ENDPOINTS[provider]
    parsed = urllib.parse.urlsplit(endpoint)
    # The endpoint mapping is source-controlled. No CLI URL, region fallback or redirect.
    if parsed.scheme != "https" or parsed.username or parsed.password or parsed.fragment:
        return {"provider": provider, "status": "endpoint_configuration_error"}
    request = urllib.request.Request(
        endpoint,
        headers={
            "Authorization": "Bearer " + key,
            "Accept": "application/json",
            "User-Agent": "Verdant-BalanceCheck/1.0",
        },
        method="GET",
    )
    opener = urllib.request.build_opener(RejectRedirects())
    try:
        with opener.open(request, timeout=timeout) as response:
            status = response.status
            raw = response.read(MAX_RESPONSE_BYTES + 1)
        if len(raw) > MAX_RESPONSE_BYTES:
            return {"provider": provider, "status": "response_too_large"}
        body = json.loads(raw.decode("utf-8-sig"))
        return summarize_response(provider, status, body)
    except urllib.error.HTTPError as error:
        status = error.code
        error.close()
        kind = {
            401: "authentication_error",
            403: "permission_error",
            429: "rate_limited",
        }.get(status, "http_error")
        if 300 <= status < 400:
            kind = "redirect_blocked"
        return {"provider": provider, "status": kind, "http_status": status}
    except (urllib.error.URLError, TimeoutError, OSError):
        return {"provider": provider, "status": "network_error"}
    except (ValueError, UnicodeError):
        return {"provider": provider, "status": "invalid_json_response"}


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--keys-file", type=Path, default=desktop_key_path(),
                        help="Local credentials file; its path and contents are not printed.")
    parser.add_argument("--providers", nargs="+", choices=tuple(ENDPOINTS),
                        default=list(ENDPOINTS))
    parser.add_argument("--timeout", type=float, default=20.0,
                        help="Timeout in seconds for each read-only GET (1-60).")
    parser.add_argument("--json", action="store_true", help="Print sanitized JSON only.")
    args = parser.parse_args(argv)
    if not math.isfinite(args.timeout) or not 1 <= args.timeout <= 60:
        parser.error("--timeout must be between 1 and 60 seconds")
    selected = list(dict.fromkeys(args.providers))
    try:
        keys, invalid = load_keys(args.keys_file)
    except (OSError, UnicodeError):
        results = [{"provider": provider, "status": "credentials_file_unreadable"}
                   for provider in selected]
    else:
        results = []
        for provider in selected:
            if provider in invalid:
                result = {"provider": provider, "status": "ambiguous_or_invalid_key_line"}
            elif provider not in keys:
                result = {"provider": provider, "status": "key_not_found"}
            else:
                result = query_balance(provider, keys[provider], args.timeout)
            results.append(result)
    if args.json:
        print(json.dumps(results, ensure_ascii=True, allow_nan=False))
    else:
        for result in results:
            fields = [result["provider"] + ": " + result["status"]]
            for name in ("balance", "frozen", "http_status", "error_code"):
                if name in result:
                    fields.append(name + "=" + str(result[name]))
            print(" | ".join(fields))
    return 0 if all(result["status"] == "ok" for result in results) else 1


if __name__ == "__main__":
    sys.exit(main())
