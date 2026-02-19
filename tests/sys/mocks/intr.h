/* Dummy intr.h for mocking kernel interrupt headers in host tests */
#ifndef _ARCH_I386_INTR_H
#define _ARCH_I386_INTR_H
/*
 * This file is intentionally empty or minimal to allow host-side tests
 * to compile code that includes <intr.h>.
 * The actual implementation should be mocked in the test file or here.
 */
#endif
