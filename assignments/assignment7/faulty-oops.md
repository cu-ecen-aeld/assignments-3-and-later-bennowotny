# A7 P2: Kernel Fault Analysis

## Kernel Fault

The following command was run from inside the A5 repository QEMU instance with the `faulty`, `hello`, and `scull` kernel modules loaded.

```console
# echo "hello_world" > /dev/faulty
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b51000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 113 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008db3d20
x29: ffffffc008db3d80 x28: ffffff8001b35cc0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008db3dc0
x20: 000000557b7e9b50 x19: ffffff8001bac000 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000787000 x3 : ffffffc008db3dc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 el0_svc_common.constprop.0+0x44/0xf0
 do_el0_svc+0x2c/0xc0
 el0_svc+0x2c/0x90
 el0t_64_sync_handler+0xf4/0x120
 el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f) 
---[ end trace 0000000000000000 ]---
```

## Analysis

There are several important parts of the kernel fault that can help us debug the issue encountered.

The first line of the fault looks like so:

```console
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
```

This tells us that the kernel fault was due to a dereference of address `0x0`, likely the `NULL` pointer.

Next, we see the some information about the execution of the kernel at the time of the failure.  The next part we might recognize is:

```console
Modules linked in: hello(O) faulty(O) scull(O)
CPU: 0 PID: 113 Comm: sh Tainted: G           O       6.1.44 #1
```

This tells us that the kernel was tainted by the `hello`, `faulty`, and `scull` modules.  Since these modules weren't part of the original kernel, these are likely suspect as the cause of the kernel fault.

We also see a register dump and a stack trace (since debug symbols are present):

```console
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
...
```

This indicates that the instruction being run when the fault happened was in a function called `faulty_write`.  This is backed up by the call stack:

```console
Call trace:
 faulty_write+0x10/0x20 [faulty]
 ksys_write+0x74/0x110
 __arm64_sys_write+0x1c/0x30
 invoke_syscall+0x54/0x130
 ...
```

We might want to investigate the addresses mentioned in the kernel modules to see if we can find offending instructions.  Using a cross-`objdump` on the `faulty.ko` module shows us the following:

```console
./buildroot/output/build/ldd-d980877eb69c1ad6d90ce187f17916ef3585ede1/misc-modules/faulty.ko:     file format elf64-littleaarch64


Disassembly of section .text:

0000000000000000 <faulty_write>:
   0:   d2800001        mov     x1, #0x0                        // #0
   4:   d2800000        mov     x0, #0x0                        // #0
   8:   d503233f        paciasp
   c:   d50323bf        autiasp
  10:   b900003f        str     wzr, [x1]
  14:   d65f03c0        ret
  18:   d503201f        nop
  1c:   d503201f        nop
```

Here, we see that we explicitly load `0` on instruction `<faulty_write>+0x0` and try to write to it as an address on instruction `<faulty_write>+0x10`.  This seems very much like an intentional write to the `NULL` address.

At this point, it would be a good idea to search through the kernel source files, in particular the extra modules loaded, and look for a function called `faulty_write` to see if there is a candidate for the bad access.  Sure enough, in the `faulty.c` source file for the `faulty` kernel module:

```c
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count, loff_t *pos)
{
  /* make a simple fault by dereferencing a NULL pointer */
  *(int *)0 = 0;
  return 0;
}
```

And the bad access of the `NULL` address is immediately apparent.
