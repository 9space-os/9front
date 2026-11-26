# Patches for upstream 9front

Fixes made on the `gcc` branch (the gcc port of 9front) that are not about
gcc: they apply to 9front as it is, built with its own compilers.  Two
more (0026, 0027) come from the `main` branch's (then `ui`) HiDPI work and are useful
on 9front by themselves; some later ones come from `main`'s own work too
(the drivers, APE, the pager). Each one
is a patch for [9front](https://github.com/9front/9front) branch `front`,
in `git format-patch` form, one change per patch, without the port's
`/* GCC port: ... */` markers.

They stay on the branch they come from as well (marked there) until
9front has taken or declined them.

- Base: `origin/front` at `cb9e5e7e9` (2026-11-18, "ssh: fix buffer
  size off-by-one, add early size check to make sure padding fits").
- Source: branch `upstream` of this repository (worktree
  `../tree-upstream`, see `dist/docs/upstream.md`), one commit per patch on top of `origin/front`.
- The numbers stay: a patch keeps its number for good (they are quoted in
  the branches' comments and in reviews). One 9front has taken moves,
  unchanged, to `archive/` (see "Archive" below); the series then has a
  gap there. Files are not regenerated as a whole: a new patch gets the
  next free number,

	git format-patch -1 --start-number N -o patch-upstream <commit on upstream>

  and `upstream` is rebased onto each new `front` (`dist/docs/upstream.md`),
  dropping the commits 9front has taken.

## Applying

On Unix: `git am sys/src/build/host/patch-upstream/0*.patch` on a checkout of `front`.
On 9front: `git/import` takes the files as they are.

All 31 apply as a series (on `f5df6bc0a`, on `origin/front` at
`82ddeb288`, 2026-11-10, at `00d3043a2`, 2026-11-12, and at `eb49f1f87`,
2026-11-12); 0032 to 0034 on them at `eb49f1f87` (2026-11-14); all 38 at
`6ae667969` (2026-11-15), 0039 to 0044 on them there (2026-11-16), 0045
on them there (2026-11-17), 0046 and 0047 on them there (2026-11-18);
all 59 at `cb9e5e7e9` (2026-11-19), 0060 on them there (2026-11-19), 0061 to
0063 on them there (2026-11-20), 0064 and 0065 on them there (2026-11-21),
0066 on them there (2026-11-23); the 58 not archived on `2f3a62877`
(2026-11-25), as files, unchanged (`git am --3way`); 0067 on `upstream`
there (2026-11-26).
Each applies alone too, except:

- 0009 needs 0007 (context: `vbump` in `rwstat`),
- 0010 needs 0007 and 0009,
- 0012 needs 0007 (context: `vbump` in `ropen` and `rcreate`).

The commit messages end with a `Co-Authored-By:` line (the changes were
made with an AI assistant); drop it if 9front does not want it.

## How they were tested

- **Running**: the same code (apart from comments, and in `tcp.c` the
  port's own changes around it) runs in the `gcc` branch's system, built by gcc for amd64 and 386, booted in QEMU. Each
  patch below says which test found the problem and which one shows the
  fix. Most were found by the Rust port for 9front (`../9front-rust`: Rust's
  std test suite, cargo and rustc running on 9front, tokio).
- **9front's compilers**: on the `upstream` branch's sources and headers
  (not the gcc branch's), in a 9front guest: every `.c` of ext4srv,
  `ndb/cs.c` and `9/ip/tcp.c` (from `pc64` and `pc`) with `6c` and `8c`
  `-FTVw` (the mkfiles' flags); APE's `arm64/notetramp.c` with
  `7c -FTVw -p` against the patched `ureg.h`; `9/arm64/main.c` (0016)
  with `7c -FTVw`; `9/pc/sd53c8xx.c` (0017) with `8c -FTVw` (pc) and
  `6c -FTVw` (from pc64); `9/ip/devip.c` (0025) with `6c -FTVw` (pc64)
  and `8c -FTVw` (pc); `9/port/fault.c` (0023) with `6c -FTVw`
  (pc64), `8c -FTVw` (pc) and `7c -FTVw` (arm64). No errors, no warnings.
- **9front's compilers, 0048 to 0059**: on the `upstream` branch's
  sources and headers bound over the guest's, in 9space's system booted
  in QEMU (its `6c`, `8c` and `pcc` are 9front's compilers, built by
  gcc), each changed file's object made by its own mkfile:
  `archacpi`, `devswap`, `wifi` and `ethervgbe` for pc64 (`6c`) and pc
  (`8c`); APE's `sigset` (`6c` and `8c`), `sigprocmask`, `signal`,
  `fcntl`, `setvbuf` (`6c`), `ape/lib/9`'s `getenv` (`6c` and `8c`),
  `ape/lib/draw`'s `newwindow` and `scroll` (`6c`); reform's `shortcuts`
  (`6c`); and a `#if` on `INT8_MIN` to `INT64_MIN` with `pcc`. No errors,
  no warnings. 0048 changes comments only, 0063 a man page only. 0061 and 0062:
  `auth/factotum`'s `wpapsk`, `ip`'s `tftpfs` and `socksd`, each with
  `6c` (amd64) and `8c` (386) by their mkfiles, the same way: no errors,
  no warnings. 0060: `port/devsegment.c`
  with `6c` (pc64) and `8c` (pc), the same way. 0064 and 0065: not yet
  compiled with `6c`/`8c`; their code is the `gcc` branch's but for the
  member paths, built there for amd64, 386 and arm64.
- **Not done**: a kernel and ext4srv built with 9front's compilers and run
  with these patches. The kernel conf (0014), riostart (0015) and the
  dist mkfile (0024) are not compiled code; the gcc branch makes its CD
  with the same commands in `sys/src/build/host/guest/boot/mkiso.rc`, which has
  the change of 0024.

## Archive

`archive/` holds the patches 9front has taken (as they were sent; 9front's
commit is its own form of the fix). On `upstream` and in the branches the
change is then front's.

| # | 9front's commit | |
|---|---|---|
| 0001 | `026e87832` devip/tcp: only hang up other sides rq when still pointing to us | 2026-11-22 |
| 0020 | `890656a50` devip/tcp: raise error(Ehungup) when other side already close | 2026-11-22 |
| 0023 | `71f7e9dd5` kernel: fix SG_FAULT handling (fix \|\| vs ? precedence bug) | 2026-11-22 |
| 0032 | `e58d29dbf` libaml: handle Store() to anonymous (Buffer)Field | 2026-11-22 |
| 0037 | `1dabff7ac` kernel/pci: configure bars also for base class codes (0x13, 0x14 and 0xFF) | 2026-11-22 |
| 0046 | `9bff2eaa5` nusb/cam: fix skipping the '-' dash in pformatwrite() | 2026-11-22 |
| 0049 | `74016e4ec` pc, pc64: default to 0 for pcibusno() when _BBN is missing from root bridge | 2026-11-22 |
| 0061 | `11e6a6201` auth/factotum: fix wpapsk ptk calculation | 2026-11-22 |

## The patches

| # | Area | Kind |
|---|------|------|
| 0001 | ip/tcp: stale bypass pointer hangs up a reused conversation | bug fix, taken (`026e87832`, archive/) |
| 0002 | ip/tcp: full listener queue: keep the call in limbo, no reset | behaviour change |
| 0003 | ndb/cs: errors cut to 63 bytes | bug fix |
| 0004 | ext4srv: holes read block 0 of the disk | bug fix (data) |
| 0005 | ext4srv: "file not found" instead of "file does not exist" | bug fix |
| 0006 | ext4srv: truncating to the same length fails | bug fix |
| 0007 | ext4srv: qid.vers never changes | bug fix |
| 0008 | ext4srv: rwrite uses a stale file length | bug fix (data) |
| 0009 | ext4srv: wstat to a longer length fails | bug fix (stat(5)) |
| 0010 | ext4srv: old bytes come back after shorten and extend | bug fix (data) |
| 0011 | ext4srv: atime of a new file is 0 | bug fix |
| 0012 | ext4srv: exclusive-use files (DMEXCL) | new feature |
| 0013 | ape: arm64 Ureg layout differs from the kernel's | bug fix |
| 0014 | pc: xhci controller never registered | bug fix |
| 0015 | riostart: stats window can take the console's focus | bug fix |
| 0016 | arm64: the last cpu never comes up (sev lost) | bug fix |
| 0017 | sd53c8xx: boot hangs 600 s in scsiverify with SMP | bug fix |
| 0018 | ext4srv: a read of 0 bytes answers with a garbage count, ext4srv dies | bug fix |
| 0019 | ext4srv: remove and rename ignore the directory's permission | bug fix (permissions) |
| 0020 | ip/tcp: writes to a closed loopback peer vanish without an error | bug fix, taken (`890656a50`, archive/) |
| 0021 | ext4srv: reading on in a removed directory kills ext4srv | bug fix |
| 0022 | ape: lib9p's "file not found" is not ENOENT | bug fix |
| 0023 | fault: SG_FAULT segments let reads and writes through | bug fix, taken (`71f7e9dd5`, archive/) |
| 0024 | dist: the CD's ESP can overlap the end of the ISO | bug fix (data) |
| 0025 | ip: a closing conversation is given out again, its new connect hangs | bug fix |
| 0026 | truetypefs: antialiased subfonts (size `Na`) | feature |
| 0027 | vga: `cursorsize`, the software cursor scaled | feature |
| 0028 | aux/vga/mga4xx: register 0x14 of the attribute controller written past attribute[] | bug fix (found by gcc -O2) |
| 0029 | snap: a memory section's count ended one past its buffer | bug fix (found by gcc -O2) |
| 0030 | cwfs: readln put the NUL past the buffer on a long line | bug fix (found by gcc -O2) |
| 0031 | dossrv: a long name's checksum written past reserved[] | bug fix (found by gcc -O2) |
| 0032 | libaml: a store into the field Index gives was dropped | bug fix, taken (`e58d29dbf`, archive/) |
| 0033 | 9boot (efi): the boot device first, PXE through the firmware's MTFTP | bug fix |
| 0034 | bootrc: cdboot takes the ISO 9660 with this machine's init | bug fix |
| 0035 | kbmap: the maps sorted by name | bug fix |
| 0036 | nusb/kb: a collection's usage | bug fix |
| 0037 | pci: the BARs of a device of class 0xFF | bug fix, taken (`1dabff7ac`, archive/) |
| 0038 | nusb(4): cam's file is formats | documentation |
| 0039 | sam: journalv with snprint, not sprint | bug fix (a crash, only with -DDEBUG) |
| 0040 | sam: a file that does not exist yet is new when first read; a command on it under samterm was lost | bug fix |
| 0041 | ape/lib/9: exits and _exits with their argument in parentheses | bug fix |
| 0042 | postscript: the functions declared (atof, malloc, error's arguments wrong on a 64-bit machine) | bug fix |
| 0043 | compress, expr, antiword, spin: the functions they call declared (compress's utime) | bug fix |
| 0044 | news, zuke, pool, dma, doom, eqn, troff: no variable both changed and read without a sequence point | bug fix |
| 0045 | usbohci: bufferRounding on the last Td of an input transfer: a short last packet ends it | bug fix |
| 0046 | nusb/cam: format WxHxBPP-FOURCC in ctl: skip the '-' before the fourcc | bug fix, taken (`9bff2eaa5`, archive/) |
| 0047 | etheriwl: send the binding's time quota where the firmware has no DYNAMIC_QUOTA, not only where it has | bug fix |
| 0048 | kernel: the sources named instead of "stolen from" (comments) | provenance |
| 0049 | archacpi: a PCI root bridge without _BBN or _ADR is bus 0 | bug fix, taken (`74016e4ec`, archive/) |
| 0050 | ape: sigaddset refused most signals; sigprocmask with a nil set | bug fix |
| 0051 | ape: stdint.h's INTn_MIN usable in #if | bug fix |
| 0052 | ape: setvbuf with no buffer and size 0 wrote past malloc(0) | bug fix (memory) |
| 0053 | ape/lib/9: getenv returns a copy the caller frees, as Plan 9's | bug fix (memory) |
| 0054 | ape: a page fault's note is SIGSEGV | bug fix |
| 0055 | ape: fcntl with an unknown command fails with EINVAL | bug fix |
| 0056 | devswap: the pager waits instead of spinning when nothing can be freed or killed | bug fix |
| 0057 | reform/shortcuts: volume and mute under audio(3) as well | bug fix |
| 0058 | wifi: putrates wrote all rates again into the extended element | bug fix |
| 0059 | wifi: the link is up as soon as the access point is associated | bug fix |
| 0060 | devsegment: an ORCLOSE segment directory removed whatever was in its slot | bug fix |
| 0061 | factotum: wpapsk ordered the nonces by 6 bytes, not 32 | bug fix, taken (`11e6a6201`, archive/) |
| 0062 | ip/tftpfs, ip/socksd: sizeof(Udphdr) is 56 under 6c, udp(3) wants 52 | bug fix |
| 0063 | pipe(3): writes are atomic up to 65536 bytes, not "typically 32768" | documentation |
| 0064 | shr(3): a walk held the share's lock across the RPC to the mounted server: deadlock with a server removing its ORCLOSE entry at exit | bug fix |
| 0065 | chan.c: createdir held the mount head's lock across cclone (a walk RPC): the same deadlock | bug fix |
| 0066 | kernel: locals set after waserror() and read in its handler are volatile (mntwalk leaked a chan per failed walk) | bug fix |
| 0067 | ape: _dirfstat's second try with room for the stat's count (fstat failed for long names) | bug fix |

### 0001 ip/tcp: don't hang up a reused conversation through a stale bypass pointer

*Taken by 9front as `026e87832` (2026-11-22, in its own form); the patch is in `archive/`.*

A spliced loopback connection has each side's `bypass` pointing to the
other. A side that reaches Closed clears only its own. When the other side
closed later, `tcpclose()` hung up the rq of whatever conversation the
stale pointer named, by then often a new one: a new listener on it got
"listen hungup", its clients "connection refused". Now it hangs up only a
peer that still points back, as `tcpbypass()` checks already.

Found by: tokio tests in the Rust port (echo servers on 127.0.0.1, one
after another in one program). Shown by: `../9front-rust/tests/async.sh`.

### 0002 ip/tcp: keep a call in limbo when the listener's queue is full

When the last ack of a handshake comes and the listener already has
`Maxincall` (10) calls queued, or no conversation is free,
`tcpincoming()` dropped the call and `tcpiput()` sent a reset. The client
has the connection as established at that point (its connect returned), so
it reads eof right away on a connection it was told is open.

Now the call stays in limbo and the ack is dropped, as BSD and Linux do with
a full backlog: `limborexmit()` resends the syn-ack (up to 5 times, about 4
s in all), the client acks again, and the call comes in once there is room.
Later segments of such a call (they ack the syn-ack, their seq is past
irs+1) are dropped too instead of answered with a reset; the client sends
them again. If no room comes in time the limbo entry expires as before and
the client's next segment gets the reset.

This changes behaviour only when a listener's queue is full or no
conversation is free. Limbo stays bounded by `Maxlimbo` (1000), and keeping
a call there still needs the peer to complete the handshake (know iss).

Found by: tokio's echo test, 20 clients at once ("early eof", now and then;
the console showed `Fsnewcall: incall queue full (10)`). Shown by: a listener
that takes no calls for a second while 30 clients connect
(`../9front-rust/tests/crates/tokioecho`): without the patch 20 of them read
eof at once, with it all 30 get their echo.

### 0003 ndb/cs: don't cut errors to 63 bytes

`rwrite()` read a failed lookup's error into `curerr[64]`, and `sendmsg()`
puts "cs: " in front: for a long name the reason was cut off
("cs: can't translate address no-such-host-abcdefghijklmnopqrstuvwxyz"
without ".invalid: dns: name does not exist"), so a program cannot tell a
missing name from other failures. Now `ERRMAX`.

Found by: the Rust port's DNS test (`tests/crates/tlsdns`). Shown by: `echo
-n 'tcp!no-such-host-abcdefghijklmnopqrstuvwxyz.invalid!80' >/net/cs`.

### 0004 ext4srv: read holes as zeros, not block 0 of the disk

`ext4_fread()` handled a hole (an unallocated block, physical block 0) only
in a partial first block. In the loop over whole blocks and in a partial
last block it read physical block 0 of the partition into the file, and a
run started with `if(!fblock_start)`, so after a hole the next data block
was read in the hole's place (the data after a hole came out shifted). Now
a run starts when it is empty and a hole reads as zeros.

Found by: the gcc port building itself on an ext4 image made by Linux's
`mkfs.ext4 -d`, which makes every all-zero block a hole: binaries, archives
and object files read wrong.

### 0005 ext4srv: say "file does not exist" as other file servers

A missing file gave "file not found". Plan 9 says "file does not exist"
(Enonexist), and programs look for it: APE maps only that to ENOENT, so a
POSIX program took a missing file on ext4 for a hard error.

Found by: gcc on ext4srv stopping at the first `-I` directory without the
header instead of trying the next.

### 0006 ext4srv: truncating a file to the length it has is no error

`ext4_ftruncate()` refused any length not below the file's ("space
preallocation not supported"), the same length too: a wstat of length 0 on
an empty file failed.

Found by: cargo on ext4srv (Rust's `File::set_len(0)` on a new file).

### 0007 ext4srv: change qid.vers with every change of a file

qid.vers was the inode's generation, which writes never change (0 for a new
file). The kernel caches a program's text by qid: a program rebuilt in place
(new contents, the same inode number) ran with the old text and the new data
("invalid opcode"). Now vers is generation + mtime + a count of the changes
(write, create, truncation) per inode since ext4srv started, so it differs
after every change, also within the same second.

Found by: `cargo build` on an ext4 image, running a program it had just
rebuilt in place.

### 0008 ext4srv: rwrite takes the file's length from its inode

An open file keeps the length it had at open. `rwrite()` fills the gap from
the end to the write's offset with zeros using that length: after another fid
had made the file longer, the fill wrote zeros over that fid's data; after
another fid had truncated it, the gap was not filled. Now the length is read
from the inode first.

Found by: Rust's std test suite on ext4srv (two handles to one file).

### 0009 ext4srv: a wstat to a longer length extends the file with zeros

stat(5): a wstat of length sets the file's length, and what is past the old
end reads as zeros. Every length went to `ext4_ftruncate()`, which only
shortens, so any longer length failed. Now zeros are written up to the new
length first, as `rwrite()` does past the end. Needs 0007.

Found by: Rust's std test suite (`File::set_len` to a larger size; POSIX
`ftruncate` through APE likewise).

### 0010 ext4srv: zero the rest of the last block when a file is shortened

`ext4_ftruncate()` frees the blocks past the new length but leaves the old
bytes in the rest of the new last block; extending the file again (0009)
showed them where it should read zeros. Now the rest of that block is zeroed
before truncating. Needs 0007, 0009.

Found by: Rust's std test suite (`set_len` shorter, then longer, then read).

### 0011 ext4srv: set atime of a new file

`rcreate()` set mtime and ctime but left atime 0 (1970).

Found by: Rust's std test suite (metadata of a new file).

### 0012 ext4srv: exclusive-use files (DMEXCL)

open(5): a DMEXCL file can be open by one fid at a time. ext4srv dropped the
bit, so locks built on it locked nothing. Now it is kept in the inode as
`EXT4_INODE_FLAG_UNRM` (chattr's `u`, unused by Linux), as DMTMP is kept as
NODUMP; reported in stat and in the qid (QTEXCL); set by create and wstat. A
second open of such a file fails with "file is locked" until the first fid
is clunked. Locks are not broken after a time (open(5) allows a server to). Needs 0007.

Found by: cargo and rustc on 9front, which lock their build directories with
DMEXCL files. Shown by: the Rust port's `tests/lock.rs` on `/tmp` of a guest
whose `/tmp` is ext4srv; `e2fsck` of the image afterwards is clean.

### 0013 ape: arm64 Ureg in the kernel's layout

`/arm64/include/ape/ureg.h` had r29 and sp in a union and type, psr, pc after
r30; the kernel's `/arm64/include/ureg.h` has r29, r30, sp, pc, psr, type.
APE's `notetramp.c` sets `u->pc` to continue a note in the signal handler:
it set psr instead, and every APE program on arm64 that caught a signal died
at pc 0 (built with 7c too). Now the kernel's layout.

Found by: the gcc port's APE signal test on arm64 (QEMU).

### 0014 pc: register the xhci controller (usbxhcipci first, as in pc64)

The `link` section of a kernel conf calls only the first name's link
function. pc had `usbxhci pci usbxhcipci`: `usbxhcilink()` is empty, so
`usbxhcipcilink()`, which registers the controller type, never ran and 9pc
found no xhci controller. pc64 has `usbxhcipci pci usbxhci`.

Found by: reading the 386 kernel's conf while porting it (pc64 has the
order that works). Not tested with an xhci device on 9pc.

### 0015 glenda's riostart: make the stats window first, not in the background

`window 0,0,161,117 stats` is the old syntax; window(1) makes such a window
from a background job, which can finish after the windows made below it. A
new window becomes the current one, so stats could take the focus from the
console window (console=0: rc on the serial line) and typed input went to
stats. `window -r 0 0 161 117 stats -lmisce` makes it at once through wctl.

Found by: the gcc port's disk and gui tests, which boot a terminal and type
into the serial console window: the input was lost on about every second
boot under KVM.

### 0016 arm64: wake each new cpu until it runs

`mpinit` starts the other processors with PSCI CPU_ON; they wait in
`_start`'s `wfe` for an event, and nothing sent one: on a QEMU that models
wfe exactly (QEMU 11) no other cpu came up and `synccycles()` waited
forever. A single `sev()` after CPU_ON (as bcm64 does) is not enough: when
it comes before the new cpu runs, it is lost; each cpu was then woken only
by the sev for the next one, and the last one never came up. Now `mpinit`
sends `sev()` until the new cpu has set `active.machs[i]` in `machinit`.

Found by: the gcc port's NIX branch, booting arm64 (QEMU virt, cortex-a72)
with 4 to 8 cpus: cpu3 of 4 never came up, also without NIX. Shown by: the
boot test with 4 cpus and the NIX tests with 6 and 8 (all cpus up).

### 0017 sd53c8xx: clear the SCSI reset interrupt in busreset

`busreset()` pulses SCSI RST, which raises a SCSI reset interrupt (SIST0
RST). `sd53c8xxenable` enables the interrupt only afterwards; the pending
interrupt then reaches `sd53c8xxinterrupt` late, with SMP on another
processor, after the SCRIPTS were started for the first command and wait
for a reselection. The handler sets `c->running = 0` for the old reset,
the next start (a write of DSP) is ignored by the running chip, and the
command times out after 600 seconds. `busreset` now reads ISTAT, SIST0,
SIST1 and DSTAT after the reset, which clears it.

Found by: the boot test with 4 cpus (QEMU lsi53c895a, KVM), which hung
in `scsiverify` now and then (kbdfs's open of the disk); traced with
QEMU's `lsi_*` trace events and gdb on the hung guest. Shown by: 21 boots
with 4 cpus without a hang, where it hung at the first before.

### 0018 ext4srv: set the read count before returning on a read of 0 bytes

`ext4_fread()` returned early for a size of 0 before setting `*rcnt`, so
`rread()`'s count stayed uninitialized and went out as the Rread count,
larger than msize: lib9p's `respond()` printed "msize = ... n = 0 Rread ...
count 724576" and aborted, ext4srv died and the mount hung. Go's os package
does reads of 0 bytes. Found by: `go test` of Go's std on an ext4 disk
(arm64 guest). Shown by: the same `go test` runs through.

### 0019 ext4srv: remove and rename need write permission in the parent directory

remove(5) needs write permission in the file's directory. `rremove()`
checked only that the user owned the file: a file in a directory of mode
0555 could be removed. The rename in `rwstat()` said it checked the
parent's permission but checked the file's own. Both check the
directory's now (`haveparentperm()`).

Found by: Go's os tests (`TestRemoveAllButReadOnlyAndPathError`) on an
ext4 disk. Shown by: in a 0555 directory rm and a rename are refused, in
0775 both work.

### 0020 ip/tcp: a write to a spliced loopback connection whose other end is closed fails

*Taken by 9front as `890656a50` (2026-11-22, in its own form); the patch is in `archive/`.*

When one end of a spliced (bypass) loopback connection closes, it clears
only its own link; `tcpbypass()` then dropped everything the other end
wrote, without an error: a writer never learnt that the peer was gone
(over the network a reset tells it). Now the write fails with Ehungup, as
a write to a closed queue does. The flush through `qsetbypass()` in
`tcpsplice()` does not get there: both links are set just before.

Found by: Go's net tests (`TestWritevError`: 1 GB written to a closed
connection without an error). Shown by: the same write fails at once
("i/o on hungup channel"), the test passes.

### 0021 ext4srv: reading on in a directory that shrank or was removed ends instead of crashing

`ext4_dir_entry_next()` seeks to the offset where the last read stopped.
Past the directory's size (it was emptied or removed since, as by a
recursive remove while it is open) the iterator has no entry, and
`it.curr` was dereferenced anyway: ext4srv died ("fault read addr=0x6")
and the mount hung. Now that is the end of the directory.

Found by: Go's os tests (RemoveAll) on an ext4 disk in the arm64 guest.
Shown by: a program that opens a directory of 500 files, reads one
entry, removes it all and reads on: ext4srv died at once, now the read
ends and the server keeps answering (9front-go `tests/prog/dirremove.go`).

### 0022 ape: lib9p's "file not found" is ENOENT

lib9p answers a walk to a missing file with "file not found" (`srv.c`),
which APE's errno map (`_errno.c`) did not know: errno was EPLAN9 for a
missing file in ramfs and every other lib9p server. Programs that only
probe for a file and go on at ENOENT stopped instead.

Found by: cgo on the live CD (9front-go): gcc in `/tmp` (ramfs) gave up
on the first include directory without the header ("./stddef.h: file not
found"). Shown by: the same build goes through, errno is ENOENT.

### 0023 fault: SG_FAULT segments fault on every access

*Taken by 9front as `71f7e9dd5` (2026-11-22, in its own form); the patch is in `archive/`.*

The check in `fault()` reads `(attr & SG_FAULT) != 0 || read? (exec
fault) : (attr & SG_RONLY) != 0`, but `||` binds tighter than `?:`: it is
`((attr & SG_FAULT) != 0 || read)? ...`, so a segment with SG_FAULT
faulted only where an exec fault would, and reads and writes went
through. zynq and cycv set SG_FAULT on their `axi` segment until the
FPGA is loaded. Now the `?:` is parenthesized as intended; segments
without SG_FAULT are not affected.

Found by: the gcc port's GPU branch, whose segment(3) revokes a segment
by setting SG_FAULT: a process could still read a revoked segment.
Shown by: its test `sys/src/build/host/tests/gpu/segrevoke` (a read of a revoked segment
faults).

### 0024 dist: room for the ESP past the ISO

`%.iso` appends 2 MB to the ISO and puts the ESP into the last whole
cylinder of fdisk's geometry (`a p1 $-1 $`). A cylinder can be larger
than 2 MB (heads*sectors*512 bytes, up to 255*63*512 = 7.8 MB; 4 MB at
1.5 GB), so that cylinder could begin inside the ISO and disk/format
wrote the FAT over its end, where mk9660 puts the Joliet tree. Now 16
MB, two whole cylinders of any geometry.

Found by: the gcc port's GPU branch, whose CD with Mesa grew to 1.5 GB:
xorriso reported the image as "recorded in interleaved mode", the Joliet
tree was overwritten. Shown by: the same CD made with 16 MB is clean.

### 0025 ip: closeconv keeps its reference until the conversation is Idle

`closeconv()` took `inuse` to 0 first and set the conversation Idle last,
after the protocol's close. `tcpclose()` can let go of the conversation's
lock in between: `tcpoutput()` drops it around `ipoput4()`/`ipoput6()` to
send the FIN. If the ack of that FIN came in meanwhile (Last_ack ->
Closed, on another processor or in the loopback reader), the
conversation was free by `Fsprotoclone()`'s test (`inuse` 0, the
protocol done with it), and a process opening `clone` got it and wrote
`connect`. Then `closeconv()` went on and set it Idle over that
process's Connecting (or Connected): `Fsconnected()` only moves
Connecting to Connected, and `connectctlmsg()` slept for ever on
`connected` while the TCP side was Established, often spliced already.
Now `closeconv()` drops its reference at the end, with the Idle, under
the lock; nothing else in the close looks at `inuse`.

Found by: dial(2) over loopback hanging under load (the Rust port's
parallel std net tests; reproduced with a program of four procs
listening, accepting and closing and twelve procs dialing 200 times
each: it hung after about a thousand dials in every run, with the
pristine kernel built by 6c too). Pinned down with a log of the
conversations' events read through gdb: on every hung dialer's
conversation the previous owner's `closeconv()` ended after the
dialer's connect had begun. Shown by: the same program with 12, 24 and
50 dialing procs runs to the end, repeatedly.

### 0026 truetypefs: antialiased subfonts (size "Na")

A size followed by `a` (`/n/ttf/Go-Mono.ttf.26a/font`) gives grey (`k8`)
subfonts: each glyph rasterised at four times the size, a pixel the
share of its 4×4 pixels there that are set; without the `a` the
subfonts are one bit deep as before.  Found by: the `main` branch's (then `ui`)
HiDPI (fonts of 26 and more pixels for a scale of 2 looked jagged one
bit deep).  Shown by: the `main` branch's (then `ui`) sys/src/build/host/tests/ui `hidpi.rc` (wsys -S 2
gives its text windows and programs `Go-Mono.ttf.26a`, 31 pixels high,
drawn through it).  Compiled with `6c -FTVw` and `8c -FTVw` against
the `upstream` branch's headers in a 9front guest; no errors, no
warnings.

### 0027 vga: cursorsize, the software cursor scaled for a monitor with many pixels to the inch

vgactl's `cursorsize n` (16 to 64) draws the software cursor `n` pixels
big, its 16×16 picture scaled (nearest) and its offset with it; reading
vgactl gives the size; hardware cursors stay 16×16.  Shown by: the `ui`
branch's sys/src/build/host/tests/ui `hidpi.rc` (VESA, software cursor: 32, 24, 16 read
back at scales 2, 1.5, 1).  `devvga.c` (pc, and pc64 from `../pc`) and
`port/swcursor.c` compiled with `8c -FTVw` (pc) and `6c -FTVw` (pc64)
against the `upstream` branch's headers; no errors, no warnings.  Not
done: a kernel built by 9front's compilers booted with it.  (The `ui`
branch also passes the size to its GPU drivers' cursor planes; that
part needs its devgpu and is not here.)

### 0028 aux/vga/mga4xx: attribute[] holds registers 0x00-0x14

`init` and `load` run over the attribute controller's registers 0x00 to
0x14 (`i < 0x15`), writing and reading `mga->attribute[i]`, but the array
had 0x14 entries: register 0x14 went one byte past it, into the next
field of the `Mga` struct.  Found by gcc's `-Waggressive-loop-optimizations`
when the gcc branch's userland went to `-O2` (gcc may then end such a
loop early).  The file is the same on `front`; the patch touches nothing
else of the series.  Built with gcc -O2 on the main branch (aux/vga);
not done: compiled with 9front's own compilers, run on a Matrox card.

### 0029 snap: room for the NUL after a memory section's 12-byte count

`readsnap` reads a memory section's segment count with
`Bread(b, buf, 12)` and ends it with `buf[12] = 0`, one past `char
buf[12]`.  The buffer is 13 bytes now.

### 0030 cwfs: readln leaves room for the NUL

`readln` stored up to `len` characters and then the NUL after them: a
line of 4096 characters or more in the users file put the NUL one past
the static `buf`.  It stores `len-1` now (`while(--len > 0)`).

### 0031 dossrv: a long name's checksum into the field at offset 13

`putnamesect` wrote the checksum of a VFAT long name's slot as
`ds.reserved[1]`, past `Dosdir`'s one-byte `reserved`; offset 13, where
the slot keeps the checksum, is `ctimetenth`.  The same byte as before,
written through the field that is there.

0029 to 0031 were found by gcc's `-Wstringop-overflow` when the main
branch's userland went to `-O2`; built there with gcc -O2 (snap, cwfs,
dossrv), dossrv run by the CD's boot (9fat).  Not done: compiled with
9front's own compilers.

### 0032 libaml: a store into the field Index gives

*Taken by 9front as `e58d29dbf` (2026-11-22, in its own form); the patch is in `archive/`.*

`store()` took only references and names as its target; the buffer
field `Index` returns fell into the default case and `Store (x, Index
(BUFF, i))` changed nothing, without an error. A Lenovo ThinkBook 16 G7
ARP's ATIF method (function 0x10, the brightness curve) fills its reply
buffer so: amdgpu got the previous call's reply, read 128 data points
from it where 99 fit, and wrote past its buffer. Now `rwfield` writes
such a target.

Found by: the main branch's amdgpu on that laptop (a pool panic, the
block's malloc tag naming `initialize_backlight_caps`; the method read
from the laptop's SSDT). Shown by: the main branch's `sys/src/build/host/tests/gpu`
(`nfst.py`, an SSDT with that store, loaded by QEMU's `-acpitable`,
evaluated through aux/acpi). `libaml/aml.c` compiled on the `upstream`
branch's sources with `6c -FTVw` and `8c -FTVw` in a 9front guest: no
errors, no warnings.

### 0033 9boot (efi): the boot device first, PXE through the firmware's MTFTP

- plan9.ini and the kernel are looked for first on the device the
  loader came from (LoadedImage's DeviceHandle; a partition counts as
  its disk by the device path): with another system's ISO or disk
  before the stick, 9boot took that one and stopped at its prompt
  ("no bootfile").
- PXE: files are read with the firmware's `Mtftp`; 9boot's own UDP
  TFTP is the fallback and checks the server's port (RFC 1350). The
  ThinkBook's `UdpRead` returned a new packet's port with the previous
  packet's data: blocks came twice, the kernel was garbage. The server
  is taken from the PXE reply, the proxy offer or the DHCP ack (proxy
  DHCP beside the router).
- ISO: sectors through a 4096-aligned buffer, blocks up to 4096 bytes.

Built with 9front's compilers (8c/6c, `sys/src/build/host/guest/build-boot.sh` in
a guest). Shown by: the ThinkBook booted by PXE (proxy DHCP, dnsmasq),
the main branch's `sys/src/build/host/tests/iso` (a disk booted with another ISO before
it, UEFI).

### 0034 bootrc: cdboot takes the ISO 9660 with this machine's init

With `cdboot`, `local.rc` took the first 9660 file system as the root,
another system's ISO before ours too. `cdsystem` mounts each and takes
the first with `/$cputype/init`, else the first as before. Shown by:
the same `sys/src/build/host/tests/iso` case.

### 0035 kbmap: the maps sorted by name

kbmap listed `/sys/lib/kbmap` in the order its file server reads the
directory: alphabetical from hjfs and cwfs, any order from others (the
main branch's ufs serving a root from a Linux host). It sorts the names
now. Shown by: kbmap on a machine booted by PXE with that root. Not
done: compiled with 9front's own compilers.

### 0036 nusb/kb: a collection's usage

`repparse1` cleared the local items at a Collection, then read its usage
from `l[Nu]`, past the list of usages (the first is `l[Nl]`): every
collection's usage was only its page, and `hidparse`'s check for a
touchscreen's finger (0x0D0022) that is not touching never matched. The
usage is taken before the locals are cleared now. Found by: the main
branch's precision touchpad code (aux/i2chid, `sys/src/build/host/tests/hid`), which looks
for the touchpad's collection 0x0D0005. Not done: compiled with 9front's
own compilers, tried on a touchscreen.

### 0037 pci: the BARs of a device of class 0xFF

*Taken by 9front as `1dabff7ac` (2026-11-22, in its own form); the patch is in `archive/`.*

`pciscan` read and sized the base address registers only for the classes
it lists; a device that fits no class (0xFF: Realtek's PCI Express card
readers) kept `p->mem[]` empty, and its driver found no registers. Class
0xFF is read as the others now. Found by: the main branch's rtsx driver on
a Lenovo ThinkBook 16 G7 ARP (RTS522A: "no registers", while Linux shows
BAR 0 at 0x98900000 and 9front's `#$/pci/3.0.0raw` has it too). Shown by:
the card reader then reading an SD card. Not done: compiled with 9front's
own compilers.

### 0038 nusb(4): cam's file is formats

The page named the file of a camera's resolutions and frame rates
`format`; nusb/cam serves `formats`. Found by: reading it on a laptop's
camera.

### 0039 sam: journalv with snprint, not sprint

sam built with `-DDEBUG` writes its protocol with samterm to
`/tmp/sam.out`; `journalv` called `sprint(buf, sizeof(buf), "%lld", v)`,
the buffer's size as the format, and sam crashed at the first vlong
(samterm's `Tstartcmdfile`). Found by: the main branch's reference
workflow test (`sys/src/build/host/tests/workflow`), reading sam's journal under samterm.
Shown by: the journal then complete (`Tstartcmdfile`, its pointer, the
`H` messages after it), with a sam built by gcc. `mesg.c` of the
`upstream` branch compiles with `6c -FTVw` and `8c -FTVw`, with and
without `-DDEBUG` (6c), in a 9front guest; no warnings.

### 0040 sam: a file that does not exist yet is new, not an error, when first read

Under samterm, sam reads a file only when a command first needs it
(`cmdexec` → `load` → `edit(f, 'I')`). For a file that does not exist
yet, `edit` ended in `error_r(Eopen, ...)`, which cancels the command
that caused the read: `sam newfile`, then `a/text/` typed in the
command window appended nothing (samterm showed only `?can't open`), and
`w` wrote an empty file. With `sam -d` the same read happens at the
start, outside any command, so nothing was lost there. Now the first
read of a file that does not exist (`access(..., AEXIST)` fails) warns,
`no such file`, and leaves it new and empty; the command goes on. Any
other failure to open it (no permission, ...) is still an error, so that
`w` cannot write an empty buffer over a file that is there. Found by:
the main branch's reference workflow test (`sys/src/build/host/tests/workflow`): `w` wrote
`#0`; sam's journal (0039) showed the `a` command arriving and `can't
open` cancelling it. Shown by: the same steps then writing the 15
characters, under samterm in wsys and with `sam -d`. sam's `.c` files
of the `upstream` branch compile with `6c -FTVw` and `8c -FTVw` in a
9front guest; no warnings.

### 0041 ape/lib/9: exits and _exits with their argument in parentheses

APE's Plan 9 compatibility header (`/sys/src/ape/lib/9/libc.h`) defined
`exits(s)` as `exit(s && *(char*)s ? 1 : 0)`: an argument with an
operator of lower precedence than `&&` came apart, so
`exits(nfail ? "fail" : nil)` gave `exit` the string `"fail"` (a pointer
as its status) instead of 1. Found by: gcc, once int-conversion was an
error again (the main branch's `sys/src/build/host/tests/guest/ape9test.c`). Not done:
compiled with 9front's own compilers (a macro; nothing in the tree used
it with such an argument).

### 0042 postscript: the functions declared

The postscript programs on APE are compiled with `pcc -B` and called the
C library's and `common`'s functions undeclared, as returning `int`. On
a 64-bit machine that is wrong where they do not: `postreverse`'s `atof`
gave the file's `%%Version` as garbage, `download`'s `malloc` and
`realloc` lost the high half of their pointers, and `common/misc.c`'s
`error` took its arguments as `unsigned int` while its callers pass
strings for its `%s`. `common/ext.h` now includes `stdlib.h`, `string.h`
and `unistd.h` for the programs on APE (`tr2post`, a native program, is
left alone) and declares `common`'s functions; `error` is variadic
(`vfprintf`), `lineno` and `position` (longs) are printed with `%ld`;
each program declares its own functions before their use; `tcpostio` is
compiled with `_POSIX_SOURCE` (it calls `read`, `fork`, `select`) and
takes its child's status from `wait` in an `int` (an `unsigned long` got
4 of its 8 bytes on amd64); its own `getline` is `fdgetline` (POSIX 2008
has a `getline` with other arguments, which an APE with it declares). Found by: gcc's implicit-function-declaration
(an error since GCC 14). Shown by: the programs compiled by gcc with no
implicit declaration left; with 9front's own `mk` in a 9front guest
(amd64 `pcc`/`6c`, 386 `pcc`/`8c`) they compile as on `front` (6c's
"argument prototype mismatch" found `tcpostio`'s `wait`). Not done: the
programs run on PostScript input.

### 0043 compress, expr, antiword, spin: the functions they call declared

`compress`: `unistd.h` and `utime.h` (with `_POSIX_SOURCE`), a prototype
for `foreground`, and `utime` given a `struct utimbuf` (it was a
`time_t[2]`, which 6c rejected once `utime` was declared); `expr`:
`stdlib.h`, `string.h`, `unistd.h` (it calls `write`) with
`_POSIX_SOURCE`, its own and `regexp.h`'s functions declared, its old
`char *malloc();` gone; `antiword`: `strcasecmp` and `getopt` declared
(9front's APE has them in no POSIX header); `spin`: `stdlib.h` for
`exit`. Found and shown as 0042.

### 0044 news, zuke, pool, dma, doom, eqn, troff: no variable both changed and read without a sequence point

gcc's `-Wsequence-point` found seven expressions whose result C leaves
undefined, as every compiler may order them: `news`
(`if((d = dirstat(dir))==nil | hi==d->qid.vers)`: `|` for `||`, so a
nil `d` was read), `zuke` (`*f++ = clip16(*f * g)`: the sample scaled
could be the next one), libc's `pool` (`*lp++ = sig ^ (lp - v)`: the
fill pattern's offset with `lp` before or after its `++`), `pc/dma`
(`pa` assigned on one side of `!=`, read on the other), `doom`
(`eventtail = (++eventtail)&...`), `eqn` (`n = n%2 ? n : ++n`) and
`troff` (`numtabp[ST].val` assigned twice in one expression). Each made
explicit. Found by: a build with `-Wsequence-point` (the gcc branch now
builds with `-Werror=sequence-point`). Not done: compiled with 9front's
own compilers.

### 0045 usbohci: bufferRounding on the last Td of an input transfer: a short last packet ends it

Under QEMU's `pci-ohci` no USB device was ever attached ("no data from
device", then "too many attaches"), with 9front's own release as well:
the configuration descriptor is read asking for more bytes than the
device has. Without the R (bufferRounding) bit QEMU retires such a Td
with "data underrun" and leaves CBP as it was (real controllers advance
it), which `qhinterrupt` took for 0 bytes. With R the controller reports
the bytes through CBP and does not halt the endpoint; FreeBSD, OpenBSD
and Linux set it. Only on the last Td of an input transfer: a short
packet before it still stops the endpoint. Found and shown by the gcc
branch's `sys/src/build/host/tests/usb` (a stick on OHCI: root hub, `/dev/sdU*`, sector 0
read back; now passes on xHCI, EHCI, UHCI and OHCI). Not done: compiled
with 9front's own compilers.

### 0046 nusb/cam: format WxHxBPP-FOURCC in ctl: skip the '-' before the fourcc

*Taken by 9front as `9bff2eaa5` (2026-11-22, in its own form); the patch is in `archive/`.*

Writing back what `ctl` reads, `format 640x480x16-YUY2`, failed with
"invalid argument": after the bits per pixel the parser was left on the
`-`, so the fourcc had five characters and was compared as `-YUY`. Only
the short form `format WxH` worked. Found on real hardware: a ThinkPad
X1 Carbon 3rd's integrated camera (04ca:7049, YUY2 only) booted by PXE;
the fix shown there (switching to `1280x720x16-YUY2` and back, a frame
read in each). Not done: compiled with 9front's own compilers.

### 0047 etheriwl: send the binding's time quota where the firmware has no DYNAMIC_QUOTA, not only where it has

`setbindingquotas` sent the TIME_QUOTA command only where the firmware
has the DYNAMIC_QUOTA capability (capa[1] bit 12), the one case where
it sets the quotas itself; OpenBSD's `iwm_run` sends it exactly where it
lacks it. A Wireless 7265 (`iwm-7265-17`) never got one: frames to it
waited for the next beacon (round trips up to 100 ms, a sawtooth over
ping intervals) and it sent ~250 frames a second, ~2 Mbit/s. Found and
shown on a ThinkPad X1 Carbon 3rd booted by PXE, 9space's kernel (its
etheriwl also has 802.11n and 5 GHz): with the quota ~1 ms and ~22
Mbit/s each way. The entries carry `low_latency` where the firmware's
API has QUOTA_LOW_LATENCY (38), as `iwm_update_quotas`. Not done:
compiled with 9front's own compilers; run on 9front's own driver.

### 0048 kernel: name the sources instead of "stolen from"

Comments only. `ethervgbe.c`'s register layout follows FreeBSD's vge(4)
(`if_vgereg.h`); Bill Paul's BSD notice is now kept in full. The omap and
mt7688 `fpi.c` name their origin: Plan 9 from Bell Labs' ARM kernels,
after an original by ark (Plan 9 Foundation, 9front authors; MIT).

### 0049 archacpi: a PCI root bridge without _BBN or _ADR is bus 0

*Taken by 9front as `74016e4ec` (2026-11-22, in its own form); the patch is in `archive/`.*

`pcibusno` failed for a root bridge (PNP0A03, PNP0A08) with neither
`_BBN` nor `_ADR`. ACPI allows `_BBN` to be left out there, and then it
means bus 0; QEMU's generated DSDT does exactly that. Every `_PRT` under
such a bridge was dropped, so ACPI routed no PCI interrupts.

### 0050 ape: sigaddset takes every signal; sigprocmask with no set changes nothing

`stdsigs` or'ed the signals' numbers, not their bits:
`SIGHUP|...|SIGUSR2` is 15. So `sigaddset` refused every signal whose bit
is not in 15 (SIGTERM, SIGCHLD and others), and `sigfillset` filled in
15. It now holds each signal's bit, the job-control signals included.
`sigprocmask(how, nil, &old)`, POSIX's way to read the mask, read
`*set` through the nil pointer.

### 0051 ape: stdint.h's INTn_MIN usable in #if

`INT8_MIN` to `INT64_MIN` were casts, which `#if` refuses; C99 7.18.2
wants them usable there (libxkbcommon's `state.c` tests them). They are
now written as the most negative values without a cast, and their types
stay as before (int, long long).

### 0052 ape: setvbuf with no buffer and size 0 allocates BUFSIZ

`setvbuf(f, nil, _IOFBF or _IOLBF, 0)` asks stdio to choose the buffer.
It did `malloc(0)` with a buffer length of 0, and the writes then ran
past it into the heap (SDL does this call). Now it allocates BUFSIZ.

### 0053 ape/lib/9: Plan 9's getenv for Plan 9's sources, a copy the caller frees

libdraw built for APE (`newwindow.c`, `scroll.c`) frees what `getenv`
returns, as with Plan 9's `getenv`. APE's `getenv`, however, returns the
string inside the environment, which was then freed and used again.
`ape/lib/9`'s `libc.h` now maps `getenv` to `_getenv9`, with Plan 9's
semantics: the `/env` file is read at the call into a copy of its own,
and a list's NULs become blanks.

### 0054 ape: a page fault's note is SIGSEGV

For a page fault the kernel posts "sys: trap: fault read addr=..."
(`port/fault.c`). APE's table had only the older texts (bus error,
address error, TLB), so the fault never reached a SIGSEGV handler and
ended the program instead.

### 0055 ape: fcntl with an unknown command fails with EINVAL

`fcntl`'s switch had no default case: an unknown command returned 0.
Mesa's `os_dupfd_cloexec` asks for F_DUPFD_CLOEXEC, which APE lacks, and
took the 0 as the new descriptor (stdin). POSIX wants EINVAL.

### 0056 devswap: the pager waits instead of spinning when nothing can be freed or killed

The pager could hold a processor for good when memory was full and
only the host owner's noswap processes were left. Either `killbig` found
nobody to kill and the pager tried again at once, or the process it had
killed could not exit (its mount RPC waited for the flush of a server
that was itself waiting for pages) and `killbig` said "try later" over
and over. `killbig` now returns Killed, Exiting or Nokill. The pager
waits 100 ms for an exit, and 1 s when nobody may be killed, printing
"pager: out of memory, no process to kill" once. FreeBSD's pagedaemon
likewise goes on to its next pass when `vm_pageout_oom` finds no
process. Shown in QEMU (256 MB, a ramfs filled past memory, every
process noswap): QEMU used 2000 ticks of processor time in 20 s before,
14 after. The deadlock itself remains (the server holds the pages), but
it no longer burns a processor.

### 0057 reform/shortcuts: volume and mute under audio(3) as well

`shortcuts` assumed reform's audio driver. Under audio(3), "master +3"
sets the volume to 3%, not 3 more, and there is no "master toggle".
Volume up and down now try "mix" first; otherwise they set master from
what `/dev/volume` says, kept between 0 and 100. Mute remembers the
volume and restores it where the device has no toggle.

### 0058 wifi: putrates writes the rates past 8 into the extended element, not all of them again

With more than 8 rates, `putrates` cut the supported rates element to 8
and then wrote all n rates again into the extended supported rates
element (50), with length n. The first 8 rates appeared twice, and probe
and association requests grew n bytes longer than their rates. The
extended element now holds the rates after the first 8, with the basic
rates first in both elements as before.

### 0059 wifi: the link is up as soon as the access point is associated

The status showed "associated" (or "blocked", waiting for the WPA
handshake) up to 500 ms before `ethersetlink(1)`, which only came with
`wifsproc`'s next round. devether drops what is sent while the link is
down, so the first packet after an association was lost (the first ping
to a new access point). `setstatus` now raises the link when the bss
becomes associated or blocked.

### 0060 devsegment: an ORCLOSE segment directory removes the segment it made, not whatever is in its slot

`segmentclose` removed the segment of a chan created with ORCLOSE by
its slot number. If that segment had been removed by name meanwhile and
another one created in the same slot, the close removed the other one.
The chan now keeps a reference to the segment it made and removes it
only if the slot still holds it. Found while making 9space's GPU drivers
create their segments ORCLOSE (so that a driver the pager kills leaves
none behind), by reading the code: no test provokes the slot reuse
itself. The same change runs in 9space's system in QEMU under
sys/src/build/host/tests/gpu (its killdrv check: the driver killed while vkcube draws,
none of its segments left). Not done: run on 9front's own kernel.

### 0061 factotum: wpapsk orders the nonces by all 32 bytes, not the first 6

*Taken by 9front as `11e6a6201` (2026-11-22, in its own form); the patch is in `archive/`.*

`calcptk` compared the supplicant's and the authenticator's nonces with
`memcmp(..., Eaddrlen)`: their first 6 bytes. IEEE 802.11i's PTK takes
Min(ANonce, SNonce) || Max(ANonce, SNonce) over all 32 bytes, so nonces
equal in their first 6 bytes (2^-48 of the handshakes) gave another PTK
than the access point's, and that handshake failed. Found by reading
the code. Shown by the `gcc` branch's `sys/src/build/host/tests/guest` wpatest: a factotum
of its own, the PTK through its rpc file against values computed from
the standard on the host (Python's hashlib and hmac; the PBKDF2 path
checked against the standard's H.4 vector): nonces equal in their
first 6 bytes give 613c14ef... with the old code, 27177eee... (right)
with this one.

### 0062 ip/tftpfs, ip/socksd: the UDP header Udphdrsize bytes, not sizeof a struct around it

Both use udp(3)'s `headers` form through a struct of a `Udphdr` and
the data after it, with `sizeof(Udphdr)` as the header's length.
udp(3) takes the first `Udphdrsize` (52) bytes as the header. 6c pads
every struct to 8 bytes at its end (`cc/dcl.c` `sualign`, `6c/swt.c`
`Asu2`): `sizeof(Udphdr)` is 56 and the data starts at 56, so tftpfs on
amd64 sent 4 bytes of padding in front of its requests and read every
reply 4 bytes too far on (its opcode and block number skipped); socksd's
UDP relay likewise. 8c pads to 4, and 52 comes out right by chance. Now
one byte buffer, the header at 0 and the data at `Udphdrsize`. Found by
comparing every struct's layout under gcc with 6c's rules (the `gcc`
branch's port checks; gcc gives 52). Not done: tftpfs run against a
TFTP server on 9front amd64.

### 0063 pipe(3): writes are atomic up to 65536 bytes, not "typically 32768"

The kernel's queues take a write whole up to `Maxatomic`, 64*1024
(`port/qio.c`), and `qwrite` splits a longer one into pieces of that
size; devpipe writes through `qwrite`. `Maxatomic` has been 64 KB at
least since the sources of 2011 (the first import); the page still said
32768.

### 0064 shr(3): a walk into a share's mounts without the share's lock

`shrwalk` held the share's `rlock` across the walk into each mount, an
RPC to the file server mounted there. A server that removes its entry
as it exits (ORCLOSE: `shrremove`'s `wlock`) then waited for that
`rlock` while the walk waited for the server's answer, which never
came: its pipe stays open until its exit, which waited. Both hung for
good. Found with a GPU driver posted in `/shr/gpu/N` (9space's gpu(4)),
killed while clients walk into its tree: about one run in ten, nearly
every run with a loop walking there across the kill. Kernel stacks:
the server's last proc in `pexit`, `closefgrp`, `cclose`, `shrremove`,
`wlock`; the client in `shrwalk`, `mntwalk`, `mountrpc`, `rpcattn`. The
walk now goes on references to the mounts' chans taken under the lock,
as `findmount` does with a name space: with it no hang in ten runs of
the loop that hung two in three.

### 0065 chan.c: createdir clones the mount without the mount head's lock

`createdir` held the head's `rlock` across `cclone` of the MCREATE
mount, a walk: an RPC to the server mounted there. An unmount or a
share entry's removal (`wlock`) behind it then waited for a server that
may itself be waiting to go, the deadlock 0064 removes from shr(3)'s
walk. The clone now goes on a reference taken under the lock, as
`findmount` does. Not seen hanging; found reading for 0064's pattern.

### 0066 kernel: locals set after waserror() and read in its handler are volatile

After error()'s longjmp a local that changed after its waserror() is
indeterminate unless volatile (C11 7.13.2.1); a compiler that keeps it in
a register gives the handler its value at waserror() time. Built with gcc,
devmnt's mntwalk read `alloc` as 0 and lost the clone chan of every failed
walk: 300 failed walks leaked 524 KB in a union directory, a gcc-built
kernel ran out of heap during a build; a kencc kernel of the same source
leaked nothing. The others of the pattern, found by reading each handler
(tree's sys/src/build/host/check/find-clobbered.py lists the candidates): write (n:
the chan's offset not restored), shrcreate (nc), tlswstat (d), wifictl
(cb), pcmread/pcmwrite (m). namec's c is not among them: its address goes
to walk and domount, so it lives in memory. Tested: the walk leak with a
gcc kernel on an installed 9space (gone, and a whole gccmk build kept the
kernel heap at 5.7-7.1 MB); the six files with `6c -FTVw` in a guest
(devmnt, devshr, devtls, sysfile from `upstream`; devi82365 and wifi in
`main`'s form, whose headers the guest has).

### 0067 ape: _dirfstat's second try with room for the stat's count

A stat reply too short for the buffer gives the size needed in its first
16 bits, without those two bytes. `_dirstat` asks again with
`BIT16SZ+nd`, `_dirfstat` asked with `nd` alone, two bytes short, and
failed a second time: fstat failed for every file whose stat outgrows the
first buffer (`STATFIXLEN+64`), a name of more than about 45 bytes for
glenda's files. gcc's cc1 opens a file and fstats it, and called such a
file missing (Mesa's `intel_nir_lower_non_uniform_barycentric_at_sample.c`,
found building 9space on itself on a ThinkPad X1; ratrace showed Fstat
returning 2). Tested with file names of 40 to 56 bytes and the native gcc
on the installed system: before the fix from 47 bytes on no such file,
after it (gcc relinked with the fixed libap) all compile.

## Not included

- The rest of the `main` branch's (then `ui`) HiDPI: libdraw's `display->dpi` and
  `scalesize`, and acme, samterm, the menus, page, vt, vnc and
  image/histogram sizing their fixed pixels by it.  Without a window
  system that serves a `dpi` file (rio serves none) it changes
  nothing; it belongs with a rio that draws at a scale.

- TCP half close (ctl `shutdown`, the gcc branch's `tcpshutdown`): a new
  interface added for the Rust port (`shutdown(Write)`), not a fix.
- Everything about gcc (types, `#pragma pack`, `volatile`, GAS ports,
  member paths, the build tool).
