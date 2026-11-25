#!/usr/bin/env python3
"""Talk to 9front-mcp in a guest (sys/src/build/host/guest/mcp-guest.sh) over HTTP.

usage: sys/src/build/host/guest/mcp.py rc 'command' [timeout]   (command '-': from stdin)
       sys/src/build/host/guest/mcp.py read path
       sys/src/build/host/guest/mcp.py write path < content
       sys/src/build/host/guest/mcp.py wait [seconds]           until the server answers
       sys/src/build/host/guest/mcp.py reload                   the server reads its deny list
                                                     again (after the user changed it)
Prints the tool's text; exits 1 when the tool reports an error (rc: a non-empty
status). env: MCPURL (http://127.0.0.1:$PORT/mcp, PORT 18091), MCPTOKEN
(mcpguest); the session id is kept per server, in $CLI_DIR/mcp.sid.HOST_PORT of
MCPURL (MCPSID: another file), so that guests and machines used at once keep
their sessions apart.
"""
import json, os, sys, time, urllib.error, urllib.parse, urllib.request

URL = os.environ.get("MCPURL", "http://127.0.0.1:%s/mcp" % os.environ.get("PORT", "18091"))
TOKEN = os.environ.get("MCPTOKEN", "mcpguest")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import paths
CLI_DIR = paths.get("CLI_DIR")	# dist/paths.env: 9space/cache/cli
# one session per server: its host and port in the name (127.0.0.1:18091 -> 127.0.0.1_18091)
SID = os.environ.get("MCPSID") or os.path.join(CLI_DIR,
    "mcp.sid." + urllib.parse.urlsplit(URL).netloc.replace(":", "_"))


def post(body, sid=None, timeout=60):
    h = {"Authorization": "Bearer " + TOKEN, "Content-Type": "application/json",
         "Accept": "application/json, text/event-stream"}
    if sid:
        h["Mcp-Session-Id"] = sid
    r = urllib.request.urlopen(urllib.request.Request(URL, json.dumps(body).encode(), h), timeout=timeout)
    data = r.read()
    return r.headers.get("Mcp-Session-Id"), (json.loads(data) if data.strip() else None)


def init():
    sid, _ = post({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
        "protocolVersion": "2025-06-18", "capabilities": {},
        "clientInfo": {"name": "mcp.py", "version": "0"}}})
    post({"jsonrpc": "2.0", "method": "notifications/initialized"}, sid)
    with open(SID, "w") as f:
        f.write(sid)
    return sid


def session():
    if os.path.exists(SID):
        return open(SID).read().strip()
    return init()


def call(name, args, wait):
    sid = session()
    body = {"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": name, "arguments": args}}
    try:
        _, ans = post(body, sid, wait)
    except urllib.error.HTTPError as e:
        if e.code not in (400, 404):  # a session of an earlier server
            raise
        _, ans = post(body, init(), wait)
    return ans


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    cmd = sys.argv[1]
    if cmd == "wait":
        end = time.time() + (int(sys.argv[2]) if len(sys.argv) > 2 else 300)
        while time.time() < end:
            try:
                init()
                return
            except (OSError, urllib.error.URLError):
                time.sleep(2)
        sys.exit("mcp.py: no server at " + URL)
    if cmd == "rc":
        t = int(sys.argv[3]) if len(sys.argv) > 3 else 120
        c = sys.stdin.read() if sys.argv[2] == "-" else sys.argv[2]
        ans = call("rc", {"command": c, "timeout": t}, t + 30)
    elif cmd == "read":
        ans = call("read", {"path": sys.argv[2]}, 60)
    elif cmd == "write":
        ans = call("write", {"path": sys.argv[2], "content": sys.stdin.read()}, 60)
    elif cmd == "reload":
        ans = call("reload_deny", {}, 60)
    else:
        sys.exit(__doc__)
    if "error" in ans:
        sys.exit("mcp.py: %s" % ans["error"])
    res = ans.get("result") or {}
    text = "".join(c.get("text", "") for c in res.get("content", []))
    sys.stdout.write(text if text == "" or text.endswith("\n") else text + "\n")
    # rc: the server ends the output with "status: ..." when the command failed
    last = text.rstrip("\n").rsplit("\n", 1)[-1]
    sys.exit(1 if res.get("isError") or (cmd == "rc" and last.startswith("status: ")) else 0)


main()
