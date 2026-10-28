"""Read the source tree without a compiler and check the search in a colour list (V40): a magnifier in the header of "Enter filament" (the colour step) and of "New from database" (the colour families and the entries); a tap parks and the screen's tick opens the entry's keyboard over that screen, a word that comes back builds the list once with the names that hold it, and the header shows the word with an x that ends the search; back ends a search first and the step stays, a close forgets it; the lists are the ones already loaded (the database's of the chosen material, the entry's of its sources), and in "New from database" a search spans every family of the list."""
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

RULES_H = "services/colour_search_rules.h"
RULES = "services/colour_search_rules.cpp"
UI_H = "ui/colour_search.h"
UI = "ui/colour_search.cpp"
DBP = "ui/db_pick_screen.cpp"
KEYS = "ui/sort_entry_keys.cpp"
INTERNAL = "ui/sort_entry_internal.h"
ENTRY = "ui/sort_entry_screen.cpp"
PICK = "ui/sort_entry_pick.cpp"
STEPS = "ui/sort_entry_steps.cpp"


def sq(text):
    """The text as the code view has it (comments gone, string and char
    contents blanked) and without any blank, so that a call is found however
    it is wrapped and a needle reads like the code it is held against."""
    return re.sub(r"\s+", "", cc.Source(text).code)


def source_of(rel):
    """One file under src/ as the ratchet reads it."""
    path = src / rel
    assert path.is_file(), f"src/{rel}: file is missing"
    return cc.Source(path.read_text(encoding="utf-8"))


def code_of(rel):
    """The whole file, squeezed."""
    return re.sub(r"\s+", "", source_of(rel).code)


def body_of(rel, name):
    """The code between the braces of the one function called name, squeezed."""
    code = source_of(rel).code
    bodies = [code[start + 1:end] for fn, start, end in cc.find_functions(code) if fn == name]
    assert len(bodies) == 1, f"src/{rel}: {name}() is not defined exactly once"
    return re.sub(r"\s+", "", bodies[0])


def without_logs(code):
    """The squeezed code with the log calls taken out."""
    return re.sub(r'logSDf?\([^;]*\);', "", code)


def in_order(body, needles, where):
    """Every needle is in body, each one after the one before it."""
    at = -1
    for needle in needles:
        found = body.find(sq(needle), at + 1)
        assert found >= 0, f"{where}: '{needle}' is not there, or not after the one before it"
        at = found


def once(body, needle, where):
    """The needle is in body exactly once."""
    assert body.count(sq(needle)) == 1, f"{where}: '{needle}' is not there exactly once"


# 1. The rule: a unit of its own without Arduino or LVGL (the host test
# compiles it alone), the word of 23 bytes and a terminator.
rules_h = code_of(RULES_H)
once(rules_h, "bool colourSearchMatch(const char* name, const char* word);", f"src/{RULES_H}")
assert re.search(r"#define\s+COLOUR_SEARCH_MAX\s+24\b", (src / RULES_H).read_text(encoding="utf-8")), \
    f"src/{RULES_H}: COLOUR_SEARCH_MAX is not 24"
for rel in (RULES_H, RULES):
    raw = (src / rel).read_text(encoding="utf-8")
    for banned in ("Arduino.h", "lvgl.h", "lang.h", "<ctype.h>", "<cctype>", "<strings.h>"):
        assert banned not in raw, f"src/{rel}: includes {banned} (the rule is host-tested, ASCII folding only)"

# 2. The part both screens share. The head's tap only parks.
where = f"src/{UI}: onHeadTap()"
assert body_of(UI, "onHeadTap") == sq("ColourSearch* s = (ColourSearch*)lv_event_get_user_data(e); if (s) s->tapped = true;"), \
    f"{where}: the head's handler must only park the tap"
ui = code_of(UI)
assert ui.count(sq("lv_obj_add_event_cb(b, onHeadTap, LV_EVENT_CLICKED, s);")) == 1, \
    f"src/{UI}: the head is not the one button that parks the tap"

# The tick: a list that is gone or hidden, or a step without a search, takes
# nothing; a word that came back builds the list; a tap with a word ends the
# search; a tap without one opens the entry's keyboard over that screen, for
# a word of COLOUR_SEARCH_MAX - 1 bytes.
where = f"src/{UI}: colourSearchTick()"
body = without_logs(body_of(UI, "colourSearchTick"))
in_order(body, ["const bool typed = s->typed;", "s->tapped = false;", "s->typed = false;",
                "lv_obj_t* scr = under ? under() : nullptr;",
                "if (!scr || lv_obj_has_flag(scr, LV_OBJ_FLAG_HIDDEN) || !s->redraw) return;",
                "if (typed) { s->redraw(); }",
                "else if (s->word[0]) { s->word[0] = '\\0'; s->redraw(); }",
                "else { s_asking = s; sortKeysShowOver(under, SK_TEXT, STR_COLOUR_FIND_KEY, COLOUR_SEARCH_MAX - 1, wordTyped); }"],
         where)
once(ui, "sortKeysShowOver(", f"src/{UI}")
assert body_of(UI, "wordTyped") == sq("if (!s_asking || !text) return; "
                                      "utf8Cut(text, sizeof(s_asking->word) - 1, s_asking->word, sizeof(s_asking->word)); "
                                      "s_asking->typed = true;"), \
    f"src/{UI}: wordTyped() must keep the word for the one who asked and leave the list to the tick"

# The title: the old head goes, the new one (magnifier or word) only on a
# step that can redraw its list, the title narrowed by what it takes.
where = f"src/{UI}: colourSearchTitle()"
body = without_logs(body_of(UI, "colourSearchTitle"))
in_order(body, ["if (s->head) lv_obj_del(s->head);", "s->head = nullptr;", "s->tapped = false;", "s->redraw = redraw;",
                "if (redraw && scr) {", "s->head = s->word[0] ? chip(scr, s) : magnifier(scr, s);",
                "room = (s->word[0] ? CS_CHIP_W : CS_HEAD_BTN) + CS_SEP;",
                "lv_label_set_text(title, text ? text : \"\");", "lv_obj_set_width(title, CS_TITLE_W - room);",
                "lv_obj_set_x(title, -room / 2);"], where)
# Back ends a search and builds the list, and says so; a close forgets
# everything, the head with the screen it is on.
assert without_logs(body_of(UI, "colourSearchBack")) == sq(
    "if (!s || !s->word[0]) return false; s->word[0] = '\\0'; if (s->redraw) s->redraw(); return true;"), \
    f"src/{UI}: colourSearchBack() must end a search and build the list again, and only with a word"
assert body_of(UI, "colourSearchClose") == sq(
    "if (!s) return; s->word[0] = '\\0'; s->head = nullptr; s->redraw = nullptr; s->tapped = false; s->typed = false; "
    "if (s_asking == s) s_asking = nullptr;"), f"src/{UI}: colourSearchClose() does not forget the search"
# Shown is what holds the word, by the name the row shows.
assert body_of(UI, "colourSearchShows") == sq("return !s || colourSearchMatch(name, s->word);"), \
    f"src/{UI}: colourSearchShows() is not the rule"
in_order(body_of(UI, "colourSearchFdb"), ["if (!s || !s->word[0]) return true;",
                                          "fdbEntryDisplayName(e, maker, material, shown, sizeof(shown));",
                                          "return colourSearchMatch(shown, s->word);"], f"src/{UI}: colourSearchFdb()")
in_order(body_of(UI, "colourSearchNone"), ["if (!s || !s->word[0] || !body || !lvPoolHasRoomForRow()) return;",
                                           "T(STR_COLOUR_FIND_NONE), s->word"], f"src/{UI}: colourSearchNone()")
# Nothing is loaded for a search: no database load, no inventory, no source.
for banned in ("fdbStart", "sortSource", "backend", "http", "fdbRelease"):
    assert banned not in ui, f"src/{UI}: '{banned}' - a search cuts the list that is loaded and loads nothing"

# 3. The keyboard is the entry's, over the screen it is asked for: it goes
# when that one is hidden or gone, as it went with the entry before.
keys = code_of(KEYS)
assert body_of(KEYS, "sortKeysShow") == sq("sortKeysShowOver(sortEntryScreen, kind, title_id, max_len, done);"), \
    f"src/{KEYS}: sortKeysShow() is not the keyboard over the entry"
assert body_of(KEYS, "sortKeysShowOver").startswith(sq("sortKeysClose(); s_under = under;")), \
    f"src/{KEYS}: sortKeysShowOver() does not keep the screen under the keys"
in_order(body_of(KEYS, "sortKeysTick"), ["if (!s_keys) return;", "lv_obj_t* under = s_under ? s_under() : nullptr;",
                                         "if (!under || lv_obj_has_flag(under, LV_OBJ_FLAG_HIDDEN)) { sortKeysClose(); return; }"],
         f"src/{KEYS}: sortKeysTick()")
assert "sortEntryScreen()" not in keys, f"src/{KEYS}: the tick still asks for the entry's screen instead of the one under the keys"
internal = code_of(INTERNAL)
once(internal, "void sortKeysShowOver(lv_obj_t* (*under)(), uint8_t kind, int title_id, size_t max_len, "
               "void (*done)(const char* text));", f"src/{INTERNAL}")

# 4. "New from database": the magnifier on the family step and on the
# entries, nowhere else; the list it cuts is the one loaded for the material
# chosen.
assert (src / DBP).read_text(encoding="utf-8").count("\n") <= 1000, f"src/{DBP}: more than 1000 lines"
dbp = code_of(DBP)
assert '#include "ui/colour_search.h"' in (src / DBP).read_text(encoding="utf-8"), f"src/{DBP}: no colour search"
assert body_of(DBP, "setTitle") == sq("colourSearchTitle(&s_find, s_scr, s_title, text, nullptr);"), \
    f"src/{DBP}: setTitle() does not drop the search on the other steps"
titles = re.findall(r"colourSearchTitle\(([^;]*)\);", dbp)
assert sorted(titles) == sorted([sq("&s_find, s_scr, s_title, text, nullptr"),
                                 sq("&s_find, s_scr, s_title, title, showFamilyStep"),
                                 sq("&s_find, s_scr, s_title, title, s_step == DBP_FAMILY ? showFamilyStep : showEntries")]), \
    f"src/{DBP}: the search is in other headers than the families' and the entries': {titles}"
once(body_of(DBP, "showFamilies"), "colourSearchTitle(&s_find, s_scr, s_title, title, showFamilyStep);", f"src/{DBP}: showFamilies()")
assert "setTitle(" not in body_of(DBP, "showFamilies") and "setTitle(" not in body_of(DBP, "showEntries"), \
    f"src/{DBP}: a colour step sets its title without the search"

# The entries: a search on the family step lists them there and stays that
# step (its x and back return to the families), the family's name is not in
# the title while a search spans them all, and nothing found is said.
where = f"src/{DBP}: showEntries()"
body = body_of(DBP, "showEntries")
assert body.startswith(sq("if (s_step != DBP_FAMILY || !s_find.word[0]) s_step = DBP_ENTRIES;")), \
    f"{where}: a search on the family step must stay that step"
in_order(body, ["if (s_family >= 0 && !s_find.word[0]) {",
                "colourSearchTitle(&s_find, s_scr, s_title, title, s_step == DBP_FAMILY ? showFamilyStep : showEntries);",
                "lv_obj_t* body = newBody(DBP_BODY_Y, DBP_BODY_H);", "if (inFamily(*fdbEntry(i))) {",
                "if (rows == 0) colourSearchNone(&s_find, body, DBP_ROW_W);"], where)
where = f"src/{DBP}: inFamily()"
assert body_of(DBP, "inFamily") == sq(
    "if (s_find.word[0]) return inLine(e) && colourSearchFdb(&s_find, e, fdbMaker(s_maker)->name, s_material); "
    "return inLine(e) && (s_family < 0 || e.family == s_family);"), \
    f"{where}: a search must span every family of the line, by the name the row shows"
assert "s_find" not in body_of(DBP, "countNames"), f"src/{DBP}: countNames() counts what a search shows"
where = f"src/{DBP}: showFamilyStep()"
assert body_of(DBP, "showFamilyStep").endswith(sq("s_family = -1; if (s_find.word[0]) showEntries(); "
                                                  "else showFamilies(counts, typical, total);")), \
    f"{where}: the family step with a word must list the entries of every family"
# Back ends a search first; a close forgets it; the tick carries out the
# head's tap and the word, over the picker's own screen.
assert body_of(DBP, "goBack").startswith(sq("if (colourSearchBack(&s_find)) return; switch (s_step) {")), \
    f"src/{DBP}: goBack() does not end a search first"
once(body_of(DBP, "dbPickClose"), "colourSearchClose(&s_find);", f"src/{DBP}: dbPickClose()")
in_order(body_of(DBP, "dbPickTick"), ["colourSearchTick(&s_find, dbPickScreen);", "if (s_nav == NAV_NONE) return;"],
         f"src/{DBP}: dbPickTick()")
once(dbp, "colourSearchTick(", f"src/{DBP}")
# The loads are as they were: the list of one maker and one material.
once(body_of(DBP, "startJob"), "return fdbStartEntries(fdbMaker(s_maker)->name, s_material);", f"src/{DBP}: startJob()")
assert "s_find" not in body_of(DBP, "startJob") and "s_find" not in body_of(DBP, "startLoad"), \
    f"src/{DBP}: a search changes what is loaded"

# 5. "Enter filament": the magnifier on the colour step alone, the word in
# the entry's state.
assert re.search(r"\bColourSearch\s+find\s*;", (src / INTERNAL).read_text(encoding="utf-8")), \
    f"src/{INTERNAL}: SortUi has no ColourSearch"
once(internal, "void sortSearchTitle(const char* text, void (*redraw)());", f"src/{INTERNAL}")
assert body_of(ENTRY, "sortTitle") == sq("colourSearchTitle(&s_ui.find, s_scr, s_title, text, nullptr);"), \
    f"src/{ENTRY}: sortTitle() does not drop the search on the other steps"
assert body_of(ENTRY, "sortSearchTitle") == sq("colourSearchTitle(&s_ui.find, s_scr, s_title, text, redraw);"), \
    f"src/{ENTRY}: sortSearchTitle() is not the title with the search"
users = sorted(str(p.relative_to(src)).replace("\\", "/") for p in src.rglob("*.cpp")
               if "sortSearchTitle(" in re.sub(r"\s+", "", cc.Source(p.read_text(encoding="utf-8")).code))
assert users == sorted([ENTRY, PICK]), f"src: sortSearchTitle() is used in {users}"
where = f"src/{PICK}: sortShowColours()"
body = body_of(PICK, "sortShowColours")
once(body, "sortSearchTitle(title, sortShowColours);", where)
assert "sortTitle(" not in body, f"{where}: the colour step sets its title without the search"
in_order(body, ["sortSearchTitle(title, sortShowColours);", "lv_obj_t* body = sortNewBody();",
                "if (groupOf(d->colours.item[i]) != g) continue;",
                "if (!colourSearchShows(&ui.find, d->colours.item[i].name)) continue;",
                "colourRow(body, *d, i);", "if (rows == 0) colourSearchNone(&ui.find, body, SORT_ROW_W);",
                "sortNewTile(body);"], where)
# Back on the colour step ends a search first; the close forgets it; the
# tick runs after the keyboard's, so a word is shown in the pass it came.
where = f"src/{ENTRY}: goBack()"
once(body_of(ENTRY, "goBack"), "case SS_COLOUR: if (!colourSearchBack(&s_ui.find)) sortShowLines(); break;", where)
assert body_of(ENTRY, "goBack").count(sq("colourSearchBack(")) == 1, f"{where}: a search ends on another step's back"
once(body_of(ENTRY, "sortEntryClose"), "colourSearchClose(&s_ui.find);", f"src/{ENTRY}: sortEntryClose()")
in_order(body_of(ENTRY, "sortEntryTick"), ["sortKeysTick();", "colourSearchTick(&s_ui.find, sortEntryScreen);",
                                           "sortMaterialLoads(sortSourceTick());"], f"src/{ENTRY}: sortEntryTick()")
# The colour list is the line's as before: the brand's colours, its other
# materials', the usual ones (sortSourceColours()); the search adds no source.
once(body_of(STEPS, "chooseLine"), "sortSourceColours(ui.entry.material, ui.entry.line);", f"src/{STEPS}: chooseLine()")
assert "sortSource" not in body_of(PICK, "sortShowColours"), f"src/{PICK}: sortShowColours() loads while it lists"

# 6. Two new texts, in all three languages.
lang_cpp = (src / "lang.cpp").read_text(encoding="utf-8")
for sid, texts in (("STR_COLOUR_FIND_KEY", ("Farbe suchen", "Search colour", "Rechercher une couleur")),
                   ("STR_COLOUR_FIND_NONE", ('Keine Farbe mit \\"%s\\"', 'No colour with \\"%s\\"',
                                             'Aucune couleur avec \\"%s\\"'))):
    row = re.search(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*\}\s*,\s*//\s*'
                    + sid + r"\b", lang_cpp)
    assert row and row.groups() == texts, f"src/lang.cpp: {sid} is not {texts}"
    assert re.search(r"^\s*" + sid + r",", (src / "lang.h").read_text(encoding="utf-8"), re.M), f"src/lang.h: no {sid}"
budgets = (root / "tools" / "ui_budgets.tsv").read_text(encoding="utf-8")
assert re.search(r"^STR_COLOUR_FIND_KEY\t18\t460\tNEVER\t", budgets, re.M), "tools/ui_budgets.tsv: no budget for the keyboard's title"
assert re.search(r"^STR_COLOUR_FIND_NONE\t14\t452\tNEVER\t", budgets, re.M), "tools/ui_budgets.tsv: no budget for the line"

# 7. Both steps are in the pipeline, right after the one for the location.
steps = re.findall(r"- name: (Test [^\n]*)\n\s+run: ([^\n]*)", (root / ".github/workflows/ci.yml").read_text(encoding="utf-8"))
names = [name for name, _ in steps]
for name in ("Test colour search rules", "Test colour search"):
    assert name in names, f".github/workflows/ci.yml: the step '{name}' is not there"
assert names.index("Test colour search rules") == names.index("Test no tag location") + 1 and \
    names.index("Test colour search") == names.index("Test colour search rules") + 1, \
    ".github/workflows/ci.yml: the colour search steps do not follow 'Test no tag location'"
assert dict(steps)["Test colour search rules"] == "python3 test/test_colour_search_rules.py", \
    ".github/workflows/ci.yml: the rules step does not run python3 test/test_colour_search_rules.py"
assert dict(steps)["Test colour search"] == "python3 test/test_colour_search.py", \
    ".github/workflows/ci.yml: the step does not run python3 test/test_colour_search.py"

print("colour search: ok")
