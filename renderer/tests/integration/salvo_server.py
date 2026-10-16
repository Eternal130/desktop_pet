#!/usr/bin/env python3
"""Minimal WebSocket "controller" for the renderer lifecycle smoke test.

Replays the parts of the controller's startup salvo that matter for the
model lifecycle, then shuts the renderer down gracefully:

    connect -> ready -> load_model x N (1.2s apart) -> shutdown

Each load_model exercises the model reload/destruction path
(ChangeScene -> ReleaseAllModel -> ~LAppModel -> ... -> Release()),
the exact path where the CubismLookUpdater double free once lived.

Usage: salvo_server.py <port> <model> <reload-count>
"""
import socket, threading, base64, hashlib, json, struct, sys, time

PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 19876
MODEL = sys.argv[2] if len(sys.argv) > 2 else 'Hiyori'
RELOADS = int(sys.argv[3]) if len(sys.argv) > 3 else 2


def ws_accept_key(key):
    magic = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'
    return base64.b64encode(hashlib.sha1((key + magic).encode()).digest()).decode()


def read_exact(sock, n):
    data = b''
    while len(data) < n:
        chunk = sock.recv(n - len(data))
        if not chunk:
            raise ConnectionError('closed')
        data += chunk
    return data


def read_frame(sock):
    b1, b2 = read_exact(sock, 2)
    opcode = b1 & 0x0F
    masked = b2 & 0x80
    ln = b2 & 0x7F
    if ln == 126:
        ln = struct.unpack('>H', read_exact(sock, 2))[0]
    elif ln == 127:
        ln = struct.unpack('>Q', read_exact(sock, 8))[0]
    mask = read_exact(sock, 4) if masked else None
    data = read_exact(sock, ln)
    if mask:
        data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    return opcode, data


def send_frame(sock, payload, opcode=1):
    b = bytearray([0x80 | opcode])
    ln = len(payload)
    if ln < 126:
        b.append(ln)
    elif ln < 65536:
        b.append(126)
        b += struct.pack('>H', ln)
    else:
        b.append(127)
        b += struct.pack('>Q', ln)
    b += payload
    sock.sendall(bytes(b))


def send_command(sock, action, payload):
    env = {"type": "command", "action": action, "id": f"smoke-{action}",
           "payload": payload, "timestamp": int(time.time() * 1000)}
    send_frame(sock, json.dumps(env).encode())
    print(f'sent {action}', flush=True)


def handle(conn):
    req = b''
    while b'\r\n\r\n' not in req:
        req += conn.recv(4096)
    key = None
    for line in req.decode('utf-8', 'replace').split('\r\n'):
        if line.lower().startswith('sec-websocket-key:'):
            key = line.split(':', 1)[1].strip()
    conn.sendall(('HTTP/1.1 101 Switching Protocols\r\n'
                  'Upgrade: websocket\r\nConnection: Upgrade\r\n'
                  f'Sec-WebSocket-Accept: {ws_accept_key(key)}\r\n\r\n').encode())
    print('handshake done', flush=True)

    def reader():
        try:
            while True:
                op, _ = read_frame(conn)
                if op == 8:  # close
                    send_frame(conn, b'', 8)
                    return
        except Exception:
            return

    threading.Thread(target=reader, daemon=True).start()

    time.sleep(1.2)
    for _ in range(RELOADS):
        send_command(conn, 'load_model', {'model_path': MODEL})
        time.sleep(1.2)
    time.sleep(1.0)
    send_command(conn, 'shutdown', {})
    # Linger so the renderer can drain the shutdown command before we vanish.
    time.sleep(8)
    try:
        conn.close()
    except Exception:
        pass


srv = socket.socket()
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(('127.0.0.1', PORT))
srv.listen(1)
print(f'listening on {PORT}', flush=True)
while True:
    conn, _ = srv.accept()
    try:
        handle(conn)
    except Exception as e:
        print(f'handler error: {e}', flush=True)
