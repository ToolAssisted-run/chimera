#!/bin/bash
# Publishes a core's built packages, one per CPU, as a GitHub release of the core's OWN
# repository. Called by .github/workflows/publish-core.yml, which every core's
# CI reuses - the logic lives here, in one file, rather than as fifteen copies
# of the same YAML drifting apart.
#
# Two kinds of release, the same two the frontend publishes (see release.yml):
#
#   dev      a ROLLING prerelease, replaced on every green push to main.
#   nightly  an IMMUTABLE dated release (the newest is GitHub's Latest), published only
#            when main moved. NEVER deleted: a movie cites the core build that
#            recorded it, and someone replaying that run in five years needs
#            THAT package, because a package's identity is a function of its
#            toolchain and the same sources through a later gcc are different
#            bytes.
#
# A package is machine code for ONE CPU - miniBox runs core.wbx directly and
# refuses a guest built for another (the host's ELF machine check) - so a
# release carries one package per CPU the core builds for, all of one version,
# each named <core-id>-<version>-<arch>.chimeraCore. The arch is the caller's,
# the CPU the gate that built the package ran on (x64 or arm64, the names
# Chimera's own release bundles use); a
# core.wbx inside must agree with it. The version is read OUT OF THE PACKAGE
# rather than passed in: it is what the build stamped into waterbox.config, it
# is what a movie cites, and it is what the core manager checks the download
# against. Anything else is a way for the release and the package to disagree.
#
# Usage: publish-core.sh --package <file> [--arch <cpu>] [--package <file> --arch <cpu>]...
#                        --core-id <id> [--kind dev|nightly]
#                        [--sha <commit>] [--notes <file>] [--dry-run]
# Each --arch names the CPU of the --package before it. A lone package with no
# --arch is x64, the CPU every core built for before arm64.
set -eu

packages=()
arches=()
core_id=""
kind=dev
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
		--kind) kind="$2"; shift 2 ;;
		--sha) sha="$2"; shift 2 ;;
		--notes) notes="$2"; shift 2 ;;
		--dry-run) dry=1; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "${#packages[@]}" -gt 0 ] && [ -n "$core_id" ] || {
	echo "usage: publish-core.sh --package <file> [--arch <cpu>]... --core-id <id> [--kind dev|nightly]" >&2; exit 2; }
case "$kind" in
	dev|nightly) ;;
	*) echo "kind must be dev or nightly" >&2; exit 2 ;;
esac
if [ "${#packages[@]}" -eq 1 ] && [ -z "${arches[0]}" ]; then
	arches[0]=x64
fi

[ -n "$sha" ] || sha="$(git rev-parse HEAD)"
date="$(date -u +%Y-%m-%d)"

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

# ---- what each package says it is -------------------------------------------
version=""
version_date=""
assets=()
for i in "${!packages[@]}"; do
	package="${packages[$i]}"
	arch="${arches[$i]}"
	[ -f "$package" ] || { echo "no package at $package" >&2; exit 1; }
	case "$arch" in
		x64|arm64) ;;
		"") echo "$package: no --arch; with more than one package, each names its CPU" >&2; exit 2 ;;
		*) echo "$package: arch must be x64 or arm64, not $arch" >&2; exit 2 ;;
	esac
	for a in "${assets[@]+"${assets[@]}"}"; do
		case "$a" in *-"$arch".chimeraCore)
			echo "two packages for $arch" >&2; exit 2 ;;
		esac
	done
	# the version it stamps, when that commit was made (versionDate, UTC), and
	# the CPU its core.wbx is built for (ELF machine at 18: 62 x86-64, 183
	# aarch64), if it carries one
	read -r v vdate machine < <(python3 - "$package" <<'PY'
import json, struct, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    config = json.loads(z.read("waterbox.config").decode("utf-8"))
    v = config.get("version", "")
    vdate = config.get("versionDate", "")
    machine = "-"
    if "core.wbx" in z.namelist():
        head = z.open("core.wbx").read(20)
        if head[:4] == b"\x7fELF" and len(head) == 20:
            machine = {62: "x64", 183: "arm64"}.get(struct.unpack_from("<H", head, 18)[0], "other")
    print(v or "-", vdate.replace(" ", "T") or "-", machine)
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
	asset="$core_id-$version-$arch.chimeraCore"
	cp "$package" "$staging/$asset"
	assets+=("$asset")
done
# When the commit was made, as the package states it (versionDate, UTC), on
# the clock Chimera shows it in (issue 331): "2026-10-18 18:35 CEST". The
# release says it the same way, so the version a person sees in Chimera can
# be found on the release page. Empty for a package that states no date.
made=""
[ -z "$version_date" ] || made="$(python3 "$(dirname "$0")/european-time.py" "$version_date")"
# the title of a nightly is its tag's date: see below for which tag that is
nightly_title() { echo "Nightly $1 (${version:0:8}${made:+, made $made})"; }
if [ "$kind" = dev ]; then
	title="Development build ${version:0:8}${made:+ (made $made)}"
else
	title="$(nightly_title "$date")"
fi

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
	changes="$(python3 "$(dirname "$0")/release-changes.py" --repo "$repo_slug" --since "$previous" --to "$sha" --kind "$kind" 2>/dev/null || true)"
fi

if [ -z "$notes" ]; then
	notes="$staging/NOTES.md"
	{
		echo "Core package for [Chimera](https://github.com/ToolAssisted-run/chimera)."
		echo
		echo "Download the \`.chimeraCore\` file below for your machine's CPU and put"
		echo "it in Chimera's \`Cores\` folder (**File > Core Manager** shows where"
		echo "that is). A package runs only on the CPU it is built for. Chimera"
		echo "downloads nothing itself."
		echo
		case " ${arches[*]} " in
			*" arm64 "*)
				echo "**You most likely want the x64 package.** Take the arm64 one only if you"
				echo "know you are using an arm64 device. Put only one of them in the folder."
				echo ;;
		esac
		echo "| | |"
		echo "|---|---|"
		echo "| Core | \`$core_id\` |"
		echo "| Version | \`$version\` |"
		[ -z "$made" ] || echo "| Made | $made (Chimera lists it as \`$made  (${version:0:8})\`) |"
		echo "| Built from | \`$sha\` |"
		for a in "${assets[@]}"; do
			arch="${a%.chimeraCore}"; arch="${arch##*-}"
			echo "| $arch | \`$a\` |"
		done
		echo
		if [ "$kind" = dev ]; then
			echo "This is a **development build**: it is replaced on every change, so it"
			echo "may stop being downloadable. For a build that is kept permanently -"
			echo "which is what a movie needs to stay replayable - take a nightly."
		else
			echo "This is a **nightly**: it is never deleted. A movie recorded on this"
			echo "package can be replayed against it for as long as this repository"
			echo "exists."
		fi
		if [ -n "$changes" ]; then
			echo
			printf '%s\n' "$changes"
		fi
	} > "$notes"
fi

echo "publishing ${assets[*]} ($kind) from $sha"
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
# A dated nightly is a full release and the one GitHub shows as Latest; the
# rolling dev build is a pre-release and never Latest (user-decided,
# 2026-09-25). The index release is made elsewhere and stays a pre-release.
release_kind_flags() {
	case "$1" in
		nightly-*) echo "--latest" ;;
		*) echo "--prerelease --latest=false" ;;
	esac
}

ensure_release() { # <tag> <title>
	local n
	for n in 1 2 3 4; do
		gh release view "$1" >/dev/null 2>&1 && return 0
		# shellcheck disable=SC2046
		gh release create "$1" --target "$sha" $(release_kind_flags "$1") --title "$2" --notes-file "$notes" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: release $1" >&2; sleep $((n * 15)); }
	done
	return 1
}

if [ "$kind" = dev ]; then
	# ONE TAG, MOVED - through the releases API, never `git push`. A workflow's
	# token may not create or update workflow FILES, and pushing a tag at a
	# commit whose .github/workflows differ from the default branch's counts as
	# exactly that. Deleting the release with its tag and recreating it goes
	# through the API, which has no such restriction.
	#
	# The delete is retried and then checked: a failed one left the old release
	# standing, ensure_release found it and the new package went in beside the
	# old one, under the old title, notes and commit. "Not found" is the only
	# answer that means gone; any other failure to look is asked again.
	dev_gone=0
	for n in 1 2 3 4; do
		gh release delete dev --yes --cleanup-tag >/dev/null 2>&1 || true
		if look=$(gh release view dev 2>&1 >/dev/null); then
			:
		else
			case "$look" in *"not found"*) dev_gone=1; break ;; esac
		fi
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: delete the old dev release" >&2; sleep $((n * 15)); }
	done
	if [ "$dev_gone" -ne 1 ]; then
		echo "publish-core: the old dev release could not be deleted; the new package is not put into it" >&2
		exit 1
	fi
	ensure_release dev "$title"
	for a in "${assets[@]}"; do
		retry gh release upload dev "$staging/$a" --clobber
		echo "published $a"
	done
else
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
fi
