"""Compile the rule that tells whether an answer came from the scale's own
server (services/own_server_rules.h) and check it: same host and same port as
the backend's address, nothing else (Spec 5.3 U3/AA, v0.8.7-beta.2).

Niko's refusal record (o12) asked strstr(backendBaseUrl(), host): a Moonraker
on the Pi that also runs Spoolman (port 7125 beside 7912) counted as the
server, a host that is the start of another one matched, and an empty host -
a begin() that failed - matched every address. In this fork Klipper printers
are asked through the same client (Spec 5.3 Z), so a Moonraker's 401 could
read as Spoolman refusing the key, and its 429 would pause the scale's own
questions."""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).parents[1]
source = r"""
#include "services/own_server_rules.h"
#include <cassert>

int main() {
  assert(sameServerAs("http://192.168.10.219:8000", "192.168.10.219", 8000));
  assert(sameServerAs("http://192.168.10.219:8000/", "192.168.10.219", 8000));
  assert(sameServerAs("http://192.168.10.219:8000/api", "192.168.10.219", 8000));
  assert(sameServerAs("http://Spoolman.local:7912", "spoolman.local", 7912));

  // Another service on the same machine is another server.
  assert(!sameServerAs("http://192.168.10.178:7912", "192.168.10.178", 7125));
  // A host that only begins or ends like the server's.
  assert(!sameServerAs("http://192.168.1.50:7912", "192.168.1.5", 7912));
  assert(!sameServerAs("http://192.168.1.5:7912", "192.168.1.50", 7912));
  assert(!sameServerAs("http://my-spoolman.local:7912", "spoolman.local", 7912));

  // Ports the address leaves out.
  assert(sameServerAs("http://bambuddy.lan", "bambuddy.lan", 80));
  assert(!sameServerAs("http://bambuddy.lan", "bambuddy.lan", 443));
  assert(sameServerAs("https://online.example/api/spoolman", "online.example", 443));
  assert(sameServerAs("HTTPS://online.example:8443", "online.example", 8443));

  // Nothing to compare: never the server.
  assert(!sameServerAs("http://192.168.10.219:8000", "", 8000));
  assert(!sameServerAs("http://192.168.10.219:8000", nullptr, 8000));
  assert(!sameServerAs("", "192.168.10.219", 8000));
  assert(!sameServerAs(nullptr, "192.168.10.219", 8000));
  assert(!sameServerAs("192.168.10.219:8000", "192.168.10.219", 8000));   // no scheme
  assert(!sameServerAs("http://", "x", 80));
  return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    cpp = Path(tmp) / "t.cpp"
    binary = Path(tmp) / "t"
    cpp.write_text(source, encoding="utf-8")
    subprocess.run(["c++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-I", str(root / "src"),
                    str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("own server rules: ok")
