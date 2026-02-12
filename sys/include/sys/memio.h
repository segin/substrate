#ifndef _SYS_MEMIO_H
#define _SYS_MEMIO_H

#include <sys/ioctl.h>

/* Minimal IOC macros if not defined */
#ifndef _IOW
#define _IOC_NRBITS 8
#define _IOC_TYPEBITS 8
#define _IOC_SIZEBITS 14
#define _IOC_DIRBITS 2

#define _IOC_NRSHIFT 0
#define _IOC_TYPESHIFT (_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT (_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT (_IOC_SIZESHIFT+_IOC_SIZEBITS)

#define _IOC_NONE 0U
#define _IOC_WRITE 1U
#define _IOC_READ 2U

#define _IOC(dir,type,nr,size) \
    (((dir)  << _IOC_DIRSHIFT) | \
     ((type) << _IOC_TYPESHIFT) | \
     ((nr)   << _IOC_NRSHIFT) | \
     ((size) << _IOC_SIZESHIFT))

#define _IOR(type,nr,size) _IOC(_IOC_READ,(type),(nr),sizeof(size))
#define _IOW(type,nr,size) _IOC(_IOC_WRITE,(type),(nr),sizeof(size))
#endif

#define MEM_IOC_MAGIC 'M'

/* Enable/Disable access */
#define MEM_SET_ALLOW        _IOW(MEM_IOC_MAGIC, 1, int)
#define MEM_GET_ALLOW        _IOR(MEM_IOC_MAGIC, 2, int)

/* Set secure level (can only increase) */
#define MEM_SET_SECURELEVEL  _IOW(MEM_IOC_MAGIC, 3, int)
#define MEM_GET_SECURELEVEL  _IOR(MEM_IOC_MAGIC, 4, int)

#endif
