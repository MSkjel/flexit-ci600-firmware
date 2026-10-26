#!/usr/bin/env python3
"""Turn the ESP repo's packages/panel*.yaml into C++ for the test harness. Globals, scripts, boot and interval
lambdas and entity actions go in with the lambda code as it is. stubs.h fakes the ESPHome side, and its id()
returns a reference.

    gen.py ESP_REPO OUT.cpp
"""

import filecmp
import os
import re
import sys

import esp_sync
import yaml

# Packages the harness runs and the vars for each, roughly what tests/panel.yaml in the ESP repo uses. Not update.yaml,
# which the ESP repo's update_test.cpp tests.
FILES = [
    ("packages/panel.yaml", {}),
    ("packages/panel/fireplace.yaml", {}),
    ("packages/panel/humidity_boost.yaml", {"flexit_humidity_sensor": "inside_humidity"}),
    ("packages/panel/co2.yaml", {"flexit_co2_sensor": "co2"}),
    ("packages/panel/summer_cooling.yaml", {}),
    (
        "packages/panel/sensor.yaml",
        {
            "entity": "inside_humidity",
            "label": "Bathroom|Bad",
            "unit": "%",
            "icon": "humidity",
            "trend": "true",
            "levels": "20, 30, 60, 70",
        },
    ),
    (
        "packages/panel/binary_sensor.yaml",
        {
            "entity": "heater_enabled",
            "label": "Heater on|Varme på",
            "icon": "fire",
            "on_text": "Allowed|Tillatt",
            "off_text": "Blocked|Sperret",
        },
    ),
    (
        "packages/panel/switch.yaml",
        {
            "entity": "heater",
            "label": "Heater|Varme",
            "icon": "radiator",
            "on_text": "Heating|Varmer",
            "off_text": "Idle|Hviler",
        },
    ),
    (
        "packages/panel/number.yaml",
        {"entity": "temperature", "label": "Setpoint", "unit": "C", "icon": "thermo", "page": "comfort"},
    ),
    (
        "packages/panel/button.yaml",
        {"entity": "clear_alarms", "label": "Clear alarms", "icon": "bell", "view": "false"},
    ),
    (
        "packages/panel/ha_text.yaml",
        {"name": "washer", "entity_id": "sensor.washer_state", "label": "Washer|Vaskemaskin", "icon": "washer"},
    ),
    (
        "packages/panel/ha_sensor.yaml",
        {
            "name": "hot_water",
            "entity_id": "sensor.water_heater_temperature",
            "label": "Hot water|Varmtvann",
            "icon": "shower",
            "unit": "°C",
            "decimals": "0",
            "levels": "40,50,75,85",
            "min": "20",
            "max": "80",
        },
    ),
    (
        "packages/panel/ha_binary_sensor.yaml",
        {
            "name": "front_door",
            "entity_id": "binary_sensor.front_door",
            "label": "Front door|Inngangsdør",
            "icon": "door",
            "on_text": "Open|Åpen",
            "off_text": "Closed|Lukket",
        },
    ),
    (
        "packages/panel/ha_switch.yaml",
        {
            "name": "door_lock",
            "entity_id": "lock.front_door",
            "label": "Door lock|Dørlås",
            "icon": "lock",
            "on_action": "lock.lock",
            "off_action": "lock.unlock",
            "on_state": "locked",
            "on_text": "Locked|Låst",
            "off_text": "Unlocked|Ulåst",
        },
    ),
    ("packages/panel/picture.yaml", {"name": "dusk", "file": "x.png"}),
    ("packages/panel/online_picture.yaml", {"name": "radar", "url": "http://homeassistant.local:8123/local/radar.png"}),
    ("packages/panel/ha_picture.yaml", {"name": "front_door", "entity_id": "camera.front_door"}),
    ("packages/panel/animation.yaml", {"name": "ball", "file": "x.gif", "x": 250, "y": 20}),
    ("packages/panel/animation.yaml", {"name": "noise", "file": "y.gif", "resize": "64x64", "drift_x": -10}),
    ("packages/panel/met.yaml", {"latitude": "59.9139", "longitude": "10.7522", "encoding": "auto"}),
]


class Loader(yaml.SafeLoader):
    pass


# an id given with !extend, which adds to an entity from another package
class Extend(str):
    pass


# ESPHome tags. !extend is kept as a marker, the rest like !lambda and !secret load as plain values
def _tag(loader, suffix, node):
    if suffix == "extend":
        return Extend(loader.construct_scalar(node))
    if isinstance(node, yaml.ScalarNode):
        return loader.construct_scalar(node)
    if isinstance(node, yaml.SequenceNode):
        return loader.construct_sequence(node)
    return loader.construct_mapping(node)


Loader.add_multi_constructor("!", _tag)


def text_of(v):
    """A var's value as ESPHome substitutes it: YAML's true is the string true."""
    return str(v).lower() if isinstance(v, bool) else str(v)


def load(esp, path, vars_):
    """Load a package with ${var} filled in from its defaults and vars_."""
    text = open(f"{esp}/{path}").read()
    defaults = yaml.load(text, Loader=Loader).get("defaults", {}) or {}
    subst = {**{k: text_of(v) for k, v in defaults.items()}, **{k: text_of(v) for k, v in vars_.items()}}
    subst = {k: re.sub(r"\$\{(\w+)\}", lambda m: subst.get(m.group(1), m.group(0)), v) for k, v in subst.items()}
    text = re.sub(r"\$\{(\w+)\}", lambda m: subst.get(m.group(1), m.group(0)), text)
    left = re.findall(r"\$\{\w+\}", text)
    if left:
        sys.exit(f"{path}: no value for {left}")
    return yaml.load(text, Loader=Loader)


def packages(esp, path, vars_):
    """The package and the ones it includes under packages:, with their vars."""
    doc = load(esp, path, vars_)
    yield path, doc
    for inc in (doc.get("packages") or {}).values():
        if isinstance(inc, dict) and "file" in inc:
            inc_path = os.path.normpath(os.path.join(os.path.dirname(path), inc["file"]))
            yield from packages(esp, inc_path, inc.get("vars", {}))


def esp_ids(code):
    """ESPHome turns both id(x). and id(x)-> into x-> inside lambdas. Our id(x) returns a reference, like
    ESPHome's outside lambdas, so id(x)-> becomes id(x)."""
    return re.sub(r"\bid\((\w+)\)->", r"id(\1).", code)


def lambdas(actions):
    """Each action's code. homeassistant.action goes in stubs.h's ha_actions, wait_until and delay don't wait, and an
    action it can't run stops gen.py."""
    out = []
    for a in actions or []:
        if isinstance(a, dict) and ("wait_until" in a or "delay" in a):
            out.append(f"/* {next(iter(a))}: the sim goes straight on */")
        elif isinstance(a, dict) and "lambda" in a:
            out.append(esp_ids(a["lambda"]))
        elif isinstance(a, dict) and isinstance(a.get("script.execute"), str):
            out.append(f"id({a['script.execute']}).execute();")
        elif isinstance(a, dict) and "homeassistant.action" in a:
            h = a["homeassistant.action"]
            data = ", ".join(f'{{"{k}", "{v}"}}' for k, v in (h.get("data") or {}).items())
            out.append(f'ha_actions.push_back({{"{h["action"]}", {{{data}}}}});')
        else:
            sys.exit(f"gen.py can't run this action: {a}")
    return out


def inline(actions):
    """The actions' code on one line, for a callback."""
    return " ".join(lambdas(actions))


def then(v):
    return v.get("then", v) if isinstance(v, dict) else v


UNITS = {"us": 0.001, "ms": 1, "s": 1000, "min": 60000, "h": 3600000, "d": 86400000}


def period_ms(v):
    """An ESPHome time period like 20ms or 5min, in ms."""
    m = re.fullmatch(r"\s*([\d.]+)\s*(us|ms|s|min|h|d)\s*", str(v))
    if not m:
        sys.exit(f"gen.py can't read the time period {v}")
    return round(float(m.group(1)) * UNITS[m.group(2)])


PTYPE = {"int": "int", "string": "std::string", "bool": "bool", "float": "float"}
X_TYPE = {"sensor": "float", "binary_sensor": "bool", "text_sensor": "std::string"}  # the type of x in on_value
# stubs.h's class for each kind of entity
CLASS = {
    "select": "Select",
    "number": "Number",
    "switch": "Switch",
    "button": "Button",
    "sensor": "Sensor",
    "binary_sensor": "BinarySensor",
    "text_sensor": "TextSensor",
    "event": "Event",
}

FIRMWARE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
PROTO = os.path.join(FIRMWARE, "proto")


def check_include(full, f):
    """The .c files and the headers proto/ has are copies of proto/, which the harness builds anyway, so they must
    match."""
    ours = os.path.join(PROTO, f)
    if os.path.exists(ours):
        differs = not filecmp.cmp(full, ours, shallow=False)
    else:
        differs = f.endswith(".c")
    if differs:
        sys.exit(f"{full} isn't the same as proto/{f}: run make -C sim esp_sync")


def check_proto(esp):
    """Every proto/ file has a copy, and proto.lock has the copies as esp_sync made them. check_include() checks
    they're the same as proto/."""
    panel = os.path.join(esp, esp_sync.PANEL)
    names = esp_sync.proto_files(FIRMWARE)
    for n in names:
        if not os.path.exists(os.path.join(panel, n)):
            sys.exit(f"{panel} has no copy of proto/{n}: run make -C sim esp_sync")
    lock = esp_sync.read_lock(os.path.join(panel, esp_sync.LOCK))
    if lock is None:
        sys.exit(f"{panel} has no {esp_sync.LOCK}: run make -C sim esp_sync")
    hashes = lock[1]
    for n in sorted(set(names) | set(hashes)):
        if n not in hashes:
            sys.exit(f"{esp_sync.LOCK} has no hash for {n}: run make -C sim esp_sync")
        if n not in names:
            sys.exit(f"{esp_sync.LOCK} has {n}, which proto/ doesn't: run make -C sim esp_sync to remove it")
        if esp_sync.digest(os.path.join(panel, n)) != hashes[n]:
            sys.exit(
                f"{os.path.join(panel, n)} isn't the copy {esp_sync.LOCK} has: change proto/{n} instead and run "
                "make -C sim esp_sync"
            )


def collect(esp):
    """What the harness needs from the packages in FILES."""
    y = {"globals": [], "scripts": [], "boots": [], "intervals": [], "entities": {}, "images": [], "headers": []}
    for path, doc in (p for top in FILES for p in packages(esp, *top)):
        # esphome includes are relative to the package. Headers go in after the globals, like ESPHome does it.
        for inc in (doc.get("esphome") or {}).get("includes", []) or []:
            where = os.path.normpath(os.path.join(esp, os.path.dirname(path), inc))
            files = sorted(os.listdir(where)) if os.path.isdir(where) else [os.path.basename(where)]
            base = where if os.path.isdir(where) else os.path.dirname(where)
            for f in files:
                if f.startswith("update"):
                    continue  # update.yaml's panel updater, tested on its own against the bootloader
                full = os.path.join(base, f)
                check_include(full, f)
                if f.endswith((".h", ".hpp")):
                    y["headers"].append(full)
        y["globals"] += doc.get("globals", [])
        y["scripts"] += doc.get("script", [])
        for b in (doc.get("esphome") or {}).get("on_boot", []):
            y["boots"].append((b.get("priority", 600) if isinstance(b, dict) else 600, lambdas(then(b))))
        for i in doc.get("interval", []):
            y["intervals"] += [(period_ms(i["interval"]), code) for code in lambdas(then(i))]
        for kind in CLASS:
            for e in doc.get(kind, []) or []:
                eid = e.get("id")
                if eid is None:
                    continue
                if isinstance(eid, Extend):  # !extend adds options to an existing select
                    y["entities"][str(eid)][1].setdefault("options", []).extend(e.get("options", []))
                    continue
                y["entities"][eid] = (kind, e)
        y["images"] += doc.get("image", []) or []  # declared in extra.h, only their triggers are wired here
    return y


def emit(y):
    """The harness's C++ as lines."""
    entities = y["entities"]
    out = [
        "// generated by gen.py from the ESP repo's packages, don't edit",
        '#include "stubs.h"',
        '#include "extra.h"',
        "",
    ]
    for g in y["globals"]:
        init = g.get("initial_value")
        out.append(f"static {g['type']} {g['id']}{'{' + str(init) + '}' if init is not None else '{}'};")
    out.append("")
    for eid, (kind, e) in entities.items():
        # template selects and numbers with a lambda have no state until it gives one, the rest start at their
        # initial value, which for a number is min_value unless set
        if kind == "select":
            opts = ", ".join('"%s"' % o for o in e["options"])
            init = "nullptr" if "lambda" in e else '"%s"' % e.get("initial_option", e["options"][0])
            out.append(f"Select {eid}{{{{{opts}}}, {init}}};")
        elif kind == "number":
            init = "NAN" if "lambda" in e else e.get("initial_value", e["min_value"])
            out.append(f"Number {eid}{{{e['min_value']}, {e['max_value']}, {e['step']}, {init}}};")
        else:
            out.append(f"{CLASS[kind]} {eid};")
    out.append("")
    params = {
        s["id"]: ", ".join(f"{PTYPE[t]} {n}" for n, t in (s.get("parameters") or {}).items()) for s in y["scripts"]
    }
    for s in y["scripts"]:
        out.append(f"struct Script_{s['id']} {{ void execute({params[s['id']]}); }} {s['id']};")
    out.append("")
    for h in y["headers"]:
        out.append(f'#include "{h}"')
    out.append("")
    for s in y["scripts"]:
        body = "\n".join(lambdas(s["then"]))
        out.append(f"void Script_{s['id']}::execute({params[s['id']]}) {{\n{body}\n}}")
    out.append("")
    out.append("void yaml_boot() {")
    for _, codes in sorted(y["boots"], key=lambda b: -b[0]):  # higher priority first, as ESPHome runs them
        for b in codes:
            out.append(f"  []() {{\n{b}\n  }}();")
    out.append("}")
    # the harness steps 20 ms at a time, and runs the slower ones when a test wants them
    out.append("void yaml_interval() {")
    for ms, b in y["intervals"]:
        if ms <= 20:
            out.append(f"  []() {{\n{b}\n  }}();")
    out.append("}")
    out.append("void yaml_every(uint32_t ms) {")
    out.append("  (void) ms;")
    for ms, b in y["intervals"]:
        if ms > 20:
            out.append(f"  if (ms == {ms})\n    []() {{\n{b}\n  }}();")
    out.append("}")
    # template switches with a lambda get their state from it every loop, as ESPHome's do
    out.append("void yaml_loop() {")
    for eid, (kind, e) in entities.items():
        if kind == "switch" and "lambda" in e:
            out.append(f"  {eid}.publish_state([]() -> bool {{\n{esp_ids(e['lambda'])}\n  }}());")
    out.append("}")
    out.append("void yaml_wire() {")
    for eid, (kind, e) in entities.items():
        if kind in ("select", "number") and not e.get("optimistic"):
            out.append(f"  {eid}.optimistic = false;")
        if kind == "select" and "set_action" in e:
            out.append(f"  {eid}.set_action = [](StringRef x) {{ {inline(e['set_action'])} }};")
        if kind == "select" and "on_value" in e:
            out.append(f"  {eid}.add_on_state_callback([](size_t) {{ {inline(e['on_value'])} }});")
        if kind == "number" and "set_action" in e:
            out.append(f"  {eid}.set_action = [](float x) {{ {inline(e['set_action'])} }};")
        if kind == "switch":
            for a, f in (("turn_on_action", "on_action"), ("turn_off_action", "off_action")):
                if a in e:
                    out.append(f"  {eid}.{f} = []() {{ {inline(e[a])} }};")
            for a, on in (("on_turn_on", "x"), ("on_turn_off", "!x")):
                if a in e:
                    out.append(f"  {eid}.add_on_state_callback([](bool x) {{ if ({on}) {{ {inline(e[a])} }} }});")
        if kind == "button" and "on_press" in e:
            out.append(f"  {eid}.on_press = []() {{ {inline(e['on_press'])} }};")
        if kind in X_TYPE and "on_value" in e:
            out.append(f"  {eid}.add_on_state_callback([]({X_TYPE[kind]} x) {{ {inline(e['on_value'])} }});")
    for e in y["images"]:
        for t in ("on_download_finished", "on_error"):
            if t in e:
                out.append(f"  {e['id']}.{t} = []() {{ {inline(e[t])} }};")
    out.append("}")
    return out


def main():
    esp, out = sys.argv[1], sys.argv[2]
    check_proto(esp)
    y = collect(esp)
    open(out, "w").write("\n".join(emit(y)) + "\n")
    print(
        f"{out}: {len(y['globals'])} globals, {len(y['scripts'])} scripts, {len(y['boots'])} boot and "
        f"{len(y['intervals'])} interval lambdas, {len(y['entities'])} entities"
    )


if __name__ == "__main__":
    main()
