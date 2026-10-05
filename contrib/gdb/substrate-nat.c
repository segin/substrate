/* Native-dependent code for Substrate/i386 and Substrate/x86-64.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Inspects the inferior through Substrate's ptrace(2): the general-purpose
   registers via PTRACE_GETREGS/SETREGS into a struct user_regs_struct
   (sys/ptrace.h), while memory access and run control (CONT, SINGLESTEP, KILL,
   wait) come from the generic inf-ptrace target.  The floating-point and
   vector registers come in the CPU's own save layout -- the XSAVE area
   (PTRACE_GETXSTATE), the FXSAVE image (PTRACE_GETFPXREGS) or the FNSAVE
   image (PTRACE_GETFPREGS), whichever the kernel uses -- and the target
   description offers x87, SSE or AVX registers to match.

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
#include "i387-tdep.h"
#ifdef __x86_64__
#include "amd64-tdep.h"
#endif
#include "inf-ptrace.h"
#include "gdbsupport/x86-xstate.h"
#include "gdbsupport/byte-vector.h"

/* %cs of a 64-bit process: the kernel's SEL_UCODE at ring 3.  A 32-bit
   process under the 64-bit kernel has the compatibility-mode selector.  */
#define SUBSTRATE_UCODE64_SEL 0x2b

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

/* How the kernel hands over the floating-point and vector registers.  It
   is the CPU's own save format, so it is found once: the XSAVE area (x87,
   SSE, AVX, ...) where the CPU has XSAVE, else the FXSAVE image (x87 and
   SSE), else -- a 486 -- the FNSAVE image.  */

enum substrate_fp_kind
{
  FP_UNPROBED,
  FP_NONE,
  FP_FSAVE,
  FP_FXSAVE,
  FP_XSAVE,
};

static enum substrate_fp_kind substrate_fp = FP_UNPROBED;
static uint64_t substrate_xcr0;		/* FP_XSAVE: the components saved */
static size_t substrate_xsave_len;	/* FP_XSAVE: the size of the area */

static void
substrate_probe_fp (pid_t pid)
{
  struct ptrace_xstate_info info;
  gdb_byte buf[PTRACE_FPXREGS_SIZE];

  if (substrate_fp != FP_UNPROBED)
    return;

  if (ptrace (PTRACE_GETXSTATE_INFO, pid, nullptr, &info) == 0
      && info.xsave_len >= PTRACE_FPXREGS_SIZE)
    {
      substrate_fp = FP_XSAVE;
      substrate_xcr0 = info.xsave_mask;
      substrate_xsave_len = info.xsave_len;
    }
  else if (ptrace (PTRACE_GETFPXREGS, pid, nullptr, buf) == 0)
    substrate_fp = FP_FXSAVE;
  else if (ptrace (PTRACE_GETFPREGS, pid, nullptr, buf) == 0)
    substrate_fp = FP_FSAVE;
  else
    substrate_fp = FP_NONE;
}

/* The XCR0 value describing the registers the kernel hands over.  */

static uint64_t
substrate_fp_xcr0 ()
{
  switch (substrate_fp)
    {
    case FP_XSAVE:
      return substrate_xcr0 & X86_XSTATE_ALL_MASK;
    case FP_FXSAVE:
      return X86_XSTATE_SSE_MASK;
    default:
      return X86_XSTATE_X87_MASK;
    }
}

class substrate_nat_target final : public inf_ptrace_target
{
public:
  void fetch_registers (struct regcache *, int) override;
  void store_registers (struct regcache *, int) override;

  /* The registers the inferior has: 64-bit or 32-bit, and x87, SSE or AVX
     according to what the kernel saves.  */
  const struct target_desc *read_description () override;

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

const struct target_desc *
substrate_nat_target::read_description ()
{
  if (inferior_ptid == null_ptid)
    return this->beneath ()->read_description ();

  pid_t pid = get_ptrace_pid (inferior_ptid);

  substrate_probe_fp (pid);
#ifdef __x86_64__
  struct user_regs_struct regs;

  if (ptrace (PTRACE_GETREGS, pid, nullptr, &regs) == -1)
    perror_with_name (_("Couldn't get registers"));
  /* The kernel's 64-bit user code selector; a 32-bit process runs on its
     compatibility-mode one.  */
  if (regs.cs == SUBSTRATE_UCODE64_SEL)
    return amd64_target_description (substrate_fp_xcr0 (), true);
#endif
  return i386_target_description (substrate_fp_xcr0 (), false);
}

static substrate_nat_target the_substrate_nat_target;

/* Is REGCACHE's architecture a 64-bit one?  */

static bool
substrate_regcache_is_64bit (const struct regcache *regcache)
{
#ifdef __x86_64__
  return gdbarch_ptr_bit (regcache->arch ()) == 64;
#else
  (void) regcache;
  return false;
#endif
}

/* Fetch the floating-point and vector registers: REGNUM, or all of them
   if it is -1.  */

static void
substrate_fetch_fpregs (struct regcache *regcache, pid_t pid, int regnum)
{
  bool is64 = substrate_regcache_is_64bit (regcache);

  substrate_probe_fp (pid);
  switch (substrate_fp)
    {
    case FP_XSAVE:
      {
	gdb::byte_vector buf (substrate_xsave_len);

	if (ptrace (PTRACE_GETXSTATE, pid, (void *) substrate_xsave_len,
		    buf.data ()) == -1)
	  perror_with_name (_("Couldn't get extended state"));
#ifdef __x86_64__
	if (is64)
	  amd64_supply_xsave (regcache, regnum, buf.data ());
	else
#endif
	  i387_supply_xsave (regcache, regnum, buf.data ());
	break;
      }
    case FP_FXSAVE:
      {
	gdb_byte buf[PTRACE_FPXREGS_SIZE];

	if (ptrace (PTRACE_GETFPXREGS, pid, nullptr, buf) == -1)
	  perror_with_name (_("Couldn't get floating point status"));
#ifdef __x86_64__
	if (is64)
	  amd64_supply_fxsave (regcache, regnum, buf);
	else
#endif
	  i387_supply_fxsave (regcache, regnum, buf);
	break;
      }
    case FP_FSAVE:
      {
	gdb_byte buf[PTRACE_FPREGS_SIZE];

	if (ptrace (PTRACE_GETFPREGS, pid, nullptr, buf) == -1)
	  perror_with_name (_("Couldn't get floating point status"));
	i387_supply_fsave (regcache, regnum, buf);
	break;
      }
    default:
      break;
    }
  (void) is64;
}

/* Store them: read the kernel's copy, replace REGNUM (or everything) in
   it, write it back.  */

static void
substrate_store_fpregs (struct regcache *regcache, pid_t pid, int regnum)
{
  bool is64 = substrate_regcache_is_64bit (regcache);

  substrate_probe_fp (pid);
  switch (substrate_fp)
    {
    case FP_XSAVE:
      {
	gdb::byte_vector buf (substrate_xsave_len);

	if (ptrace (PTRACE_GETXSTATE, pid, (void *) substrate_xsave_len,
		    buf.data ()) == -1)
	  perror_with_name (_("Couldn't get extended state"));
#ifdef __x86_64__
	if (is64)
	  amd64_collect_xsave (regcache, regnum, buf.data (), 0);
	else
#endif
	  i387_collect_xsave (regcache, regnum, buf.data (), 0);
	if (ptrace (PTRACE_SETXSTATE, pid, (void *) substrate_xsave_len,
		    buf.data ()) == -1)
	  perror_with_name (_("Couldn't write extended state"));
	break;
      }
    case FP_FXSAVE:
      {
	gdb_byte buf[PTRACE_FPXREGS_SIZE];

	if (ptrace (PTRACE_GETFPXREGS, pid, nullptr, buf) == -1)
	  perror_with_name (_("Couldn't get floating point status"));
#ifdef __x86_64__
	if (is64)
	  amd64_collect_fxsave (regcache, regnum, buf);
	else
#endif
	  i387_collect_fxsave (regcache, regnum, buf);
	if (ptrace (PTRACE_SETFPXREGS, pid, nullptr, buf) == -1)
	  perror_with_name (_("Couldn't write floating point status"));
	break;
      }
    case FP_FSAVE:
      {
	gdb_byte buf[PTRACE_FPREGS_SIZE];

	if (ptrace (PTRACE_GETFPREGS, pid, nullptr, buf) == -1)
	  perror_with_name (_("Couldn't get floating point status"));
	i387_collect_fsave (regcache, regnum, buf);
	if (ptrace (PTRACE_SETFPREGS, pid, nullptr, buf) == -1)
	  perror_with_name (_("Couldn't write floating point status"));
	break;
      }
    default:
      break;
    }
  (void) is64;
}

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

  if (regnum == -1 || !getregs_supplies (regcache, regnum))
    substrate_fetch_fpregs (regcache, pid, regnum);
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

  if (regnum == -1 || !getregs_supplies (regcache, regnum))
    substrate_store_fpregs (regcache, pid, regnum);
}

void _initialize_substrate_nat ();
void
_initialize_substrate_nat ()
{
  add_inf_child_target (&the_substrate_nat_target);
}
