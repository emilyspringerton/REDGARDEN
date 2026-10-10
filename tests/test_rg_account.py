#!/usr/bin/env python3
"""rg_account.h end to end: real curl + real loopback callback against a stub IDUNA and a fake browser."""
import http.server, json, os, re, stat, subprocess, sys, tempfile, threading, urllib.request, urllib.error

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = "eyJhbGciOiJFUzI1NiJ9.e30.sig_-ABC"
TICKET = "ab" * 36
CALLS = []

class Stub(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def _send(self, code, obj):
        b = json.dumps(obj).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(b))); self.end_headers(); self.wfile.write(b)
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = self.rfile.read(n).decode()
        CALLS.append((self.path, self.headers.get("Authorization"), body))
        if self.path == "/api/v1/games/redgarden/guest-login":
            d = json.loads(body)
            if d.get("guest_secret") == "goodsecret":
                return self._send(200, {"player_id": d["player_id"], "display_name": "Ada", "token": TOKEN,
                                        "expires_at": 4102444800, "account_state": "guest"})
            return self._send(401, {"error": "invalid credentials"})
        if self.path == "/api/v1/redgarden/self-ticket":
            if self.headers.get("Authorization") != "Bearer " + TOKEN:
                return self._send(401, {"error": "unauthorized"})
            return self._send(200, {"ticket": TICKET, "expires_at": 1, "player_id": "x"})
        self._send(404, {"error": "nope"})

srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Stub)
threading.Thread(target=srv.serve_forever, daemon=True).start()
BASE = "http://127.0.0.1:%d" % srv.server_address[1]

tmp = tempfile.mkdtemp()
exe = os.path.join(tmp, "t")
subprocess.check_call(["gcc", "-std=c99", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Werror", "-o", exe,
                       os.path.join(ROOT, "tests/test_rg_account.c")])
fails = []
def check(c, m):
    print(("ok   " if c else "FAIL ") + m)
    if not c: fails.append(m)

PID = "11111111-2222-4333-8444-555555555555"

# --- B: saved guest creds -> guest-login -> ticket, no browser ---
acct = os.path.join(tmp, "b.txt")
open(acct, "w").write("player_id=%s\nsecret=goodsecret\nname=\ntoken=\nexp=0\n" % PID)
r = subprocess.run([exe, BASE, acct, "5"], capture_output=True, text=True, timeout=30)
check(r.returncode == 0 and ("TICKET " + TICKET) in r.stdout, "saved guest creds mint a ticket (rc=%d out=%r err=%r)" % (r.returncode, r.stdout, r.stderr[-200:]))
check("NAME Ada" in r.stdout, "display name read back from IDUNA")
check("opening" not in r.stderr, "no browser popped when saved creds work")
check(os.path.exists(acct) and "token=" + TOKEN in open(acct).read(), "token cached in account file")
check(stat.S_IMODE(os.stat(acct).st_mode) == 0o600, "account file is 0600")

# --- C: stale secret -> 401 -> browser flow ---
# --- A: fresh install -> browser flow with a hostile local process poking the port first ---
def run_browser_case(name, preload):
    acct = os.path.join(tmp, name)
    if preload: open(acct, "w").write(preload)
    p = subprocess.Popen([exe, BASE, acct, "20"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    url = None
    # stderr is unbuffered in the child; read until the URL line shows up
    while True:
        line = p.stderr.readline()
        if not line: break
        m = re.search(r"opening (http\S+)", line)
        if m: url = m.group(1); break
    check(url is not None and "port=" in url and "state=" in url, name + ": connect URL has port+state")
    if url is None:
        p.kill(); return None
    port = re.search(r"port=(\d+)", url).group(1); state = re.search(r"state=([0-9a-f]+)", url).group(1)
    def hit(q):
        try:
            return urllib.request.urlopen("http://127.0.0.1:%s/cb?%s" % (port, q), timeout=5).status
        except urllib.error.HTTPError as e:
            return e.code
    bad = hit("state=deadbeef&player_id=%s&token=%s&name=Mallory&exp=0" % (PID, "evil.evil.evil"))
    check(bad == 400, name + ": wrong state rejected (got %s)" % bad)
    good = hit("state=%s&player_id=%s&token=%s&name=Grace%%20H&exp=4102444800&secret=abcdef0123" % (state, PID, TOKEN))
    check(good == 200, name + ": correct state accepted (got %s)" % good)
    out, err = p.communicate(timeout=30)
    check(p.returncode == 0 and ("TICKET " + TICKET) in out, name + ": ticket minted after callback (rc=%s out=%r)" % (p.returncode, out))
    check("NAME Grace H" in out, name + ": url-decoded name")
    saved = open(acct).read()
    check("secret=abcdef0123" in saved and "name=Grace H" in saved, name + ": identity persisted for next launch")
    return acct

run_browser_case("a.txt", None)
run_browser_case("c.txt", "player_id=%s\nsecret=staleSecret\nname=\ntoken=\nexp=0\n" % PID)

# --- D: no callback in time -> clean failure, no ticket ---
acct = os.path.join(tmp, "d.txt")
r = subprocess.run([exe, BASE, acct, "2"], capture_output=True, text=True, timeout=30)
check(r.returncode == 1 and "NOTICKET" in r.stdout, "timeout without sign-in yields no ticket")

# --- E: shell metacharacters in base_url never reach a shell ---
r = subprocess.run([exe, BASE + "/;touch " + os.path.join(tmp, "pwned"), os.path.join(tmp, "e.txt"), "1"],
                   capture_output=True, text=True, timeout=30)
check(not os.path.exists(os.path.join(tmp, "pwned")), "unsafe base_url rejected, nothing executed")

print("\n%d failure(s)" % len(fails))
sys.exit(1 if fails else 0)
