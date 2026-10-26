#!/usr/bin/env python3
"""prefs_set.py - set integer values in a Feral preferences file (registry XML) in the game's real setup key
HKEY_CURRENT_USER\\Software\\Monolith Productions\\Middle Earth Shadow of Mordor\\Setup (docs/re-launcher.md 1).

    prefs_set.py FILE NAME=INT [NAME=INT ...]

NAME may name a subkey path with backslashes: 'SpecificationAlerts\\LnxDistributionUnsupported=1' sets the value
LnxDistributionUnsupported in Setup\\SpecificationAlerts (keys made when missing). An existing value of that name is
replaced (its type becomes integer), a new one is added after its siblings with their indentation. The rest of the file
stays as it was: the same declaration line, the same text and order. For boot_check.sh's --prefs-set: only ever on the
run's own seeded copy (RUN/home), never on the user's real prefs. Prints one line per value; exit 2 on bad input.
"""
import re
import sys
import xml.etree.ElementTree as ET

SETUP = ["HKEY_CURRENT_USER", "Software", "Monolith Productions", "Middle Earth Shadow of Mordor", "Setup"]


def child_key(parent, name, depth):
    for k in parent.findall("key"):
        if k.get("name") == name:
            return k, False
    k = ET.Element("key", {"name": name})
    add(parent, k, depth)
    k.text = "\n" + "    " * (depth + 1)  # never a self-closing <key/>: Feral's files always open and close
    return k, True


def add(parent, el, depth):
    """el into parent with the file's indentation (4 spaces a level): a value before the parent's first key (Feral's
    order: the values, then the subkeys), a key at the end"""
    ind = "\n" + "    " * depth
    kids = list(parent)
    first_key = next((i for i, k in enumerate(kids) if k.tag == "key"), None) if el.tag == "value" else None
    if first_key is not None:
        if first_key > 0:
            el.tail = kids[first_key - 1].tail
        else:
            el.tail, parent.text = parent.text, ind
        parent.insert(first_key, el)
        return
    if kids:
        last = kids[-1]
        el.tail = last.tail if last.tail is not None else "\n" + "    " * (depth - 1)
        last.tail = ind
    else:
        el.tail = "\n" + "    " * (depth - 1)
        parent.text = ind
    parent.append(el)


def main(argv):
    if len(argv) < 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    path, pairs = argv[1], argv[2:]
    for p in pairs:
        if not re.fullmatch(r"[A-Za-z0-9_ ]+(\\[A-Za-z0-9_ ]+)*=-?[0-9]+", p):
            print("bad NAME=INT: %r" % p, file=sys.stderr)
            return 2
    raw = open(path, "rb").read()
    first, sep, _ = raw.partition(b"\n")
    decl = first if first.startswith(b"<?xml") else None
    root = ET.fromstring(raw)
    if root.tag != "registry":
        print("not a Feral registry file (root %r)" % root.tag, file=sys.stderr)
        return 2
    node, depth, made = root, 1, []
    for name in SETUP:
        node, new = child_key(node, name, depth)
        if new:
            made.append(name)
        depth += 1
    for p in pairs:
        name, val = p.split("=", 1)
        parts = name.split("\\")
        key, d = node, depth
        for sub in parts[:-1]:
            key, new = child_key(key, sub, d)
            d += 1
        old = None
        for v in key.findall("value"):
            if v.get("name") == parts[-1]:
                old = v
                break
        if old is not None:
            was = "%s %s" % (old.get("type"), (old.text or "").strip())
            old.set("type", "integer")
            old.text = str(int(val))
        else:
            was = "absent"
            el = ET.Element("value", {"name": parts[-1], "type": "integer"})
            el.text = str(int(val))
            add(key, el, d)
        print("Setup\\%s = %d (was %s)" % (name, int(val), was))
    body = ET.tostring(root, encoding="unicode", short_empty_elements=False)  # <value></value> stays
    with open(path, "wb") as f:
        if decl is not None:
            f.write(decl + b"\n")
        f.write(body.encode("utf-8"))
        f.write(b"\n")
    if made:
        print("made the missing keys: %s" % "\\".join(made))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
