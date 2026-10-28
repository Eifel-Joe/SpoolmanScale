"""Read the source tree without a compiler and check that the print card closes on its own after a label sent to a Brother over WiFi (design L1, Festlegung 6): a Brother never confirms, so "sent" takes the yellow look of an unconfirmed print, but it gets the countdown a confirmed print has, shorter, and the card closes when it has run down. One pick from the original nearly dropped "|| net_sent" from that condition, and the card then waited for OK after every Brother label."""
import re
import sys
from pathlib import Path

root = Path(__file__).parents[1]
src = root / "src"

# The tokenizer and the function finder of the convention ratchet: comments
# and string contents blanked, function bodies by brace matching. The script
# is not a package, so its folder goes on the path.
sys.path.insert(0, str(root / "scripts"))
import check_conventions as cc  # noqa: E402

CARD = "ui/print_card.cpp"


def squeeze(text):
    """The text without any blank, so that a call is found however it is wrapped."""
    return re.sub(r"\s+", "", text)


def body_of(rel, name):
    """The code between the braces of the one function called name, blanks gone."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    code = cc.Source(path.read_text(encoding="utf-8")).code
    spans = [(start, end) for found, start, end in cc.find_functions(code) if found == name]
    assert len(spans) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return squeeze(code[spans[0][0] + 1:spans[0][1]])


def block_of(body, head, where):
    """The code between the braces that follow head (it ends in an opening
    brace and occurs once)."""
    head = squeeze(head)
    assert body.count(head) == 1, f"{where}: '{head}' is not there exactly once"
    open_at = body.find(head) + len(head) - 1
    depth = 0
    for i in range(open_at, len(body)):
        if body[i] == "{":
            depth += 1
        elif body[i] == "}":
            depth -= 1
            if depth == 0:
                return body[open_at + 1:i]
    raise AssertionError(f"{where}: '{head}' has no closing brace")


def in_order(body, needles, where):
    """Every needle is in body, each one after the one before it."""
    at = -1
    for needle in needles:
        found = body.find(squeeze(needle), at + 1)
        assert found >= 0, f"{where}: '{needle}' is not there, or not after the one before it"
        at = found


# 1. The result: "sent" is a Brother's, and it is one of the unconfirmed ones.
where = f"src/{CARD}: printCardResult()"
body = body_of(CARD, "printCardResult")
in_order(body, ["const bool ok = (result == LP_OK);",
                "const bool net_sent = (result == LP_NET_SENT);",
                "const bool unconfirmed = (result == LP_SENT_UNCONFIRMED) || net_sent || net_reply;"], where)

# 2. The countdown runs for a confirmed print and for a label sent over WiFi,
# for each its own time, and its end asks the loop to close the card.
countdown = block_of(body, "if (ok || net_sent) {", where)
for needle in ("lv_anim_set_time(&a, ok ? PC_AUTOCLOSE_MS : PC_NET_SENT_CLOSE_MS);",
               "lv_anim_set_ready_cb(&a, [](lv_anim_t*) { s_close_req = true; });",
               "lv_anim_start(&a);"):
    assert squeeze(needle) in countdown, f"{where}: the countdown of 'if (ok || net_sent)' lacks '{needle}'"
raw = (src / CARD).read_text(encoding="utf-8")
net_ms = re.search(r"#define\s+PC_NET_SENT_CLOSE_MS\s+(\d+)\b", raw)
assert net_ms and int(net_ms.group(1)) > 0, f"src/{CARD}: PC_NET_SENT_CLOSE_MS is not a time above 0"

# 3. The loop carries out what the countdown asked for.
assert body_of(CARD, "printCardLoop") == squeeze("if (!s_scr || !s_close_req) return; closeCard();"), \
    f"src/{CARD}: printCardLoop() must close the card once it was asked to"

print("print card: ok")
