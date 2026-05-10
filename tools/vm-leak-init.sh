#!/bin/sh
# vm_leak torture test
#
# Replace dist/sbin/init with this file then run
#   ./build-rootfs.sh --image
# to bake it into rootfs.img.  Boot with `debug=vm_leak` on the kernel
# command line and watch every proc_exit emit a counter snapshot.
#
# Phase A: a few sanity execs against a populated /etc.
# Phase B: 100-iteration loop hitting:
#   - multi-component path lookups (ls /bin, /usr/bin, /lib, /etc, ...)
#   - file reads with stat (cat /etc/passwd, /etc/fstab, ...)
#   - both static (/bin/echo, /bin/true, /bin/false) and dynamic
#     (/usr/bin/ldd) execs
#   - directory descent (find /etc, find /lib)
#   - cwd_node mutations (cd /usr/bin && ls; cd /)
#
# The known reproduction symptom is that lstat starts returning ENOENT
# for files that exist after enough lookups, which surfaces as:
#   ls: cannot access '/bin/pwd': No such file or directory
# from the very first `ls /bin` call inside phase B.
echo "[t] === torture test begin ==="

/bin/ls /                 > /dev/null && echo "[t] A1 ok"
/bin/ls /etc              > /dev/null && echo "[t] A2 ok"
/bin/cat /etc/passwd      > /dev/null && echo "[t] A3 ok"

echo "[t] === phase B loop ==="
i=0
while [ $i -lt 100 ]; do
  /bin/ls /bin            > /dev/null
  /bin/ls /usr/bin        > /dev/null
  /bin/ls /lib            > /dev/null
  /bin/ls /etc            > /dev/null
  /bin/ls /usr/include    > /dev/null 2>/dev/null
  /bin/ls /usr/lib        > /dev/null 2>/dev/null

  /bin/cat /etc/passwd    > /dev/null
  /bin/cat /etc/fstab     > /dev/null
  /bin/cat /etc/group     > /dev/null
  /bin/cat /etc/profile   > /dev/null

  /bin/echo "iter $i"     > /dev/null
  /bin/true
  /bin/false
  /bin/pwd                > /dev/null

  /usr/bin/ldd /bin/echo  > /dev/null

  /bin/find /etc          > /dev/null
  /bin/find /lib          > /dev/null

  cd /usr/bin && /bin/ls  > /dev/null
  cd /                    > /dev/null

  i=$((i+1))
  if [ $((i % 10)) = 0 ]; then
    echo "[t] iter $i"
  fi
done

echo "[t] === torture test done ==="
