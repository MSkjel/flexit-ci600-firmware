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

import yaml

ESP = sys.argv[1]
OUT = sys.argv[2]

# Packages the harness runs and the vars for each, roughly what tests/panel.yaml in the ESP repo uses
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
    ("packages/panel/switch.yaml", {"entity": "heater", "label": "Heater|Varme", "icon": "radiator"}),
    (
        "packages/panel/number.yaml",
        {"entity": "temperature", "label": "Setpoint", "unit": "C", "icon": "thermo", "page": "comfort"},
    ),
    (
        "packages/panel/button.yaml",
        {"entity": "clear_alarms", "label": "Clear alarms", "icon": "bell", "view": "false"},
    ),
    ("packages/panel/picture.yaml", {"name": "dusk", "file": "x.png"}),
    ("packages/panel/animation.yaml", {"name": "ball", "file": "x.gif", "x": 250, "y": 20}),
    ("packages/panel/animation.yaml", {"name": "noise", "file": "y.gif", "resize": "64x64"}),
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


def load(path, vars_):
    """Load a package with ${var} filled in from its defaults and vars_."""
    text = open(f"{ESP}/{path}").read()
    defaults = yaml.load(text, Loader=Loader).get("defaults", {}) or {}
    subst = {**{k: str(v) for k, v in defaults.items()}, **{k: str(v) for k, v in vars_.items()}}
    text = re.sub(r"\$\{(\w+)\}", lambda m: subst.get(m.group(1), m.group(0)), text)
    left = re.findall(r"\$\{\w+\}", text)
    if left:
        sys.exit(f"{path}: no value for {left}")
    return yaml.load(text, Loader=Loader)


def packages(path, vars_):
    """The package and the ones it includes under packages:, with their vars."""
    doc = load(path, vars_)
    yield path, doc
    for inc in (doc.get("packages") or {}).values():
        if isinstance(inc, dict) and "file" in inc:
            yield from packages(os.path.normpath(os.path.join(os.path.dirname(path), inc["file"])), inc.get("vars", {}))


def esp_ids(code):
    """ESPHome turns both id(x). and id(x)-> into x-> inside lambdas. Our id(x) returns a reference, like
    ESPHome's outside lambdas, so id(x)-> becomes id(x)."""
    return re.sub(r"\bid\((\w+)\)->", r"id(\1).", code)


def lambdas(actions):
    """The code for each action in a list. The packages only use lambda and script.execute actions."""
    out = []
    for a in actions or []:
        if isinstance(a, dict) and "lambda" in a:
            out.append(esp_ids(a["lambda"]))
        elif isinstance(a, dict) and "script.execute" in a:
            s = a["script.execute"]
            out.append(f"id({s}).execute();" if isinstance(s, str) else None)
    return [x for x in out if x]


def then(v):
    return v.get("then", v) if isinstance(v, dict) else v


PTYPE = {"int": "int", "string": "std::string", "bool": "bool", "float": "float"}

PROTO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "proto")

globals_, scripts, boots, intervals, wiring, entities, headers = [], [], [], [], [], {}, []
for path, doc in (p for top in FILES for p in packages(*top)):
    # esphome includes are relative to the package. Headers go in after the globals, like ESPHome does it.
    # The .c files and their headers are copies of proto/, which the harness builds anyway, so they must match.
    for inc in (doc.get("esphome") or {}).get("includes", []) or []:
        where = os.path.join(ESP, os.path.dirname(path), inc)
        files = sorted(os.listdir(where)) if os.path.isdir(where) else [os.path.basename(where)]
        base = where if os.path.isdir(where) else os.path.dirname(where)
        for f in files:
            full = os.path.join(base, f)
            if f == "update.h":
                continue  # update.yaml's panel updater, tested on its own against the bootloader
            ours = os.path.join(PROTO, f)
            if (f.endswith(".c") or os.path.exists(ours)) and not (
                os.path.exists(ours) and filecmp.cmp(full, ours, shallow=False)
            ):
                sys.exit(f"{full} isn't the same as proto/{f}: run make esp_sync")
            if f.endswith((".h", ".hpp")):
                headers.append(full)
    for g in doc.get("globals", []):
        globals_.append(g)
    for s in doc.get("script", []):
        scripts.append(s)
    for b in (doc.get("esphome") or {}).get("on_boot", []):
        boots.extend(lambdas(then(b)))
    for i in doc.get("interval", []):
        intervals.extend(lambdas(then(i)))
    for kind in ("select", "number", "switch", "button", "sensor", "binary_sensor", "text_sensor", "event"):
        for e in doc.get(kind, []) or []:
            eid = e.get("id")
            if eid is None:
                continue
            if isinstance(eid, Extend):  # !extend adds options to an existing select
                entities[str(eid)][1].setdefault("options", []).extend(e.get("options", []))
                continue
            entities[eid] = (kind, e)

out = [
    "// generated by gen.py from the ESP repo's packages, don't edit",
    '#include "stubs.h"',
    '#include "extra.h"',
    "",
]
for g in globals_:
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
    elif kind == "switch":
        out.append(f"Switch {eid};")
    elif kind == "button":
        out.append(f"Button {eid};")
    elif kind == "sensor":
        out.append(f"Sensor {eid};")
    elif kind == "binary_sensor":
        out.append(f"BinarySensor {eid};")
    elif kind == "text_sensor":
        out.append(f"TextSensor {eid};")
    elif kind == "event":
        out.append(f"Event {eid};")
out.append("")
for s in scripts:
    params = ", ".join(f"{PTYPE[t]} {n}" for n, t in (s.get("parameters") or {}).items())
    out.append(f"struct Script_{s['id']} {{ void execute({params}); }} {s['id']};")
out.append("")
for h in headers:
    out.append(f'#include "{h}"')
out.append("")
for s in scripts:
    params = ", ".join(f"{PTYPE[t]} {n}" for n, t in (s.get("parameters") or {}).items())
    body = "\n".join(lambdas(s["then"]))
    out.append(f"void Script_{s['id']}::execute({params}) {{\n{body}\n}}")
out.append("")
out.append("void yaml_boot() {")
for b in boots:
    out.append(f"  []() {{\n{b}\n  }}();")
out.append("}")
out.append("void yaml_interval() {")
for b in intervals:
    out.append(f"  []() {{\n{b}\n  }}();")
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
        out.append(f"  {eid}.set_action = [](StringRef x) {{ {' '.join(lambdas(e['set_action']))} }};")
    if kind == "select" and "on_value" in e:
        out.append(f"  {eid}.add_on_state_callback([](size_t) {{ {' '.join(lambdas(e['on_value']))} }});")
    if kind == "number" and "set_action" in e:
        out.append(f"  {eid}.set_action = [](float x) {{ {' '.join(lambdas(e['set_action']))} }};")
    if kind == "switch":
        for a, f in (("turn_on_action", "on_action"), ("turn_off_action", "off_action")):
            if a in e:
                out.append(f"  {eid}.{f} = []() {{ {' '.join(lambdas(e[a]))} }};")
    if kind == "button" and "on_press" in e:
        out.append(f"  {eid}.on_press = []() {{ {' '.join(lambdas(e['on_press']))} }};")
out.append("}")
open(OUT, "w").write("\n".join(out) + "\n")
print(
    f"{OUT}: {len(globals_)} globals, {len(scripts)} scripts, {len(boots)} boot and {len(intervals)} interval lambdas, "
    f"{len(entities)} entities"
)
