#!/bin/bash
# Publishes a core's built packages, one per CPU, as a GitHub release of the core's OWN
# repository. Called by .github/workflows/publish-core.yml, which every core's
# CI reuses - the logic lives here, in one file, rather than as fifteen copies
# of the same YAML drifting apart.
#
# One kind of release, as the frontend publishes (see release.yml): a nightly,
# an IMMUTABLE dated release (the newest is GitHub's Latest), published only
# when main moved. NEVER deleted: a movie cites the core build that recorded
# it, and someone replaying that run in five years needs THAT package, because
# a package's identity is a function of its toolchain and the same sources
# through a later gcc are different bytes. There is no rolling development
# build: a build that is replaced cannot be cited (user-decided, 2026-11-05).
#
# A package is machine code for ONE CPU - miniBox runs core.wbx directly and
# refuses a guest built for another (the host's ELF machine check) - so a
# release carries one package per CPU the core builds for, all of one version,
# each named <core-id>-<version>-<arch>.chimeraCore. The arch is the caller's,
# the CPU the gate that built the package ran on (x86_64 or aarch64, the names
# Chimera's own release bundles use); a
# core.wbx inside must agree with it, and the package must list at least one
# system of that CPU in its runsOn (docs/porting-a-core.md). The version is read OUT OF THE PACKAGE
# rather than passed in: it is what the build stamped into waterbox.config, it
# is what a movie cites, and it is what the core manager checks the download
# against. Anything else is a way for the release and the package to disagree.
#
# Usage: publish-core.sh --package <file> [--arch <cpu>] [--package <file> --arch <cpu>]...
#                        --core-id <id>
#                        [--sha <commit>] [--notes <file>] [--dry-run]
# Each --arch names the CPU of the --package before it. A lone package with no
# --arch is x86_64, the CPU every core built for before aarch64.
set -eu

packages=()
arches=()
core_id=""
sha=""
notes=""
dry=0
while [ $# -gt 0 ]; do
	case "$1" in
		--package) packages+=("$2"); arches+=(""); shift 2 ;;
		--arch)
			[ "${#packages[@]}" -gt 0 ] || { echo "--arch names the CPU of the --package before it" >&2; exit 2; }
			arches[$((${#arches[@]} - 1))]="$2"; shift 2 ;;
		--core-id) core_id="$2"; shift 2 ;;
		--sha) sha="$2"; shift 2 ;;
		--notes) notes="$2"; shift 2 ;;
		--dry-run) dry=1; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "${#packages[@]}" -gt 0 ] && [ -n "$core_id" ] || {
	echo "usage: publish-core.sh --package <file> [--arch <cpu>]... --core-id <id>" >&2; exit 2; }
if [ "${#packages[@]}" -eq 1 ] && [ -z "${arches[0]}" ]; then
	arches[0]=x86_64
fi

[ -n "$sha" ] || sha="$(git rev-parse HEAD)"
date="$(date -u +%Y-%m-%d)"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

# ---- what each package says it is -------------------------------------------
version=""
version_date=""
assets=()
asset_systems=()
for i in "${!packages[@]}"; do
	package="${packages[$i]}"
	arch="${arches[$i]}"
	[ -f "$package" ] || { echo "no package at $package" >&2; exit 1; }
	case "$arch" in
		x86_64|aarch64) ;;
		"") echo "$package: no --arch; with more than one package, each names its CPU" >&2; exit 2 ;;
		*) echo "$package: arch must be x86_64 or aarch64, not $arch" >&2; exit 2 ;;
	esac
	for a in "${assets[@]+"${assets[@]}"}"; do
		case "$a" in *-"$arch".chimeraCore)
			echo "two packages for $arch" >&2; exit 2 ;;
		esac
	done
	# the version it stamps, when that commit was made (versionDate, UTC), the
	# CPU its core.wbx is built for (ELF machine at 18: 62 x86-64, 183
	# aarch64), if it carries one, and the systems it says it runs on
	read -r v vdate machine runs_on < <(python3 - "$package" <<'PY'
import json, struct, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    config = json.loads(z.read("waterbox.config").decode("utf-8"))
    v = config.get("version", "")
    vdate = config.get("versionDate", "")
    machine = "-"
    if "core.wbx" in z.namelist():
        head = z.open("core.wbx").read(20)
        if head[:4] == b"\x7fELF" and len(head) == 20:
            machine = {62: "x86_64", 183: "aarch64"}.get(struct.unpack_from("<H", head, 18)[0], "other")
    runs_on = config.get("runsOn")
    runs_on = ",".join(r for r in runs_on if isinstance(r, str)) if isinstance(runs_on, list) else ""
    print(v or "-", vdate.replace(" ", "T") or "-", machine, runs_on or "-")
PY
)
	[ "$v" != - ] || { echo "$package stamps no version; the build must set CORE_VERSION" >&2; exit 1; }
	case "$v" in
		*+local*|*-dirty*)
			# a hand-built package is nobody else's build: publishing one would put a
			# version nothing can reproduce into somebody's movie header
			echo "refusing to publish a hand-built package (version $v)" >&2
			exit 1 ;;
	esac
	[ -z "$version" ] || [ "$v" = "$version" ] || {
		echo "the packages are of two versions, $version and $v; a release is one" >&2; exit 1; }
	version="$v"
	[ "$vdate" = - ] || version_date="$vdate"
	[ "$machine" = - ] || [ "$machine" = "$arch" ] || {
		echo "$package is given as $arch, but its core.wbx is built for $machine" >&2; exit 1; }
	# where it runs is the package's to say, and it must say it of its own CPU
	case ",$runs_on," in
		*-"$arch",*) ;;
		*) echo "$package lists no system of its CPU, $arch, in runsOn (it lists: $runs_on)" >&2; exit 1 ;;
	esac
	asset="$core_id-$version-$arch.chimeraCore"
	cp "$package" "$staging/$asset"
	assets+=("$asset")
	# the systems of its own CPU, for the notes: "windows, linux"
	systems=$(printf '%s\n' "${runs_on//,/$'\n'}" | sed -n "s/-$arch\$//p" | paste -sd, - | sed 's/,/, /g')
	asset_systems+=("$systems")
done
# When the commit was made, as the package states it (versionDate, UTC), on
# the clock Chimera shows it in (issue 331): "2026-10-18 18:35 CEST". The
# release says it the same way, so the version a person sees in Chimera can
# be found on the release page. Empty for a package that states no date.
made=""
[ -z "$version_date" ] || made="$(python3 "$(dirname "$0")/european-time.py" "$version_date")"
# the title of a nightly is its tag's date: see below for which tag that is
nightly_title() { echo "Nightly $1 (${version:0:8}${made:+, made $made})"; }
title="$(nightly_title "$date")"

# What changed since the last nightly (issue 357): the commits after that
# nightly's tag up to this build's, from the core's own history, which the
# workflow checks out whole. Today's nightly tag is passed over - this may be
# the run finishing it. Nothing is said when the history cannot be read.
repo_slug="${GITHUB_REPOSITORY:-}"
[ -n "$repo_slug" ] || repo_slug="$(gh repo view --json nameWithOwner --jq .nameWithOwner 2>/dev/null || true)"
previous=""
for t in $(git tag -l 'nightly-*' --sort=-creatordate 2>/dev/null); do
	[ "$t" = "nightly-$date" ] && continue
	previous="$t"
	break
done
changes=""
if [ -n "$repo_slug" ] && git rev-parse --git-dir >/dev/null 2>&1; then
	changes="$(python3 "$(dirname "$0")/release-changes.py" --repo "$repo_slug" --since "$previous" --to "$sha" 2>/dev/null || true)"
fi

if [ -z "$notes" ]; then
	notes="$staging/NOTES.md"
	{
		echo "Core package for [Chimera](https://github.com/ToolAssisted-run/chimera)."
		echo
		echo "Download the \`.chimeraCore\` file below for your machine's CPU and put"
		echo "it in Chimera's \`Cores\` folder (**Tools > Core Manager** shows where"
		echo "that is). A package runs only on the CPU it is built for, and only on the"
		echo "systems it lists below. Chimera downloads nothing itself."
		echo
		case " ${arches[*]} " in
			*" aarch64 "*)
				echo "**On Windows or Linux you most likely want the x86_64 package.** Take the"
				echo "aarch64 one only if you know you are using an ARM device. On a Mac, take"
				echo "aarch64 for Apple silicon and x86_64 for an Intel Mac. Put only one of them"
				echo "in the folder."
				echo ;;
		esac
		echo "| | |"
		echo "|---|---|"
		echo "| Core | \`$core_id\` |"
		echo "| Version | \`$version\` |"
		[ -z "$made" ] || echo "| Made | $made (Chimera lists it as \`$made  (${version:0:8})\`) |"
		echo "| Built from | \`$sha\` |"
		for i in "${!assets[@]}"; do
			a="${assets[$i]}"
			arch="${a%.chimeraCore}"; arch="${arch##*-}"
			echo "| $arch | \`$a\`, for ${asset_systems[$i]} |"
		done
		echo
		echo "This is a **nightly**: it is never deleted. A movie recorded on this"
		echo "package can be replayed against it for as long as this repository"
		echo "exists."
		if [ -n "$changes" ]; then
			echo
			printf '%s\n' "$changes"
		fi
	} > "$notes"
fi

echo "publishing ${assets[*]} from $sha"
if [ "$dry" -eq 1 ]; then
	echo "--dry-run: would publish \"$title\""
	sed 's/^/  | /' "$notes"
	exit 0
fi

# GitHub's API answers 5xx now and then, and a publish is two calls - create the
# release, then upload into it - so one bad answer between them used to leave a
# release with nothing in it. That is worse than no release: the nightly rule
# below then refused to touch it again, and every consumer that picks "the
# newest nightly" found an empty one (2026-09-13: flycast got a 500 on the
# upload, ares a 502 on a create that had in fact succeeded, and the frontend's
# CI could not download either). So every call is retried, and a release is
# ENSURED rather than created: a create whose answer was lost may still have
# happened, which is why it looks before it tries again.
retry() {
	local n
	for n in 1 2 3 4; do
		"$@" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: $1 $2 $3" >&2; sleep $((n * 15)); }
	done
	return 1
}
# A nightly is a full release and the one GitHub shows as Latest. The index
# release is made elsewhere and stays a pre-release.
ensure_release() { # <tag> <title>
	local n
	for n in 1 2 3 4; do
		gh release view "$1" >/dev/null 2>&1 && return 0
		gh release create "$1" --target "$sha" --latest --title "$2" --notes-file "$notes" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: release $1" >&2; sleep $((n * 15)); }
	done
	return 1
}

# Published only when main moved: a nightly already at this commit (the
# newest by its name - the releases API makes lightweight tags, whose
# creator date is the commit's, so the date in the name is the only order)
# is this build's nightly, and today's is not made. Otherwise it is today's.
git fetch --tags --force --quiet 2>/dev/null || true
tag=$(git tag --points-at "$sha" --sort=-refname -l 'nightly-*' | head -1)
[ -n "$tag" ] || tag="nightly-$date"
# A package already in a nightly is never replaced: somebody's movie may
# name it. A nightly carrying a package of this core that is not one of
# these (another version, or a name before packages carried their CPU) is
# never republished. One that lacks some of these is a publish that died
# half way, and finishing it is the only way it ever becomes what its name
# promises - so what it lacks goes up, and nothing else.
have=$(gh release view "$tag" --json assets --jq '.assets[].name' 2>/dev/null | grep "^$core_id-.*\.chimeraCore$" || true)
for h in $have; do
	case " ${assets[*]} " in
		*" $h "*) ;;
		*) echo "$tag already carries $h; a nightly is never republished"; exit 0 ;;
	esac
done
ensure_release "$tag" "$(nightly_title "${tag#nightly-}")"
for a in "${assets[@]}"; do
	if printf '%s\n' "$have" | grep -qx "$a"; then
		echo "$tag already carries $a"
		continue
	fi
	retry gh release upload "$tag" "$staging/$a" --clobber
	echo "published $a"
done
