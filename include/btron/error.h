/*
 * B-TRON Specification Compatible Header: error.h
 * Standard TRON Error Codes (BTRON 3.20 Complete Normative Set).
 */

#ifndef _BTRON_ERROR_H_
#define _BTRON_ERROR_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Standard Error Codes (Defined in Base Specification) ────────── */
#ifndef E_OK
#define E_OK        (0)   /* Normal completion */
#endif
#ifndef E_SYS
#define E_SYS      (-5)   /* System error */
#endif
#ifndef E_NOMEM
#define E_NOMEM    (-10)  /* Out of memory */
#endif
#ifndef E_NOSPT
#define E_NOSPT    (-17)  /* Feature not supported */
#endif
#ifndef E_RSVR
#define E_RSVR     (-25)  /* Reserved error */
#endif
#ifndef E_PAR
#define E_PAR      (-33)  /* Parameter error */
#endif
#ifndef E_LIMIT
#define E_LIMIT    (-34)  /* Limit exceeded */
#endif
#ifndef E_ID
#define E_ID       (-35)  /* Invalid ID */
#endif
#ifndef E_OBJ
#define E_OBJ      (-41)  /* Invalid object state */
#endif
#ifndef E_NOEXS
#define E_NOEXS    (-52)  /* Object does not exist */
#endif
#ifndef E_BUSY
#define E_BUSY     (-65)  /* Resource busy */
#endif
#ifndef E_TMOUT
#define E_TMOUT    (-69)  /* Timeout */
#endif

/* ── Extended BTRON 3.20 Subsystem Error Codes ──────────────────── */
#define ER_ADR     (-1)   /* Invalid pointer address / memory address */
#define ER_NOSPC   (-11)  /* Insufficient memory / storage space */
#define ER_ACCES   (-12)  /* Insufficient privilege / access denied */
#define ER_IO      (-13)  /* Hardware I/O device error */
#define ER_DID     (-14)  /* Invalid device descriptor / device ID */
#define ER_ROVR    (-15)  /* Read-only violation */
#define ER_DLT     (-16)  /* Object deleted during waiting state */
#define ER_CTX     (-18)  /* Invalid calling context / task context */
#define ER_FD      (-19)  /* Invalid file descriptor */
#define ER_NOFS    (-20)  /* File system not mounted */
#define ER_NODSK   (-21)  /* No disk space available */
#define ER_RONLY   (-22)  /* Volume or medium is read-only */
#define ER_FNAME   (-23)  /* Invalid file or path name */
#define ER_EXS     (-24)  /* File or directory already exists */
#define ER_PWD     (-26)  /* Wrong password / authentication failed */
#define ER_PERM    (-27)  /* Permission denied */
#define ER_OVRW    (-28)  /* Write-protected configuration / overwrite prevented */
#define ER_OVVR    (-29)  /* Resource overflow (semaphore/queue max exceeded) */
#define ER_TIMEOUT (-69)  /* Timeout (BTRON specification alias) */

/* ── Standard BTRON Alias Macros ─────────────────────────────────── */
#define ER_OK      E_OK
#define ER_SYS     E_SYS
#define ER_NOMEM   E_NOMEM
#define ER_NOSPT   E_NOSPT
#define ER_RSVR    E_RSVR
#define ER_PAR     E_PAR
#define ER_LIMIT   E_LIMIT
#define ER_ID      E_ID
#define ER_OBJ     E_OBJ
#define ER_NOEXS   E_NOEXS
#define ER_BUSY    E_BUSY
#define ER_TMOUT   E_TMOUT

/* ── Partition & Disk Subsystem Error Codes (-501 .. -519) ──────── */
#define ER_PART_NOMBR      (-501)  /* Missing 0x55AA MBR boot signature */
#define ER_PART_BADGPT     (-502)  /* Protective MBR found but GPT header invalid */
#define ER_PART_NOTFOUND   (-503)  /* Requested BTRON partition slot/type not found */
#define ER_PART_POOLFULL   (-504)  /* Static partition descriptor pool exhausted */
#define ER_PART_READONLY   (-505)  /* Partition table protected by platform storage tier */
#define ER_PART_OVERLAP    (-506)  /* Requested LBA range overlaps ESP or existing slice */
#define ER_PART_SLICE_DIS  (-507)  /* Direct .vol slice boot disabled on UEFI */

/* ── B-FS V1 & Copy Engine Error Codes (-520 .. -539) ───────────── */
#define ER_FS_BADMAGIC     (-520)  /* Unrecognized volume superblock magic */
#define ER_FS_CORRUPT      (-521)  /* Inconsistent FID table, header, or record index */
#define ER_FS_FRAG         (-522)  /* Free blocks exist but no contiguous extent fits */
#define ER_FS_NOFID        (-523)  /* Volume FID table full (nfmax reached) */
#define ER_FS_COPY_TRUNC   (-524)  /* Short read/write during multi-block copy transfer */
#define ER_FS_LINK_CYCLE   (-525)  /* VOBJ directory cycle detected during tree walk */

/* ── Installer & Replication Pipeline Error Codes (-540 .. -559) ── */
#define ER_INST_NOSRC      (-540)  /* No valid mounted source volume selected */
#define ER_INST_NODST      (-541)  /* No valid destination disk/partition selected */
#define ER_INST_SAMEDEV    (-542)  /* Source and destination resolve to the same volume */
#define ER_INST_TOOSMALL   (-543)  /* Destination partition smaller than copy payload */
#define ER_INST_PLANFULL   (-544)  /* Source tree exceeds static copy plan capacity */
#define ER_INST_CANCELLED  (-545)  /* Replication cancelled cleanly by user */
#define ER_INST_BOOTFLAG   (-546)  /* Failed to commit Ski/MBR active boot flag or ESP */

/* ── Zero-Allocation Error Code Diagnostic Helpers ──────────────── */
static inline const char *btron_err_name(ER err) {
    switch (err) {
        case E_OK:              return "E_OK";
        case E_SYS:             return "E_SYS";
        case E_NOMEM:           return "E_NOMEM";
        case E_NOSPT:           return "E_NOSPT";
        case E_PAR:             return "E_PAR";
        case E_LIMIT:           return "E_LIMIT";
        case E_ID:              return "E_ID";
        case E_OBJ:             return "E_OBJ";
        case E_NOEXS:           return "E_NOEXS";
        case E_BUSY:            return "E_BUSY";
        case E_TMOUT:           return "E_TMOUT";
        case ER_ADR:            return "ER_ADR";
        case ER_NOSPC:          return "ER_NOSPC";
        case ER_ACCES:          return "ER_ACCES";
        case ER_IO:             return "ER_IO";
        case ER_DID:            return "ER_DID";
        case ER_ROVR:           return "ER_ROVR";
        case ER_NOFS:           return "ER_NOFS";
        case ER_NODSK:          return "ER_NODSK";
        case ER_RONLY:          return "ER_RONLY";
        case ER_FNAME:          return "ER_FNAME";
        case ER_EXS:            return "ER_EXS";
        case ER_PERM:           return "ER_PERM";
        case ER_OVRW:           return "ER_OVRW";
        case ER_PART_NOMBR:     return "ER_PART_NOMBR";
        case ER_PART_BADGPT:    return "ER_PART_BADGPT";
        case ER_PART_NOTFOUND:  return "ER_PART_NOTFOUND";
        case ER_PART_POOLFULL:  return "ER_PART_POOLFULL";
        case ER_PART_READONLY:  return "ER_PART_READONLY";
        case ER_PART_OVERLAP:   return "ER_PART_OVERLAP";
        case ER_PART_SLICE_DIS: return "ER_PART_SLICE_DIS";
        case ER_FS_BADMAGIC:    return "ER_FS_BADMAGIC";
        case ER_FS_CORRUPT:     return "ER_FS_CORRUPT";
        case ER_FS_FRAG:        return "ER_FS_FRAG";
        case ER_FS_NOFID:       return "ER_FS_NOFID";
        case ER_FS_COPY_TRUNC:  return "ER_FS_COPY_TRUNC";
        case ER_FS_LINK_CYCLE:  return "ER_FS_LINK_CYCLE";
        case ER_INST_NOSRC:     return "ER_INST_NOSRC";
        case ER_INST_NODST:     return "ER_INST_NODST";
        case ER_INST_SAMEDEV:   return "ER_INST_SAMEDEV";
        case ER_INST_TOOSMALL:  return "ER_INST_TOOSMALL";
        case ER_INST_PLANFULL:  return "ER_INST_PLANFULL";
        case ER_INST_CANCELLED: return "ER_INST_CANCELLED";
        case ER_INST_BOOTFLAG:  return "ER_INST_BOOTFLAG";
        default:                return "ER_UNKNOWN";
    }
}

static inline const char *btron_err_str(ER err) {
    switch (err) {
        case E_OK:              return "OK";
        case E_PAR:             return "Invalid parameter";
        case E_LIMIT:           return "Resource limit exceeded";
        case E_NOEXS:           return "Object does not exist";
        case E_NOSPT:           return "Feature not supported";
        case E_BUSY:            return "Resource busy";
        case ER_IO:             return "Block I/O error";
        case ER_NOFS:           return "Filesystem not mounted";
        case ER_NODSK:          return "No free disk blocks";
        case ER_NOSPC:          return "Insufficient space";
        case ER_RONLY:          return "Medium is read-only";
        case ER_FNAME:          return "Invalid file or path name";
        case ER_EXS:            return "File already exists";
        case ER_PERM:           return "Permission denied";
        case ER_PART_NOMBR:     return "Missing MBR 0x55AA signature";
        case ER_PART_BADGPT:    return "Invalid GPT header";
        case ER_PART_NOTFOUND:  return "No matching BTRON partition";
        case ER_PART_POOLFULL:  return "Partition descriptor pool full";
        case ER_PART_READONLY:  return "Platform storage uses fixed/SD partition layout";
        case ER_PART_OVERLAP:   return "Partition range overlaps existing slice or ESP";
        case ER_PART_SLICE_DIS: return "Direct .vol slice boot disabled (use .img disk or slice_boot=1)";
        case ER_FS_BADMAGIC:    return "Unrecognized BTRON volume magic";
        case ER_FS_CORRUPT:     return "Corrupt filesystem metadata";
        case ER_FS_FRAG:        return "No contiguous free block extent available";
        case ER_FS_NOFID:       return "Volume FID table full";
        case ER_FS_COPY_TRUNC:  return "Short read/write during file copy";
        case ER_FS_LINK_CYCLE:  return "Directory link cycle detected";
        case ER_INST_NOSRC:     return "No valid source volume selected";
        case ER_INST_NODST:     return "No valid target partition selected";
        case ER_INST_SAMEDEV:   return "Source and target cannot be the same volume";
        case ER_INST_TOOSMALL:  return "Target partition too small for selected folders";
        case ER_INST_PLANFULL:  return "Source volume exceeds installer copy manifest limit";
        case ER_INST_CANCELLED: return "Installation cancelled by user";
        case ER_INST_BOOTFLAG:  return "Failed to write Ski/MBR boot flag";
        default:                return "System error";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_ERROR_H_ */
