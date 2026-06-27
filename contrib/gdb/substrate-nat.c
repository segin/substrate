/* Native-dependent code for Substrate/i386.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   Inspects the inferior through Substrate's ptrace(2): the general-purpose
   registers via PTRACE_GETREGS/SETREGS into a struct user_regs_struct
   (sys/ptrace.h), while memory access and run control (CONT, SINGLESTEP, KILL,
   wait) come from the generic inf-ptrace target.  Floating-point / SSE state is
   not transferred yet and is reported unavailable.  */

#include "inferior.h"
#include "regcache.h"
#include "target.h"

#include <sys/types.h>
#include <sys/ptrace.h>

#include "i386-tdep.h"
#include "inf-ptrace.h"

/* Offset in `struct user_regs_struct' where GDB i386 register REGNUM lives.  */
#define REG_OFFSET(member) offsetof (struct user_regs_struct, member)

/* substrate_r_reg_offset[REGNUM] is the byte offset of GDB register REGNUM
   (I386_EAX_REGNUM .. I386_GS_REGNUM, 0..15) inside user_regs_struct.  */
static int substrate_r_reg_offset[] =
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

/* GDB register REGNUM is fetched with PTRACE_GETREGS.  */
#define GETREGS_SUPPLIES(regnum) \
  ((0 <= (regnum)) && ((regnum) <= 15))

class substrate_nat_target final : public inf_ptrace_target
{
public:
  void fetch_registers (struct regcache *, int) override;
  void store_registers (struct regcache *, int) override;

  /* Substrate needs no post-exec ptrace setup (no PTRACE_SETOPTIONS); the
     traced child is already stopped at its first signal-delivery stop. */
  void post_startup_inferior (ptid_t ptid) override { (void) ptid; }
};

static substrate_nat_target the_substrate_nat_target;

/* Supply the GPRs in GREGS to REGCACHE.  */

static void
substrate_supply_gregset (struct regcache *regcache, const void *gregs)
{
  const char *regs = (const char *) gregs;

  for (int regnum = 0;
       regnum < (int) ARRAY_SIZE (substrate_r_reg_offset);
       regnum++)
    regcache->raw_supply (regnum, regs + substrate_r_reg_offset[regnum]);
}

/* Collect register REGNUM (or all if -1) from REGCACHE into GREGS.  */

static void
substrate_collect_gregset (const struct regcache *regcache,
			   void *gregs, int regnum)
{
  char *regs = (char *) gregs;

  for (int i = 0; i < (int) ARRAY_SIZE (substrate_r_reg_offset); i++)
    if (regnum == -1 || regnum == i)
      regcache->raw_collect (i, regs + substrate_r_reg_offset[i]);
}

void
substrate_nat_target::fetch_registers (struct regcache *regcache, int regnum)
{
  pid_t pid = get_ptrace_pid (regcache->ptid ());

  if (regnum == -1 || GETREGS_SUPPLIES (regnum))
    {
      struct user_regs_struct regs;

      if (ptrace (PTRACE_GETREGS, pid, nullptr, &regs) == -1)
	perror_with_name (_("Couldn't get registers"));

      substrate_supply_gregset (regcache, &regs);
      if (regnum != -1)
	return;
    }
}

void
substrate_nat_target::store_registers (struct regcache *regcache, int regnum)
{
  pid_t pid = get_ptrace_pid (regcache->ptid ());

  if (regnum == -1 || GETREGS_SUPPLIES (regnum))
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
