#!/usr/bin/env python3
"""Serve the settings page from src/web_page.cpp with an in-memory fake API.

The page is the same HTML the firmware embeds. Nothing is written to disk, and
stopping this process drops the fake settings.

    python3 scripts/mock_web_ui.py
    python3 scripts/mock_web_ui.py --port 8765

Then open http://127.0.0.1:8080/
"""

from __future__ import annotations

import argparse
import calendar
import copy
import json
import re
import sys
import time
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

ROOT = Path(__file__).resolve().parents[1]
PAGE_SOURCE = ROOT / "src" / "web_page.cpp"
MAX_BODY = 4096

ALLOWED_ROOT = {
    "clock",
    "alarm",
    "display",
    "audio",
    "profiles",
    "hibernation",
    "persist",
    "preview",
}


def extract_index_html(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    marker = "const char kIndexHtml[]"
    start = text.find(marker)
    if start < 0:
        raise SystemExit(f"cannot find {marker} in {path}")
    equals = text.find("=", start)
    if equals < 0:
        raise SystemExit(f"cannot find the string initializer in {path}")

    i = equals + 1
    n = len(text)
    pieces: list[str] = []
    escapes = {"n": "\n", "t": "\t", "r": "\r", '"': '"', "\\": "\\", "'": "'"}
    while i < n:
        while i < n and text[i] in " \t\r\n":
            i += 1
        if i >= n or text[i] == ";":
            break
        if text[i] != '"':
            snippet = text[i : i + 40]
            raise SystemExit(f"unexpected token while reading kIndexHtml: {snippet!r}")
        i += 1
        chars: list[str] = []
        while i < n:
            ch = text[i]
            if ch == "\\":
                nxt = text[i + 1] if i + 1 < n else ""
                chars.append(escapes.get(nxt, nxt))
                i += 2
                continue
            if ch == '"':
                i += 1
                break
            chars.append(ch)
            i += 1
        pieces.append("".join(chars))
    html = "".join(pieces)
    if not html.startswith("<!DOCTYPE html>"):
        raise SystemExit("extracted page does not start with an HTML document")
    return html


def display(r: int, g: int, b: int, brightness: int, effect: str, nixie: int, transition: str) -> dict:
    return {
        "backlight": {
            "color": {"r": r, "g": g, "b": b},
            "brightness": brightness,
            "effect": effect,
        },
        "nixie": {"brightness": nixie, "transition": transition},
    }


def initial_state() -> dict:
    profiles = [
        display(255, 140, 0, 160, "breath", 255, "fade"),
        display(80, 180, 255, 120, "static", 220, "fade"),
        display(180, 80, 255, 140, "rainbow", 200, "instant"),
        display(0, 0, 0, 0, "off", 180, "instant"),
    ]
    now = time.time()
    return {
        "settings": {
            "schema_version": 1,
            "clock": {"timezone_offset_hours": 8, "rtc_calibrated": True},
            "alarm": {"enabled": True, "time": "07:30:00", "track": 1},
            "display": copy.deepcopy(profiles[0]),
            "audio": {"volume": 15},
            "profiles": {
                "active_index": 0,
                "items": [{"index": i, "display": copy.deepcopy(item)} for i, item in enumerate(profiles)],
            },
            "hibernation": {"enabled": False, "start": "23:00", "end": "07:00"},
        },
        "wifi": {"enabled": False, "configured": False, "ssid": ""},
        "ap_started": time.monotonic(),
        "ap_active": True,
        "session_code": 3847,
        "audio_track": 0,
        "audio_state": "stopped",
        "tracks": [
            {"id": 1, "name": "mp3/0001.mp3"},
            {"id": 2, "name": "mp3/0002.mp3"},
            {"id": 3, "name": "mp3/0003.mp3"},
        ],
        "time_unix": now,
        "time_mark": time.monotonic(),
        "osf": False,
        "time_valid": True,
    }


STATE = initial_state()
HTML = ""


def now_unix() -> float:
    return STATE["time_unix"] + (time.monotonic() - STATE["time_mark"])


def error(code: str, message: str, field: str | None = None, status: int = 400) -> tuple[int, dict]:
    body: dict = {"ok": False, "error": {"code": code, "message": message}}
    if field:
        body["error"]["field"] = field
    return status, body


def settings_view() -> dict:
    return copy.deepcopy(STATE["settings"])


def apply_settings(body: dict) -> tuple[int, dict]:
    unknown = [key for key in body if key not in ALLOWED_ROOT]
    if unknown:
        return error("unknown_field", "field is not supported", unknown[0])
    if not body:
        return error("empty_update", "at least one settings field is required")

    if "preview" in body:
        preview = body["preview"]
        if not isinstance(preview, dict) or preview.get("cancel") is not True:
            return error("invalid_value", "must be true", "preview.cancel")
        print("POST /api/settings  cancel preview", flush=True)
        return 200, {"ok": True}

    persist = body.get("persist", True)
    if not isinstance(persist, bool):
        return error("invalid_type", "must be a boolean", "persist")

    if persist is False:
        extra = [key for key in body if key not in {"display", "persist"}]
        if extra:
            return error("invalid_request", "only display may be sent when persist is false")
        display_body = body.get("display")
        if not isinstance(display_body, dict):
            return error("empty_update", "display is required when persist is false", "display")
        color = ((display_body.get("backlight") or {}).get("color") or {})
        print(
            "POST /api/settings  preview"
            f" rgb=({color.get('r')},{color.get('g')},{color.get('b')})"
            f" effect={(display_body.get('backlight') or {}).get('effect')}"
            "  (not saved)",
            flush=True,
        )
        return 200, {"ok": True}

    settings = STATE["settings"]
    if "clock" in body:
        clock = body["clock"]
        if not isinstance(clock, dict):
            return error("invalid_type", "must be an object", "clock")
        if "timezone_offset_hours" in clock:
            tz = clock["timezone_offset_hours"]
            if isinstance(tz, bool) or not isinstance(tz, (int, float)) or not -12 <= tz <= 14:
                return error("invalid_value", "must be from -12 to 14", "clock.timezone_offset_hours")
            settings["clock"]["timezone_offset_hours"] = int(tz)
        if "local_time" in clock:
            text = clock["local_time"]
            if not isinstance(text, str) or re.fullmatch(r"\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}", text) is None:
                return error("invalid_value", "must be YYYY-MM-DD HH:MM:SS", "clock.local_time")
            try:
                parsed = datetime.strptime(text, "%Y-%m-%d %H:%M:%S")
            except ValueError:
                return error("invalid_value", "must be a real local time", "clock.local_time")
            tz = settings["clock"]["timezone_offset_hours"]
            STATE["time_unix"] = calendar.timegm(parsed.timetuple()) - tz * 3600
            STATE["time_mark"] = time.monotonic()
            STATE["time_valid"] = True
            STATE["osf"] = False
            settings["clock"]["rtc_calibrated"] = True
        print("POST /api/settings  saved clock", flush=True)

    if "alarm" in body:
        alarm = body["alarm"]
        if not isinstance(alarm, dict):
            return error("invalid_type", "must be an object", "alarm")
        if "enabled" in alarm:
            if not isinstance(alarm["enabled"], bool):
                return error("invalid_type", "must be a boolean", "alarm.enabled")
            settings["alarm"]["enabled"] = alarm["enabled"]
        if "time" in alarm:
            if not isinstance(alarm["time"], str) or re.fullmatch(r"\d{2}:\d{2}:\d{2}", alarm["time"]) is None:
                return error("invalid_value", "must be HH:MM:SS", "alarm.time")
            settings["alarm"]["time"] = alarm["time"]
        if "track" in alarm:
            track = alarm["track"]
            if isinstance(track, bool) or not isinstance(track, int) or not 1 <= track <= 9999:
                return error("invalid_value", "must be from 1 to 9999", "alarm.track")
            settings["alarm"]["track"] = track
        print("POST /api/settings  saved alarm", flush=True)

    if "display" in body and "profiles" not in body:
        if not isinstance(body["display"], dict):
            return error("invalid_type", "must be an object", "display")
        settings["display"] = copy.deepcopy(body["display"])
        active = settings["profiles"]["active_index"]
        settings["profiles"]["items"][active]["display"] = copy.deepcopy(body["display"])
        print("POST /api/settings  saved display", flush=True)

    if "audio" in body:
        audio = body["audio"]
        volume = audio.get("volume") if isinstance(audio, dict) else None
        if isinstance(volume, bool) or not isinstance(volume, int) or not 0 <= volume <= 30:
            return error("invalid_value", "must be from 0 to 30", "audio.volume")
        settings["audio"]["volume"] = volume
        print(f"POST /api/settings  saved volume={volume}", flush=True)

    if "profiles" in body:
        profiles = body["profiles"]
        if not isinstance(profiles, dict):
            return error("invalid_type", "must be an object", "profiles")
        if "save_index" in profiles and "active_index" in profiles:
            return error("invalid_value", "provide save_index or active_index, not both", "profiles")
        if "save_index" in profiles:
            index = profiles["save_index"]
            if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index <= 3:
                return error("invalid_value", "must be from 0 to 3", "profiles.save_index")
            shown = body.get("display", settings["display"])
            if not isinstance(shown, dict):
                return error("invalid_type", "must be an object", "display")
            settings["display"] = copy.deepcopy(shown)
            settings["profiles"]["items"][index]["display"] = copy.deepcopy(shown)
            settings["profiles"]["active_index"] = index
            print(f"POST /api/settings  saved profile {index + 1}", flush=True)
        elif "active_index" in profiles:
            index = profiles["active_index"]
            if isinstance(index, bool) or not isinstance(index, int) or not 0 <= index <= 3:
                return error("invalid_value", "must be from 0 to 3", "profiles.active_index")
            settings["profiles"]["active_index"] = index
            settings["display"] = copy.deepcopy(settings["profiles"]["items"][index]["display"])
            print(f"POST /api/settings  applied profile {index + 1}", flush=True)
        else:
            return error("empty_update", "save_index or active_index is required", "profiles")

    if "hibernation" in body:
        hibernation = body["hibernation"]
        if not isinstance(hibernation, dict):
            return error("invalid_type", "must be an object", "hibernation")
        current = settings["hibernation"]
        if "enabled" in hibernation:
            if not isinstance(hibernation["enabled"], bool):
                return error("invalid_type", "must be a boolean", "hibernation.enabled")
            current["enabled"] = hibernation["enabled"]
        for key in ("start", "end"):
            if key in hibernation:
                value = hibernation[key]
                if not isinstance(value, str) or re.fullmatch(r"\d{2}:\d{2}", value) is None:
                    return error("invalid_value", "must be HH:MM", f"hibernation.{key}")
                current[key] = value
        print(
            "POST /api/settings  saved hibernation"
            f" enabled={current['enabled']} {current['start']}-{current['end']}",
            flush=True,
        )

    return 200, {"ok": True}


class Handler(BaseHTTPRequestHandler):
    server_version = "NixieMock/1.0"

    def log_message(self, fmt: str, *args) -> None:
        if args and str(args[1]).startswith("4"):
            super().log_message(fmt, *args)

    def _send(self, status: int, payload, content_type: str) -> None:
        if isinstance(payload, str):
            data = payload.encode("utf-8")
        else:
            data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)

    def _json(self, status: int, payload: dict) -> None:
        self._send(status, payload, "application/json; charset=utf-8")

    def _read_json(self) -> dict | None:
        length = int(self.headers.get("Content-Length", "0") or 0)
        if length > MAX_BODY:
            self._json(413, error("too_large", "body exceeds 4096 bytes")[1])
            return None
        raw = self.rfile.read(length) if length else b"{}"
        try:
            body = json.loads(raw.decode("utf-8") or "{}")
        except (UnicodeDecodeError, json.JSONDecodeError):
            self._json(400, error("invalid_json", "body must be JSON")[1])
            return None
        if not isinstance(body, dict):
            self._json(400, error("invalid_type", "root must be an object")[1])
            return None
        return body

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/":
            self._send(200, HTML, "text/html; charset=utf-8")
            return
        if path == "/api/settings":
            self._json(200, settings_view())
            return
        if path == "/api/time":
            settings = STATE["settings"]
            unix = int(now_unix())
            tz = settings["clock"]["timezone_offset_hours"]
            local = datetime.fromtimestamp(unix + tz * 3600, timezone.utc)
            self._json(
                200,
                {
                    "local_time": local.strftime("%Y-%m-%d %H:%M:%S"),
                    "unix_utc": unix,
                    "time_valid": STATE["time_valid"],
                    "osf": STATE["osf"],
                    "temperature": 25.5,
                    "clock": copy.deepcopy(settings["clock"]),
                },
            )
            return
        if path == "/api/wifi":
            wifi = STATE["wifi"]
            self._json(
                200,
                {
                    "enabled": bool(wifi.get("enabled")),
                    "configured": wifi["configured"],
                    "ssid": wifi["ssid"] if wifi["configured"] else "",
                },
            )
            return
        if path == "/api/ap/status":
            if not STATE["ap_active"]:
                self._json(
                    200,
                    {
                        "active": False,
                        "session_code": STATE["session_code"],
                        "ssid": "",
                        "password": "",
                        "remaining_sec": 0,
                        "client_count": 0,
                    },
                )
                return
            remaining = max(0, 15 * 60 - int(time.monotonic() - STATE["ap_started"]))
            if remaining == 0:
                STATE["ap_active"] = False
            code = STATE["session_code"]
            self._json(
                200,
                {
                    "active": STATE["ap_active"],
                    "session_code": code,
                    "ssid": f"NixieClock-{code}",
                    "password": "nixie2026",
                    "remaining_sec": remaining,
                    "client_count": 1 if STATE["ap_active"] else 0,
                },
            )
            return
        if path == "/api/ntp/status":
            wifi = STATE["wifi"]
            self._json(
                200,
                {
                    "configured": bool(wifi.get("enabled")) and wifi["configured"],
                    "last_success": bool(wifi.get("enabled")) and wifi["configured"],
                    "last_ntp_unix": int(now_unix()) if wifi.get("enabled") and wifi["configured"] else 0,
                },
            )
            return
        if path == "/api/audio/tracks":
            tracks = STATE["tracks"]
            self._json(200, {"folder": "mp3", "count": len(tracks), "tracks": tracks})
            return
        if path == "/api/audio/status":
            self._json(
                200,
                {
                    "track": STATE["audio_track"],
                    "state": STATE["audio_state"],
                    "count": len(STATE["tracks"]),
                },
            )
            return
        self._json(404, error("not_found", "no such endpoint")[1])

    def do_POST(self) -> None:
        path = urlparse(self.path).path
        body = self._read_json()
        if body is None:
            return
        if path == "/api/settings":
            status, payload = apply_settings(body)
            self._json(status, payload)
            return
        if path == "/api/wifi":
            wifi = STATE["wifi"]
            if "enabled" not in body and "ssid" not in body:
                self._json(400, error("missing_field", "enabled or ssid is required", "enabled")[1])
                return
            if "enabled" in body:
                if not isinstance(body["enabled"], bool):
                    self._json(400, error("invalid_type", "must be a boolean", "enabled")[1])
                    return
                wifi["enabled"] = body["enabled"]
            if "ssid" in body:
                ssid = body.get("ssid", "")
                password = body.get("password", "")
                if not isinstance(ssid, str) or not ssid.strip():
                    self._json(400, error("invalid_value", "ssid is required", "ssid")[1])
                    return
                if not isinstance(password, str):
                    self._json(400, error("invalid_type", "must be a string", "password")[1])
                    return
                wifi["configured"] = True
                wifi["ssid"] = ssid.strip()
                wifi["password"] = password
            print(
                f"POST /api/wifi  enabled={wifi.get('enabled')} configured={wifi['configured']} ssid={wifi['ssid']}",
                flush=True,
            )
            self._json(200, {"ok": True})
            return
        if path == "/api/ap/stop":
            STATE["ap_active"] = False
            print("POST /api/ap/stop  config AP off", flush=True)
            self._json(200, {"ok": True})
            return
        if path == "/api/audio/play":
            track = body.get("track")
            known = {item["id"] for item in STATE["tracks"]}
            if isinstance(track, bool) or not isinstance(track, int) or track not in known:
                self._json(400, error("invalid_value", "unknown track", "track")[1])
                return
            if STATE["audio_track"] == track and STATE["audio_state"] == "playing":
                STATE["audio_state"] = "paused"
            elif STATE["audio_track"] == track and STATE["audio_state"] == "paused":
                STATE["audio_state"] = "playing"
            else:
                STATE["audio_track"] = track
                STATE["audio_state"] = "playing"
            print(f"POST /api/audio/play  track={STATE['audio_track']} state={STATE['audio_state']}", flush=True)
            self._json(200, {"track": STATE["audio_track"], "state": STATE["audio_state"]})
            return
        self._json(404, error("not_found", "no such endpoint")[1])

    def do_DELETE(self) -> None:
        path = urlparse(self.path).path
        if path == "/api/wifi":
            STATE["wifi"] = {"enabled": False, "configured": False, "ssid": ""}
            print("DELETE /api/wifi  cleared", flush=True)
            self._json(200, {"ok": True})
            return
        self._json(404, error("not_found", "no such endpoint")[1])


def main() -> None:
    global HTML
    parser = argparse.ArgumentParser(description="Preview the Nixie settings page without hardware.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    HTML = extract_index_html(PAGE_SOURCE)
    server = ThreadingHTTPServer((args.host, args.port), Handler)
    print(f"Settings page: http://{args.host}:{args.port}/", flush=True)
    print("Fake settings live in memory only. Ctrl-C stops the server.", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    try:
        main()
    except OSError as exc:
        print(f"Cannot listen: {exc}", file=sys.stderr)
        sys.exit(1)
