"""Pulls the review out of whatever the reviewer actually said.

The prompt asks for JSON and nothing else. Models wrap it in prose anyway,
and a review that cannot be read must not come back looking like "no
findings" - that would turn a failed review into a silent pass, which is
the one outcome this whole arrangement exists to prevent.

  python parse_review.py <raw> <findings.json>
"""
import io
import json
import sys


def first_object(text):
    """The first balanced {...} in the text, or None. Braces inside strings
    are skipped, because a finding that quotes code is normal."""
    start = text.find("{")
    if start < 0:
        return None

    depth = 0
    in_string = False
    escaped = False
    for i in range(start, len(text)):
        c = text[i]
        if in_string:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                in_string = False
            continue
        if c == '"':
            in_string = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                try:
                    return json.loads(text[start:i + 1])
                except Exception:
                    return None
    return None


def main(raw_path, out_path):
    raw = io.open(raw_path, encoding="utf-8", errors="replace").read()
    obj = first_object(raw)

    if not isinstance(obj, dict) or "findings" not in obj:
        obj = {"verdict": "unreadable", "findings": []}

    findings = obj.get("findings")
    if not isinstance(findings, list):
        # The verdict may say "sound", but a findings field that is not a
        # list means the reply was not the shape that was asked for, and a
        # review that cannot be read is not a pass.
        obj = {"verdict": "unreadable", "findings": []}
        findings = []
    # A finding with no severity is treated as serious. Guessing downwards
    # would let a malformed one through unlooked at.
    for f in findings:
        if isinstance(f, dict) and f.get("severity") not in ("high", "medium", "low"):
            f["severity"] = "high"
    obj["findings"] = [f for f in findings if isinstance(f, dict)]

    io.open(out_path, "w", encoding="utf-8").write(json.dumps(obj, indent=2))
    high = sum(1 for f in obj["findings"] if f.get("severity") == "high")
    print("%s, %d finding(s), %d serious"
          % (obj.get("verdict", "?"), len(obj["findings"]), high))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
