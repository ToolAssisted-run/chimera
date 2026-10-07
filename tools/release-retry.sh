# Sourced by the release workflow. GitHub's API answers 5xx now and then, and a
# publish is two calls, so a bad answer between them used to leave a release
# with nothing in it (2026-09-13). tools/publish-core.sh carries the same two
# helpers for the cores, which check this repository out to publish.

# retry <command...>: up to four attempts, 15/30/45s apart
retry() {
	local n
	for n in 1 2 3 4; do
		"$@" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: $1 $2 $3" >&2; sleep $((n * 15)); }
	done
	return 1
}

# release_kind_flags <tag>: a dated nightly is a full release and the one
# GitHub shows as Latest; anything else (the rolling dev build) is a
# pre-release and never Latest (user-decided, 2026-09-25)
release_kind_flags() {
	case "$1" in
		nightly-*) echo "--latest" ;;
		*) echo "--prerelease --latest=false" ;;
	esac
}

# ensure_release <tag> <sha> <title> <notes file>: a create whose answer was
# lost may still have happened, so look before trying again
ensure_release() {
	local n
	for n in 1 2 3 4; do
		gh release view "$1" >/dev/null 2>&1 && return 0
		# shellcheck disable=SC2046
		gh release create "$1" --target "$2" $(release_kind_flags "$1") --title "$3" --notes-file "$4" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: release $1" >&2; sleep $((n * 15)); }
	done
	return 1
}
