#!/usr/bin/env python3
"""What changed in a core since its last nightly, for the release page
(chimera issue 357). Run inside the core's checkout, with its history:

    release-changes.py --repo <owner/name> --since <tag> --to <commit>

prints a Markdown section listing every commit after <tag> up to <commit>,
each linked, with the Chimera issues its subject names ("chimera#352",
"chimera #26") linked to Chimera's issues: in a core's release page GitHub
links only "#352", and that would be the core's own issue 352.

    release-changes.py --self-test

checks the formatting on subjects written for it."""
import re
import subprocess
import sys

CHIMERA = "https://github.com/ToolAssisted-run/chimera/issues/"
# "chimera#352", "chimera #26", "Chimera#7": an issue of Chimera's, as the
# cores' commit subjects name them
ISSUE = re.compile(r"\b[Cc]himera ?#(\d+)\b")
MOST = 50


def line(repo, sha, subject):
    """One commit as a list item: its short hash linked, its subject with
    Chimera's issues linked. Markdown the subject might hold is escaped."""
    text = subject.replace("\\", "\\\\")
    for c in "*_`[]<>|":
        text = text.replace(c, "\\" + c)
    text = ISSUE.sub(lambda m: f"[chimera#{m.group(1)}]({CHIMERA}{m.group(1)})", text)
    return f"- [`{sha[:7]}`](https://github.com/{repo}/commit/{sha}) {text}"


def section(repo, since, to, commits):
    """commits: (sha, subject), newest first"""
    out = ["### What changed", ""]
    if since is None:
        out.append("This is the first nightly of this core.")
        return "\n".join(out) + "\n"
    since_link = f"[{since}](https://github.com/{repo}/releases/tag/{since})"
    if not commits:
        out.append(f"Nothing since {since_link}: this is the same commit.")
        return "\n".join(out) + "\n"
    n = len(commits)
    out.append(f"{n} commit{'s' if n != 1 else ''} since {since_link}:")
    out.append("")
    for sha, subject in commits[:MOST]:
        out.append(line(repo, sha, subject))
    if n > MOST:
        out.append(f"- and [{n - MOST} more](https://github.com/{repo}/compare/{since}...{to})")
    return "\n".join(out) + "\n"


def commits_between(since, to):
    log = subprocess.run(["git", "log", "--format=%H%x1f%s%x1e", f"{since}..{to}"],
                         capture_output=True, text=True, check=True).stdout
    return [tuple(rec.strip("\n").split("\x1f", 1)) for rec in log.split("\x1e") if rec.strip("\n")]


def self_test():
    failures = 0

    def check(what, got, want):
        nonlocal failures
        if got != want:
            failures += 1
            print(f"FAIL {what}:\n  got  {got!r}\n  want {want!r}")

    repo = "ToolAssisted-run/chimera-core-x68k"
    sha = "151d92ad0123456789abcdef0123456789abcdef"
    check("a Chimera issue is linked to Chimera",
          line(repo, sha, "fix(x68k): more floppies (chimera#352, chimera #26)"),
          f"- [`151d92a`](https://github.com/{repo}/commit/{sha}) fix(x68k): more floppies "
          f"([chimera#352]({CHIMERA}352), [chimera#26]({CHIMERA}26))")
    check("a core's own issue is left for GitHub to link",
          line(repo, sha, "fix: the menu (#4)"),
          f"- [`151d92a`](https://github.com/{repo}/commit/{sha}) fix: the menu (#4)")
    check("markdown in a subject is shown as written",
          line(repo, sha, "feat: *bold* [x] <y> a|b `c`"),
          f"- [`151d92a`](https://github.com/{repo}/commit/{sha}) feat: \\*bold\\* \\[x\\] \\<y\\> a\\|b \\`c\\`")
    check("a word ending in chimera is not an issue",
          line(repo, sha, "notchimera#5"),
          f"- [`151d92a`](https://github.com/{repo}/commit/{sha}) notchimera#5")
    check("no earlier nightly", section(repo, None, sha, []), "### What changed\n\nThis is the first nightly of this core.\n")
    check("the same commit", section(repo, "nightly-2026-10-21", sha, []),
          f"### What changed\n\nNothing since [nightly-2026-10-21](https://github.com/{repo}/releases/tag/nightly-2026-10-21): this is the same commit.\n")
    many = section(repo, "nightly-2026-10-21", sha, [(sha, f"c{i}") for i in range(MOST + 3)])
    check("a long list ends with a link to the rest", many.splitlines()[-1],
          f"- and [3 more](https://github.com/{repo}/compare/nightly-2026-10-21...{sha})")
    check("one commit is a commit", section(repo, "nightly-2026-10-21", sha, [(sha, "x")]).splitlines()[2],
          f"1 commit since [nightly-2026-10-21](https://github.com/{repo}/releases/tag/nightly-2026-10-21):")
    print(f"release-changes self-test: {'all passed' if failures == 0 else f'{failures} failed'}")
    return 1 if failures else 0


def main(argv):
    if argv[1:] == ["--self-test"]:
        return self_test()
    args = dict(zip(argv[1::2], argv[2::2]))
    if "--repo" not in args or "--to" not in args:
        print(__doc__, file=sys.stderr)
        return 2
    since = args.get("--since") or None
    commits = commits_between(since, args["--to"]) if since else []
    sys.stdout.write(section(args["--repo"], since, args["--to"], commits))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
