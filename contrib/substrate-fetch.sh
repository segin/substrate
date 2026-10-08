# substrate-fetch.sh - download a port's tarball, with mirrors.
#
# Sourced by a port's fetch.sh:
#
#     . "${HERE}/../substrate-fetch.sh"
#     substrate_fetch "${URL}" "${TARBALL}" "${SHA256}"
#
# substrate_fetch URL OUTPUT [SHA256] downloads URL to OUTPUT and returns
# non-zero if it could not.  A URL on one of the hosts below is also tried
# at other places that carry the same file, so that a port does not depend
# on the one host, which stops a whole-system build at its first download:
#
#   ftp.gnu.org         It and its own redirector, ftpmirror.gnu.org, have
#                       both been unreachable for hours at a time.
#   www.x.org           X.Org's releases and archive.
#   *.freedesktop.org,  fontconfig, dbus, pkg-config, xcb, cairo, pixman.
#   www.cairographics.org
#                       www.freedesktop.org has answered a build machine
#                       with "418" for every request.  These have no mirror
#                       of their tree, so the file is looked for by name in
#                       the source collections of other systems.
#
# Which copy is fetched does not matter to what is built: every fetch.sh
# checks the tarball's checksum afterwards.  Given the SHA256 here as well,
# a copy that is not the file -- a mirror's error page, a tarball rolled
# again under the same name -- is passed over for the next place, and does
# not end the build at that check.
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
        https://www.x.org/releases/individual/*|https://www.x.org/archive/individual/*)
            _sf_path=${1#*/individual/}
            echo "https://xorg.freedesktop.org/archive/individual/${_sf_path}"
            echo "https://artfiles.org/x.org/pub/individual/${_sf_path}"
            ;;
        https://*.freedesktop.org/*|https://www.cairographics.org/releases/*)
            _sf_file=${1##*/}
            case $1 in
                https://xcb.freedesktop.org/*)
                    echo "https://www.x.org/releases/individual/xcb/${_sf_file}"
                    echo "https://www.x.org/releases/individual/proto/${_sf_file}"
                    ;;
                https://www.cairographics.org/*)
                    # pixman is released at X.Org as well.
                    echo "https://www.x.org/releases/individual/lib/${_sf_file}"
                    ;;
            esac
            echo "https://ftp.osuosl.org/pub/blfs/conglomeration/${_sf_file%%-[0-9]*}/${_sf_file}"
            echo "http://distcache.FreeBSD.org/ports-distfiles/${_sf_file}"
            echo "https://cdn.NetBSD.org/pub/pkgsrc/distfiles/${_sf_file}"
            ;;
    esac
}

# True if FILE is the file with that SHA256, or no SHA256 was given.
substrate_fetch_is() {
    [ -n "${2:-}" ] || return 0
    echo "$2  $1" | sha256sum -c - >/dev/null 2>&1
}

substrate_fetch() {
    _sf_out=$2
    _sf_sum=${3:-}
    _sf_timeout=${SUBSTRATE_FETCH_CONNECT_TIMEOUT:-20}
    for _sf_url in $(substrate_fetch_urls "$1"); do
        if command -v curl >/dev/null 2>&1; then
            curl -fSL --connect-timeout "${_sf_timeout}" --retry 2 \
                 --retry-delay 3 -o "${_sf_out}" "${_sf_url}" &&
                substrate_fetch_is "${_sf_out}" "${_sf_sum}" && return 0
        elif command -v wget >/dev/null 2>&1; then
            wget --connect-timeout="${_sf_timeout}" --tries=2 \
                 -O "${_sf_out}" "${_sf_url}" &&
                substrate_fetch_is "${_sf_out}" "${_sf_sum}" && return 0
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
