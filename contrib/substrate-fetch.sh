# substrate-fetch.sh - download a port's tarball, with mirrors.
#
# Sourced by a port's fetch.sh:
#
#     . "${HERE}/../substrate-fetch.sh"
#     substrate_fetch "${URL}" "${TARBALL}"
#
# substrate_fetch URL OUTPUT downloads URL to OUTPUT and returns non-zero
# if it could not.  A URL on ftp.gnu.org is also tried at mirrors that
# carry the same tree, so that a port does not depend on the one host:
# ftp.gnu.org and its own redirector, ftpmirror.gnu.org, have both been
# unreachable for hours at a time, which stops a whole-system build at its
# first download.  Which copy is fetched does not matter to what is built:
# every fetch.sh checks the tarball's SHA-256 afterwards.
#
# A connection that is not made within SUBSTRATE_FETCH_CONNECT_TIMEOUT
# seconds (default 20) is given up on, so that a dead host costs seconds
# and not the couple of minutes curl waits by default.

# The URLs to try for URL, one per line, the given one first.
substrate_fetch_urls() {
    echo "$1"
    case $1 in
        https://ftp.gnu.org/gnu/*|http://ftp.gnu.org/gnu/*)
            _sf_path=${1#*://ftp.gnu.org/gnu/}
            echo "https://mirrors.kernel.org/gnu/${_sf_path}"
            echo "https://mirrors.ocf.berkeley.edu/gnu/${_sf_path}"
            echo "https://ftpmirror.gnu.org/gnu/${_sf_path}"
            ;;
    esac
}

substrate_fetch() {
    _sf_out=$2
    _sf_timeout=${SUBSTRATE_FETCH_CONNECT_TIMEOUT:-20}
    for _sf_url in $(substrate_fetch_urls "$1"); do
        if command -v curl >/dev/null 2>&1; then
            curl -fSL --connect-timeout "${_sf_timeout}" --retry 2 \
                 --retry-delay 3 -o "${_sf_out}" "${_sf_url}" && return 0
        elif command -v wget >/dev/null 2>&1; then
            wget --connect-timeout="${_sf_timeout}" --tries=2 \
                 -O "${_sf_out}" "${_sf_url}" && return 0
        else
            echo "substrate-fetch: neither curl nor wget found" >&2
            return 1
        fi
        echo "substrate-fetch: ${_sf_url} failed" >&2
        rm -f "${_sf_out}"
    done
    echo "substrate-fetch: could not download $1" >&2
    return 1
}
