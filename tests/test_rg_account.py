#!/usr/bin/env python3
"""rg_account.h end to end: real curl + real loopback callback against a stub IDUNA and a fake browser."""
import http.server, json, os, re, stat, subprocess, sys, tempfile, threading, urllib.request, urllib.error

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = "eyJhbGciOiJFUzI1NiJ9.e30.sig_-ABC"
TICKET = "ab" * 36
SSO_JWT = "eyJhbGciOiJFUzI1NiJ9.player.sig"
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
        if self.path == "/api/v1/games/redgarden/sso-exchange":
            if self.headers.get("Authorization") == "Bearer " + SSO_JWT:
                return self._send(200, {"player_id": "99999999-2222-4333-8444-555555555555", "display_name": "Ida",
                                        "token": TOKEN, "expires_at": 4102444800, "account_state": "sso"})
            return self._send(403, {"error": "no redgarden account"})
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

# --- F: --login-iduna: IDUNA SSO SDK (loopback + nonce) -> sso-exchange -> game token -> ticket (kanban #631) ---
import urllib.parse, http.client
def sdk_case(name, deliver):
    acct = os.path.join(tmp, name)
    env = dict(os.environ, REDGARDEN_SSO_BASE=BASE)
    p = subprocess.Popen([exe, BASE, acct, "20", "sdk"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=env)
    url = None
    while True:
        line = p.stderr.readline()
        if not line: break
        m = re.search(r"opening (http\S+)", line)
        if m: url = m.group(1); break
    check(url is not None and "/api/v1/auth/sso/login?redirect_uri=" in url, name + ": SDK login URL is the contract endpoint")
    if url is None:
        p.kill(); return
    redir = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)["redirect_uri"][0]
    ru = urllib.parse.urlparse(redir)
    state = urllib.parse.parse_qs(ru.query)["state"][0]
    check(ru.hostname == "127.0.0.1" and ru.path == "/cb", name + ": redirect_uri is the loopback callback")
    deliver(ru.port, state)
    out, err = p.communicate(timeout=30)
    return p.returncode, out, err, acct

def post_token(port, state_, jwt, host=None):
    c = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    body = "state=%s&sso_token=%s&player_id=7&display_name=Ida" % (state_, jwt)
    c.putrequest("POST", "/token", skip_host=True)
    c.putheader("Host", host or "127.0.0.1:%d" % port)
    c.putheader("Content-Length", str(len(body))); c.putheader("Content-Type", "application/x-www-form-urlencoded")
    c.endheaders(body.encode())
    r = c.getresponse(); r.read(); return r.status

seen = {}
def deliver_ok(port, state):
    seen["bad_state"] = post_token(port, "deadbeef", "evil.evil.evil")
    seen["bad_host"] = post_token(port, state, "evil.evil.evil", host="evil.example")
    seen["good"] = post_token(port, state, SSO_JWT)
res = sdk_case("f.txt", deliver_ok)
if res:
    rc, out, err, acct = res
    check(seen.get("bad_state") in (400, 403), "sdk: wrong state nonce rejected (got %s)" % seen.get("bad_state"))
    check(seen.get("bad_host") in (400, 403), "sdk: foreign Host header rejected (got %s)" % seen.get("bad_host"))
    check(seen.get("good") == 200, "sdk: correct state accepted (got %s)" % seen.get("good"))
    check(rc == 0 and ("TICKET " + TICKET) in out, "sdk: ticket minted after SSO sign-in (rc=%s out=%r err=%r)" % (rc, out, err[-200:]))
    check("NAME Ida" in out and "PID 99999999-" in out, "sdk: identity comes from the sso-exchange reply")
    ex = [c for c in CALLS if c[0] == "/api/v1/games/redgarden/sso-exchange"]
    check(len(ex) == 1 and ex[0][1] == "Bearer " + SSO_JWT, "sdk: sso-exchange called once with the IDUNA player JWT")
    check(os.path.exists(acct) and "token=" + TOKEN in open(acct).read(), "sdk: game token persisted")

print("\n%d failure(s)" % len(fails))
sys.exit(1 if fails else 0)
