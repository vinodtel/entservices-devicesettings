"""
/**
 * @file utils.py
 * @brief utils.py
 *
 * @testcase utils
 * @details Provides shared utility functions and constants used across all
 *          VideoPort / VideoDevice (HDMI Output) L3 test cases, including JSON-RPC
 *          command dispatch, curl-based API invocation, plugin lifecycle control,
 *          a multi-event JSON-RPC WebSocket listener, and structured logging helpers.
 *
 * @precondition
 *  - WPEFramework JSON-RPC endpoint is reachable at WPEFRAMEWORK_JSONRPC_URL.
 *
 * @dependencies
 *  - Standard Python libraries: os, json, subprocess, pathlib
 */
"""

import os
import time
import json
import base64
import hashlib
import socket
import ssl
import struct
import subprocess
from pathlib import Path
from urllib.parse import urlsplit


# Endpoint selection for local/QEMU execution. Explicit URL env vars take precedence.
TARGET_HOST = os.environ.get("TARGET_HOST", "127.0.0.1")
JSONRPC_PORT = os.environ.get("JSONRPC_PORT", "9998")
WPEFRAMEWORK_JSONRPC_URL = (
    os.environ.get("WPEFRAMEWORK_JSONRPC_URL")
    or os.environ.get("JSONRPC_URL")
    or f"http://{TARGET_HOST}:{JSONRPC_PORT}/jsonrpc"
)

# Seconds to wait for an expected notification, and the observation window used
# when asserting that a notification is NOT emitted.
EVENT_TIMEOUT = float(os.environ.get("VIDEOPORT_EVENT_TIMEOUT", "8"))
NO_EVENT_WINDOW = float(os.environ.get("VIDEOPORT_NO_EVENT_WINDOW", "4"))
# Seconds to wait after each plugin activation.
PLUGIN_INIT_WAIT = float(os.environ.get("VIDEOPORT_PLUGIN_INIT_WAIT", "3"))

# Returned by run_test() when a precondition (e.g. a connected display) is not met.
SKIPPED = "SKIPPED"

# ---------- ANSI COLOR CONSTANTS ----------
RESET = "\033[0m"
BOLD = "\033[1m"

RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
CYAN = "\033[96m"

# ---------- LOG HELPERS ----------
def _emit_log(message):
    print(message, flush=True)


def log_info(msg):
    _emit_log(f"{CYAN}{msg}{RESET}")

def log_success(msg):
    _emit_log(f"{GREEN}{BOLD}{msg}{RESET}")

def log_warning(msg):
    _emit_log(f"{YELLOW}{msg}{RESET}")

def log_error(msg):
    _emit_log(f"{RED}{BOLD}{msg}{RESET}")


def log_with_timing(msg, elapsed_time):
    """Append timing info only when VIDEOPORT_TIMING_ENABLED is set."""
    if os.environ.get("VIDEOPORT_TIMING_ENABLED"):
        return f"{msg} time consumed: {elapsed_time:.3f}s"
    return msg


def finish(tc_name, start_time, result=True, reason=""):
    """Log the final verdict of a testcase and return the value for run_test()."""
    elapsed = time.perf_counter() - start_time
    if result == SKIPPED:
        log_warning(f"{tc_name} Skipped ⚠ ({reason})")
        return SKIPPED
    if result:
        log_success(log_with_timing(f"{tc_name} Passed ✅", elapsed))
        return True
    log_error(f"{tc_name} Failed ❌ ({reason})")
    return False


def send_jsonrpc_command(method, params=None, request_id=1, timeout=5):
    """Send a JSON-RPC request to WPEFramework and return parsed response dict.
    Returns None when request fails or response is not JSON.
    """
    payload = {
        "jsonrpc": "2.0",
        "id": request_id,
        "method": method,
    }
    if params is not None:
        payload["params"] = params

    cmd = [
        "curl", "-sS", "--max-time", str(timeout),
        "-H", "Content-Type: application/json",
        "-X", "POST",
        "--data", json.dumps(payload),
        WPEFRAMEWORK_JSONRPC_URL,
    ]

    try:
        result = subprocess.run(
            cmd,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        if result.returncode != 0:
            return None
        body = (result.stdout or "").strip()
        if not body:
            return None
        return json.loads(body)
    except Exception:
        return None


def activate_plugin(callsign, timeout_seconds=40):
    """Activate an RDK plugin via Controller.1.activate.
    Returns True on success, False otherwise.
    """
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        response = send_jsonrpc_command(
            "Controller.1.activate",
            params={"callsign": callsign},
            request_id=1234567890,
        )
        if not response or "error" in response:
            time.sleep(1)
            continue
        return "result" in response
    return False


def deactivate_plugin(callsign, timeout_seconds=40):
    """Deactivate an RDK plugin via Controller.1.deactivate."""
    deadline = time.time() + timeout_seconds
    while time.time() < deadline:
        response = send_jsonrpc_command(
            "Controller.1.deactivate",
            params={"callsign": callsign},
            request_id=1234567890,
        )
        if not response or "error" in response:
            time.sleep(1)
            continue
        return "result" in response
    return False


class _WebSocketConnection:
    """Minimal RFC 6455 text-frame client implemented with the standard library."""

    GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

    def __init__(self, connection):
        self.connection = connection
        self.buffer = bytearray()

    @classmethod
    def connect(cls, url, timeout):
        endpoint = urlsplit(url)
        secure = endpoint.scheme == "https"
        host = endpoint.hostname
        if not host:
            raise ValueError(f"Invalid WebSocket endpoint: {url}")
        port = endpoint.port or (443 if secure else 80)
        path = endpoint.path or "/"
        if endpoint.query:
            path = f"{path}?{endpoint.query}"

        connection = socket.create_connection((host, port), timeout=timeout)
        if secure:
            connection = ssl.create_default_context().wrap_socket(
                connection, server_hostname=host
            )
        connection.settimeout(timeout)

        websocket = cls(connection)
        key = base64.b64encode(os.urandom(16)).decode("ascii")
        host_header = endpoint.netloc
        request = (
            f"GET {path} HTTP/1.1\r\n"
            f"Host: {host_header}\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {key}\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "Sec-WebSocket-Protocol: jsonrpc\r\n\r\n"
        )
        connection.sendall(request.encode("ascii"))
        response = websocket._receive_headers()
        lines = response.decode("iso-8859-1").split("\r\n")
        if not lines or " 101 " not in lines[0]:
            raise ConnectionError(f"WebSocket upgrade rejected: {lines[0] if lines else response!r}")

        headers = {}
        for line in lines[1:]:
            name, separator, value = line.partition(":")
            if separator:
                headers[name.strip().lower()] = value.strip()
        expected_accept = base64.b64encode(
            hashlib.sha1(f"{key}{cls.GUID}".encode("ascii")).digest()
        ).decode("ascii")
        if headers.get("sec-websocket-accept") != expected_accept:
            raise ConnectionError("WebSocket upgrade returned an invalid accept key")
        return websocket

    def _receive_headers(self):
        while b"\r\n\r\n" not in self.buffer:
            chunk = self.connection.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed during WebSocket upgrade")
            self.buffer.extend(chunk)
            if len(self.buffer) > 65536:
                raise ConnectionError("WebSocket upgrade headers are too large")
        headers, remaining = bytes(self.buffer).split(b"\r\n\r\n", 1)
        self.buffer = bytearray(remaining)
        return headers

    def send_text(self, text):
        self._send_frame(0x1, text.encode("utf-8"))

    def _send_frame(self, opcode, payload=b""):
        mask = os.urandom(4)
        length = len(payload)
        header = bytearray([0x80 | opcode])
        if length < 126:
            header.append(0x80 | length)
        elif length <= 0xFFFF:
            header.append(0x80 | 126)
            header.extend(struct.pack("!H", length))
        else:
            header.append(0x80 | 127)
            header.extend(struct.pack("!Q", length))
        masked_payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))
        self.connection.sendall(bytes(header) + mask + masked_payload)

    def receive_text(self):
        fragments = bytearray()
        while True:
            first, second = self._receive_exact(2)
            final = bool(first & 0x80)
            opcode = first & 0x0F
            masked = bool(second & 0x80)
            length = second & 0x7F
            if length == 126:
                length = struct.unpack("!H", self._receive_exact(2))[0]
            elif length == 127:
                length = struct.unpack("!Q", self._receive_exact(8))[0]
            mask = self._receive_exact(4) if masked else None
            payload = self._receive_exact(length)
            if mask:
                payload = bytes(value ^ mask[index % 4] for index, value in enumerate(payload))

            if opcode == 0x8:
                raise ConnectionError("WebSocket closed by server")
            if opcode == 0x9:
                self._send_frame(0xA, payload)
                continue
            if opcode == 0xA:
                continue
            if opcode == 0x1:
                fragments = bytearray(payload)
            elif opcode == 0x0 and fragments:
                fragments.extend(payload)
            else:
                continue
            if final:
                return fragments.decode("utf-8")

    def _receive_exact(self, length):
        while len(self.buffer) < length:
            chunk = self.connection.recv(max(4096, length - len(self.buffer)))
            if not chunk:
                raise ConnectionError("WebSocket connection closed")
            self.buffer.extend(chunk)
        data = bytes(self.buffer[:length])
        del self.buffer[:length]
        return data

    def settimeout(self, timeout):
        self.connection.settimeout(timeout)

    def close(self):
        try:
            self._send_frame(0x8)
        except (OSError, ConnectionError):
            pass
        finally:
            self.connection.close()


class JsonRpcEventListener:
    """Receive Thunder JSON-RPC notifications from one or more plugins over a
    single persistent WebSocket.

    subscriptions: list of (callsign, event_name) tuples. Every received
    notification is buffered (in arrival order), so tests can assert both the
    presence and the relative ordering of events, or their absence.
    """

    def __init__(self, subscriptions, listener_id, timeout=EVENT_TIMEOUT):
        self.subscriptions = list(subscriptions)
        self.listener_id = listener_id
        self.timeout = timeout
        self._socket = None
        self._next_request_id = 1
        # "<clientId>.<event>" -> (callsign, event)
        self._routes = {}
        # [{"callsign", "event", "params", "time", "consumed"}]
        self.received = []

    def _client_id(self, callsign):
        safe = "".join(ch if ch.isalnum() else "_" for ch in callsign)
        return f"{self.listener_id}_{safe}"

    def _request(self, method, params):
        request_id = self._next_request_id
        self._next_request_id += 1
        self._socket.send_text(json.dumps({
            "jsonrpc": "2.0",
            "id": request_id,
            "method": method,
            "params": params,
        }))
        return self._receive_until(lambda m: m.get("id") == request_id, self.timeout)

    def connect(self):
        try:
            self._socket = _WebSocketConnection.connect(WPEFRAMEWORK_JSONRPC_URL, self.timeout)
            for callsign, event in self.subscriptions:
                client_id = self._client_id(callsign)
                response = self._request(
                    f"{callsign}.1.register", {"event": event, "id": client_id}
                )
                if not response or "error" in response or "result" not in response:
                    log_error(f"Event registration rejected for {callsign}.{event}: {response}")
                    self.close()
                    return False
                self._routes[f"{client_id}.{event}"] = (callsign, event)
                log_info(f"Registered for {callsign}.{event}")
            return True
        except Exception as exc:
            log_error(f"WebSocket event registration failed: {exc}")
            self.close()
            return False

    def _buffer_notification(self, message):
        route = self._routes.get(message.get("method"))
        if route is None:
            return False
        params = message.get("params")
        self.received.append({
            "callsign": route[0],
            "event": route[1],
            "params": params if isinstance(params, dict) else {},
            "time": time.monotonic(),
            "consumed": False,
        })
        log_info(f"Notification {route[0]}.{route[1]}: {params}")
        return True

    def _receive_until(self, predicate, timeout):
        """Read messages until predicate(message) matches or timeout expires.
        Notifications seen on the way are always buffered."""
        if self._socket is None:
            return None
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self._socket.settimeout(max(0.1, deadline - time.monotonic()))
            try:
                message = json.loads(self._socket.receive_text())
            except socket.timeout:
                return None
            except (json.JSONDecodeError, TypeError):
                continue
            except (ConnectionError, OSError) as exc:
                log_error(f"WebSocket receive failed: {exc}")
                return None
            if not isinstance(message, dict):
                continue
            if "method" in message and "id" not in message:
                self._buffer_notification(message)
            if predicate(message):
                return message
        return None

    def _take_buffered(self, callsign, event, predicate):
        for entry in self.received:
            if entry["consumed"] or entry["callsign"] != callsign or entry["event"] != event:
                continue
            if predicate is None or predicate(entry["params"]):
                entry["consumed"] = True
                return entry
        return None

    def wait_for_event(self, callsign, event, predicate=None, timeout=None):
        """Return the params of the first not-yet-consumed matching notification,
        or None if none arrives within timeout."""
        timeout = self.timeout if timeout is None else timeout
        entry = self._take_buffered(callsign, event, predicate)
        if entry is not None:
            return entry["params"]
        route = None
        for key, value in self._routes.items():
            if value == (callsign, event):
                route = key
        if route is None:
            log_error(f"wait_for_event: not subscribed to {callsign}.{event}")
            return None

        def matches(message):
            if message.get("method") != route or "id" in message:
                return False
            params = message.get("params")
            return predicate is None or predicate(params if isinstance(params, dict) else {})

        if self._receive_until(matches, timeout) is None:
            return None
        return self._take_buffered(callsign, event, predicate)["params"]

    def collect(self, duration):
        """Keep reading for `duration` seconds, buffering every notification."""
        self._receive_until(lambda message: False, duration)

    def events(self, callsign=None, event=None):
        """All buffered notifications (optionally filtered), in arrival order."""
        return [
            entry for entry in self.received
            if (callsign is None or entry["callsign"] == callsign)
            and (event is None or entry["event"] == event)
        ]

    def clear(self):
        self.received = []

    def close(self):
        if self._socket is None:
            return
        try:
            for callsign, event in self.subscriptions:
                if f"{self._client_id(callsign)}.{event}" in self._routes:
                    self._socket.send_text(json.dumps({
                        "jsonrpc": "2.0",
                        "id": 0,
                        "method": f"{callsign}.1.unregister",
                        "params": {"event": event, "id": self._client_id(callsign)},
                    }))
        except Exception:
            pass
        try:
            self._socket.close()
        finally:
            self._socket = None


def send_curl_command(curl_command):
    """Send a curl command list and return the first valid JSON response line."""
    output_response = ""
    try:
        result = subprocess.run(
            curl_command,
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        response = result.stdout or ""
        for line in response.splitlines():
            try:
                json.loads(line)
                output_response = line
                break
            except json.JSONDecodeError:
                pass

        if len(output_response) < 5:
            output_response = "< No response from WPEFramework >"
    except Exception as exc:
        _emit_log(f"Inside utils.py : Exception in send_curl_command function: {exc}")
    finally:
        return output_response


def parse_result(curl_response):
    """Parse a JSON-RPC curl response string and return the 'result' field.
    Returns None on transport/parse error or when no result field is present.
    """
    if not curl_response or curl_response.startswith("< No response"):
        return None
    try:
        body = json.loads(curl_response)
    except json.JSONDecodeError:
        return None
    if not isinstance(body, dict) or "result" not in body:
        return None
    return body.get("result")


def is_ok(curl_response):
    """Return True if a JSON-RPC response is valid and carries a 'result' (no 'error')."""
    if not curl_response or curl_response.startswith("< No response"):
        return False
    try:
        body = json.loads(curl_response)
    except json.JSONDecodeError:
        return False
    if not isinstance(body, dict):
        return False
    return "result" in body and "error" not in body


def responded(curl_response):
    """True when the plugin returned a JSON body (not the no-response sentinel)."""
    return bool(curl_response) and not curl_response.startswith("< No response")

def is_rejected(curl_response):
    """True when the plugin answered but refused the request: a JSON-RPC
    'error' object or a result carrying success == false."""
    if not responded(curl_response):
        return False
    try:
        body = json.loads(curl_response)
    except json.JSONDecodeError:
        return False
    if not isinstance(body, dict):
        return False
    if "error" in body:
        return True
    result = body.get("result")
    return isinstance(result, dict) and result.get("success") is False


def restart_plugins(callsigns):
    """Deactivate callsigns (in reverse order) and re-activate them in order.
    Used by persistence scenarios to force the HAL to reload persisted values."""
    for callsign in reversed(callsigns):
        log_info(f"Restart: deactivating {callsign}")
        if not deactivate_plugin(callsign):
            log_error(f"Restart: deactivation failed for {callsign}")
            return False
    time.sleep(2)
    for callsign in callsigns:
        log_info(f"Restart: activating {callsign}")
        if not activate_plugin(callsign):
            log_error(f"Restart: activation failed for {callsign}")
            return False
        time.sleep(PLUGIN_INIT_WAIT)
    return True
