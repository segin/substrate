/* Native-dependent code for Substrate/i386 and Substrate/x86-64.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Inspects the inferior through Substrate's ptrace(2): the general-purpose
   registers via PTRACE_GETREGS/SETREGS into a struct user_regs_struct
   (sys/ptrace.h), while memory access and run control (CONT, SINGLESTEP, KILL,
   wait) come from the generic inf-ptrace target.  Floating-point / SSE state is
   not transferred yet and is reported unavailable.

   A 64-bit gdb gets the Linux/amd64 user_regs_struct from the kernel and
   8-byte PEEK/POKE words (its sizeof(long)), whatever the inferior's width.
   A 64-bit inferior uses the amd64 register numbers; a 32-bit one, debugged
   from the same gdb, takes the low halves of the same fields.  */

#include "inferior.h"
#include "regcache.h"
#include "target.h"
#include "gdbarch.h"

#include <sys/types.h>
#include <sys/ptrace.h>
#include <limits.h>
#include <unistd.h>

#include "i386-tdep.h"
#ifdef __x86_64__
#include "amd64-tdep.h"
#endif
#include "inf-ptrace.h"

/* Offset in `struct user_regs_struct' of a member.  */
#define REG_OFFSET(member) offsetof (struct user_regs_struct, member)

#ifdef __x86_64__

/* amd64_r_reg_offset[REGNUM] is the byte offset of GDB amd64 register
   REGNUM (AMD64_RAX_REGNUM .. AMD64_GS_REGNUM, 0..23).  */
static int amd64_r_reg_offset[] =
{
  REG_OFFSET (rax),			/* 0  AMD64_RAX_REGNUM */
  REG_OFFSET (rbx),			/* 1  AMD64_RBX_REGNUM */
  REG_OFFSET (rcx),			/* 2  AMD64_RCX_REGNUM */
  REG_OFFSET (rdx),			/* 3  AMD64_RDX_REGNUM */
  REG_OFFSET (rsi),			/* 4  AMD64_RSI_REGNUM */
  REG_OFFSET (rdi),			/* 5  AMD64_RDI_REGNUM */
  REG_OFFSET (rbp),			/* 6  AMD64_RBP_REGNUM */
  REG_OFFSET (rsp),			/* 7  AMD64_RSP_REGNUM */
  REG_OFFSET (r8),			/* 8  AMD64_R8_REGNUM */
  REG_OFFSET (r9),			/* 9  AMD64_R9_REGNUM */
  REG_OFFSET (r10),			/* 10 AMD64_R10_REGNUM */
  REG_OFFSET (r11),			/* 11 AMD64_R11_REGNUM */
  REG_OFFSET (r12),			/* 12 AMD64_R12_REGNUM */
  REG_OFFSET (r13),			/* 13 AMD64_R13_REGNUM */
  REG_OFFSET (r14),			/* 14 AMD64_R14_REGNUM */
  REG_OFFSET (r15),			/* 15 AMD64_R15_REGNUM */
  REG_OFFSET (rip),			/* 16 AMD64_RIP_REGNUM */
  REG_OFFSET (eflags),			/* 17 AMD64_EFLAGS_REGNUM */
  REG_OFFSET (cs),			/* 18 AMD64_CS_REGNUM */
  REG_OFFSET (ss),			/* 19 AMD64_SS_REGNUM */
  REG_OFFSET (ds),			/* 20 AMD64_DS_REGNUM */
  REG_OFFSET (es),			/* 21 AMD64_ES_REGNUM */
  REG_OFFSET (fs),			/* 22 AMD64_FS_REGNUM */
  REG_OFFSET (gs),			/* 23 AMD64_GS_REGNUM */
};

/* A 32-bit inferior under the 64-bit gdb: I386_EAX_REGNUM .. I386_GS_REGNUM
   (0..15) map onto the low four bytes of the 64-bit fields.  */
static int i386_r_reg_offset[] =
{
  REG_OFFSET (rax),			/* 0  I386_EAX_REGNUM */
  REG_OFFSET (rcx),			/* 1  I386_ECX_REGNUM */
  REG_OFFSET (rdx),			/* 2  I386_EDX_REGNUM */
  REG_OFFSET (rbx),			/* 3  I386_EBX_REGNUM */
  REG_OFFSET (rsp),			/* 4  I386_ESP_REGNUM */
  REG_OFFSET (rbp),			/* 5  I386_EBP_REGNUM */
  REG_OFFSET (rsi),			/* 6  I386_ESI_REGNUM */
  REG_OFFSET (rdi),			/* 7  I386_EDI_REGNUM */
  REG_OFFSET (rip),			/* 8  I386_EIP_REGNUM */
  REG_OFFSET (eflags),			/* 9  I386_EFLAGS_REGNUM */
  REG_OFFSET (cs),			/* 10 I386_CS_REGNUM */
  REG_OFFSET (ss),			/* 11 I386_SS_REGNUM */
  REG_OFFSET (ds),			/* 12 I386_DS_REGNUM */
  REG_OFFSET (es),			/* 13 I386_ES_REGNUM */
  REG_OFFSET (fs),			/* 14 I386_FS_REGNUM */
  REG_OFFSET (gs),			/* 15 I386_GS_REGNUM */
};

#else /* !__x86_64__ */

/* i386_r_reg_offset[REGNUM] is the byte offset of GDB i386 register REGNUM
   (I386_EAX_REGNUM .. I386_GS_REGNUM, 0..15) inside user_regs_struct.  */
static int i386_r_reg_offset[] =
{
  REG_OFFSET (eax),			/* 0  I386_EAX_REGNUM */
  REG_OFFSET (ecx),			/* 1  I386_ECX_REGNUM */
  REG_OFFSET (edx),			/* 2  I386_EDX_REGNUM */
  REG_OFFSET (ebx),			/* 3  I386_EBX_REGNUM */
  REG_OFFSET (esp),			/* 4  I386_ESP_REGNUM */
  REG_OFFSET (ebp),			/* 5  I386_EBP_REGNUM */
  REG_OFFSET (esi),			/* 6  I386_ESI_REGNUM */
  REG_OFFSET (edi),			/* 7  I386_EDI_REGNUM */
  REG_OFFSET (eip),			/* 8  I386_EIP_REGNUM */
  REG_OFFSET (eflags),			/* 9  I386_EFLAGS_REGNUM */
  REG_OFFSET (xcs),			/* 10 I386_CS_REGNUM */
  REG_OFFSET (xss),			/* 11 I386_SS_REGNUM */
  REG_OFFSET (xds),			/* 12 I386_DS_REGNUM */
  REG_OFFSET (xes),			/* 13 I386_ES_REGNUM */
  REG_OFFSET (xfs),			/* 14 I386_FS_REGNUM */
  REG_OFFSET (xgs),			/* 15 I386_GS_REGNUM */
};

#endif /* __x86_64__ */

/* The offset table for REGCACHE's architecture, and its length.  */

static const int *
substrate_reg_offsets (const struct regcache *regcache, int *count)
{
#ifdef __x86_64__
  if (gdbarch_ptr_bit (regcache->arch ()) == 64)
    {
      *count = ARRAY_SIZE (amd64_r_reg_offset);
      return amd64_r_reg_offset;
    }
#endif
  (void) regcache;
  *count = ARRAY_SIZE (i386_r_reg_offset);
  return i386_r_reg_offset;
}

#ifdef __x86_64__
/* The fs_base register's number in REGCACHE's architecture, or -1 when its
   target description has none (no segments feature).  */

static int
substrate_fs_base_regnum (const struct regcache *regcache)
{
  struct gdbarch *gdbarch = regcache->arch ();

  if (gdbarch_ptr_bit (gdbarch) != 64
      || AMD64_FSBASE_REGNUM >= gdbarch_num_regs (gdbarch))
    return -1;
  const char *name = gdbarch_register_name (gdbarch, AMD64_FSBASE_REGNUM);
  return (name != nullptr && strcmp (name, "fs_base") == 0)
	 ? AMD64_FSBASE_REGNUM : -1;
}
#endif

/* Does PTRACE_GETREGS carry GDB register REGNUM (or all, if -1)?  */

static bool
getregs_supplies (const struct regcache *regcache, int regnum)
{
  int count;

  substrate_reg_offsets (regcache, &count);
  if (regnum == -1 || (regnum >= 0 && regnum < count))
    return true;
#ifdef __x86_64__
  if (regnum == substrate_fs_base_regnum (regcache))
    return true;
#endif
  return false;
}

class substrate_nat_target final : public inf_ptrace_target
{
public:
  void fetch_registers (struct regcache *, int) override;
  void store_registers (struct regcache *, int) override;

  /* Substrate needs no post-exec ptrace setup (no PTRACE_SETOPTIONS); the
     traced child is already stopped at its first signal-delivery stop. */
  void post_startup_inferior (ptid_t ptid) override { (void) ptid; }

  /* The program an attached process is running, so `attach PID' (gdb -p)
     loads its symbols and architecture without a `file' command.  */
  const char *pid_to_exec_file (int pid) override;
};

const char *
substrate_nat_target::pid_to_exec_file (int pid)
{
  static char buf[PATH_MAX];
  char name[64];

  xsnprintf (name, sizeof (name), "/proc/%d/exe", pid);
  ssize_t len = readlink (name, buf, sizeof (buf) - 1);
  if (len <= 0)
    return nullptr;
  buf[len] = '\0';
  return buf;
}

static substrate_nat_target the_substrate_nat_target;

/* Supply the GPRs in GREGS to REGCACHE.  */

static void
substrate_supply_gregset (struct regcache *regcache, const void *gregs)
{
  const char *regs = (const char *) gregs;
  int count;
  const int *offsets = substrate_reg_offsets (regcache, &count);

  for (int regnum = 0; regnum < count; regnum++)
    regcache->raw_supply (regnum, regs + offsets[regnum]);
#ifdef __x86_64__
  int fs_base = substrate_fs_base_regnum (regcache);
  if (fs_base >= 0)
    regcache->raw_supply (fs_base, regs + REG_OFFSET (fs_base));
#endif
}

/* Collect register REGNUM (or all if -1) from REGCACHE into GREGS.  */

static void
substrate_collect_gregset (const struct regcache *regcache,
			   void *gregs, int regnum)
{
  char *regs = (char *) gregs;
  int count;
  const int *offsets = substrate_reg_offsets (regcache, &count);

  for (int i = 0; i < count; i++)
    if (regnum == -1 || regnum == i)
      regcache->raw_collect (i, regs + offsets[i]);
#ifdef __x86_64__
  int fs_base = substrate_fs_base_regnum (regcache);
  if (fs_base >= 0 && (regnum == -1 || regnum == fs_base))
    regcache->raw_collect (fs_base, regs + REG_OFFSET (fs_base));
#endif
}

void
substrate_nat_target::fetch_registers (struct regcache *regcache, int regnum)
{
  pid_t pid = get_ptrace_pid (regcache->ptid ());

  if (getregs_supplies (regcache, regnum))
    {
      struct user_regs_struct regs;

      if (ptrace (PTRACE_GETREGS, pid, nullptr, &regs) == -1)
	perror_with_name (_("Couldn't get registers"));

      substrate_supply_gregset (regcache, &regs);
    }
}

void
substrate_nat_target::store_registers (struct regcache *regcache, int regnum)
{
  pid_t pid = get_ptrace_pid (regcache->ptid ());

  if (getregs_supplies (regcache, regnum))
    {
      struct user_regs_struct regs;

      if (ptrace (PTRACE_GETREGS, pid, nullptr, &regs) == -1)
	perror_with_name (_("Couldn't get registers"));

      substrate_collect_gregset (regcache, &regs, regnum);

      if (ptrace (PTRACE_SETREGS, pid, nullptr, &regs) == -1)
	perror_with_name (_("Couldn't write registers"));
    }
}

void _initialize_substrate_nat ();
void
_initialize_substrate_nat ()
{
  add_inf_child_target (&the_substrate_nat_target);
}
