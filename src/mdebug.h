#ifndef __RECOMP_MDEBUG_H__
#define __RECOMP_MDEBUG_H__

#include <cassert>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>

#include "recompiler/context.h"

namespace N64Recomp
{
    namespace MDebug {

        #ifdef _MSC_VER
        inline uint32_t bswap32(uint32_t val) {
            return _byteswap_ulong(val);
        }
        inline uint16_t bswap16(uint16_t val) {
            return _byteswap_ushort(val);
        }
        #else
        constexpr uint32_t bswap32(uint32_t val) {
            return __builtin_bswap32(val);
        }
        constexpr uint16_t bswap16(uint16_t val) {
            return __builtin_bswap16(val);
        }
        #endif

        /* MIPS Symbol Table Debugging Format */

        enum SC {
            SC_NIL         =  0,
            SC_TEXT        =  1, /* .text symbol */
            SC_DATA        =  2, /* .data symbol */
            SC_BSS         =  3, /* .bss symbol */
            SC_REGISTER    =  4, /* value of symbol is register number */
            SC_ABS         =  5, /* value of symbol is absolute */
            SC_UNDEFINED   =  6, /* value of symbol is undefined */
            SC_CDBLOCAL    =  7, /* variable value is in se->va.?? */
            SC_BITS        =  8, /* variable is a bit field */
            SC_CDBSYSTEM   =  9, /* variable value is in cdb address space */
            SC_REGIMAGE    = 10, /* register value is saved on stack */
            SC_INFO        = 11, /* symbol contains debugger information */
            SC_USERSTRUCT  = 12, /* address in struct user for current process */
            SC_SDATA       = 13, /* load time only small data */
            SC_SBSS        = 14, /* load time only small common */
            SC_RDATA       = 15, /* load time only read-only data */
            SC_VAR         = 16, /* var parameter (FORTRAN, Pascal) */
            SC_COMMON      = 17, /* common variable */
            SC_SCOMMON     = 18, /* small common */
            SC_VARREGISTER = 19, /* var parameter in a register */
            SC_VARIANT     = 20, /* variant record */
            SC_SUNDEFINED  = 21, /* small undefined (external) data */
            SC_INIT        = 22, /* .init section symbol */
            SC_BASEDVAR    = 23, /* FORTRAN or PL/1 ptr based var */
            SC_XDATA       = 24, /* exception handling data */
            SC_PDATA       = 25, /* procedure section */
            SC_FINI        = 26, /* .fini section */
            SC_RCONST      = 27, /* .rconst section */
            SC_MAX         = 32
        };

        enum ST {
            ST_NIL        =  0,
            ST_GLOBAL     =  1, /* external symbol */
            ST_STATIC     =  2, /* static symbol */
            ST_PARAM      =  3, /* procedure argument */
            ST_LOCAL      =  4, /* local variable */
            ST_LABEL      =  5, /* label */
            ST_PROC       =  6, /* procedure */
            ST_BLOCK      =  7, /* beginning of block */
            ST_END        =  8, /* end of something */
            ST_MEMBER     =  9, /* member of struct/union/enum/.. */
            ST_TYPEDEF    = 10, /* type definition */
            ST_FILE       = 11, /* filename */
            ST_REGRELOC   = 12, /* register relocation */
            ST_FORWARD    = 13, /* forwarding address */
            ST_STATICPROC = 14, /* load time only static procedures */
            /* (CONSTANT and STAPARAM are in different orders between different sources...) */
            ST_CONSTANT   = 15, /* constant */
            ST_STAPARAM   = 16, /* FORTRAN static parameters */
            ST_STRUCT     = 26, /* structure */
            ST_UNION      = 27, /* union */
            ST_ENUM       = 28, /* enum */
            ST_INDIRECT   = 34
        };

        struct SYMR {
            int32_t iss;    /* index into String Space of name */
            int32_t value;  /* symbol value; can be an address, size or frame offset depending on symbol type */
            uint32_t bits;  /* Bitfield: */
        #if 0
            ST      st        : 6;   /* symbol type */
            SC      sc        : 5;   /* storage class - text, data, etc */
            int32_t _reserved : 1;   /* reserved bit */
            int32_t index     : 20;  /* index into sym/aux table */
        #endif

            inline ST get_st() const {
                return (ST)(bits >> 26 & 0b111111);
            }

            inline SC get_sc() const {
                return (SC)(bits >> 21 & 0b11111);
            }

            inline int32_t get_index() const {
                return bits & 0b11111111111111111111;
            }

            void swap() {
                iss   = bswap32(iss);
                value = bswap32(value);
                bits  = bswap32(bits);
            }
        };

        union AUX {
            uint32_t any_;
            uint32_t ti;      /* type information record */
            uint32_t rndx;    /* relative index into symbol table */
            uint32_t dnLow;   /* low dimension of array */
            uint32_t dnHigh;  /* high dimension of array */
            uint32_t isym;    /* symbol table index (end of proc) */
            uint32_t iss;     /* index into string space (not used) */
            uint32_t width;   /* width for non-default sized struct fields */
            uint32_t count;   /* count of ranges for variant arm */

            void swap() {
                any_ = bswap32(any_);
            }
        };

        struct PDR {
            uint32_t addr;          /* memory address of start of procedure */
            uint32_t isym;          /* start of local symbol entries */
            uint32_t iline;         /* start of line number entries */
            uint32_t regmask;       /* save register mask */
            uint32_t regoffset;     /* save register offset */
            uint32_t iopt;          /* start of optimization symbol entries */
            uint32_t fregmask;      /* save floating point register mask */
            uint32_t fregoffset;    /* save floating point register offset */
            uint32_t frameoffset;   /* frame size */
            uint16_t framereg;      /* frame pointer register */
            uint16_t pcreg;         /* offset or reg of return pc */
            int32_t  lnLow;         /* lowest line in the procedure */
            int32_t  lnHigh;        /* highest line in the procedure */
            int32_t  cbLineOffset;  /* byte offset for this procedure from the fd base */

            void swap() {
                addr         = bswap32(addr);
                isym         = bswap32(isym);
                iline        = bswap32(iline);
                regmask      = bswap32(regmask);
                regoffset    = bswap32(regoffset);
                iopt         = bswap32(iopt);
                fregmask     = bswap32(fregmask);
                fregoffset   = bswap32(fregoffset);
                frameoffset  = bswap32(frameoffset);
                framereg     = bswap16(framereg);
                pcreg        = bswap16(pcreg);
                lnLow        = bswap32(lnLow);
                lnHigh       = bswap32(lnHigh);
                cbLineOffset = bswap32(cbLineOffset);
            }

            inline std::pair<uint32_t,uint32_t> sym_bounds(std::span<const SYMR> symrs, std::span<const AUX> auxs) const {
                const SYMR& first = symrs[isym];
                const ST first_st = first.get_st();
                // The first symbol is the symbol of the procedure itself. The procedure name is the name of this symbol.
                assert(first_st == ST_PROC || first_st == ST_STATICPROC);

                const AUX& aux = auxs[first.get_index()];
                const SYMR& last = symrs[aux.isym - 1];
                const ST last_st = last.get_st();
                // The last symbol is the END marker, pointed to by the first AUX of the stPROC symbol.
                assert(last_st == ST_END);

                // Return the symbol bounds
                return std::make_pair(isym, aux.isym);
            }
        };

        enum LANG {
            LANG_C           = 0,
            LANG_PASCAL      = 1,
            LANG_FORTRAN     = 2,
            LANG_ASM         = 3,
            LANG_MACHINE     = 4,
            LANG_NIL         = 5, 
            LANG_ADA         = 6,
            LANG_PL1         = 7,
            LANG_COBOL       = 8,
            LANG_STDC        = 9,
            LANG_CPLUSPLUSV2 = 10,
            LANG_MAX         = 11
        };

        struct FDR {
            uint32_t adr;              /* memory address of beginning of file */
            int32_t  rss;              /* file name (of source, if known) */
            int32_t  issBase;          /* file's string space */
            int32_t  cbSs;             /* number of bytes in the ss */
            int32_t  isymBase;         /* beginning of symbols */
            int32_t  csym;             /* count file's of symbols */
            int32_t  ilineBase;        /* file's line symbols */
            int32_t  cline;            /* count of file's line symbols */
            int32_t  ioptBase;         /* file's optimization entries */
            int32_t  copt;             /* count of file's optimization entries */
            uint16_t ipdFirst;         /* start of procedures for this file */
            uint16_t cpd;              /* count of procedures for this file */
            int32_t  iauxBase;         /* file's auxiliary entries */
            int32_t  caux;             /* count of file's auxiliary entries */
            int32_t  rfdBase;          /* index into the file indirect table */
            int32_t  crfd;             /* count file indirect entries */
            uint32_t bits;             /* Bitfield: */
        #if 0
            LANG     lang       : 5;   /* language for this file */
            uint32_t fMerge     : 1;   /* whether this file can be merged */
            uint32_t fReadin    : 1;   /* true if it was read in (not just created) */
            uint32_t fBigEndian : 1;   /* true if AUXU's are big endian */
            uint32_t glevel     : 2;   /* level this file was compiled with */
            uint32_t _reserved  : 20;  /* reserved bits */
        #endif
            int32_t  cbLineOffset;     /* byte offset from header for this file ln's */
            int32_t  cbLine;           /* size of lines for this file */

            inline LANG get_lang() const {
                return (LANG)(bits >> 27 & 0b11111);
            }

            inline uint32_t get_fMerge() const {
                return bits >> 26 & 1;
            }

            inline uint32_t get_fReadin() const {
                return bits >> 25 & 1;
            }

            inline uint32_t get_fBigEndian() const {
                return bits >> 24 & 1;
            }

            inline uint32_t get_glevel() const {
                return bits >> 22 & 0b11;
            }

            void swap() {
                adr          = bswap32(adr);
                rss          = bswap32(rss);
                issBase      = bswap32(issBase);
                cbSs         = bswap32(cbSs);
                isymBase     = bswap32(isymBase);
                csym         = bswap32(csym);
                ilineBase    = bswap32(ilineBase);
                cline        = bswap32(cline);
                ioptBase     = bswap32(ioptBase);
                copt         = bswap32(copt);
                ipdFirst     = bswap16(ipdFirst);
                cpd          = bswap16(cpd);
                iauxBase     = bswap32(iauxBase);
                caux         = bswap32(caux);
                rfdBase      = bswap32(rfdBase);
                crfd         = bswap32(crfd);
                bits         = bswap32(bits);
                cbLineOffset = bswap32(cbLineOffset);
                cbLine       = bswap32(cbLine);
            }

            inline std::span<const AUX> get_auxs(const std::vector<AUX>& all_auxs) const {
                return std::span(all_auxs).subspan(iauxBase, caux);
            }

            inline std::span<const PDR> get_pdrs(const std::vector<PDR>& all_pdrs) const {
                return std::span(all_pdrs).subspan(ipdFirst, cpd);
            }

            inline std::span<const SYMR> get_symrs(std::span<const SYMR> all_symrs) const {
                return std::span(all_symrs).subspan(isymBase, csym);
            }

            inline const char* get_string(const char* data, size_t index) const {
                return data + issBase + index;
            }

            inline const char* get_name(const char* data) const {
                return get_string(data, rss);
            }
        };

        static const uint16_t MAGIC = 0x7009;

        /**
         * mdebug sections always start with a Symbolic Header (HDRR) containing
         * file-relative (not section-relative) offsets for where to find the rest
         * of the data.
         */
        struct HDRR {
            uint16_t magic;          /* 0x7009 */
            uint16_t vstamp;         /* version stamp */
            int32_t  ilineMax;       /* number of line number entries */
            int32_t  cbLine;         /* number of bytes for line number entries */
            int32_t  cbLineOffset;   /* offset to start of line number entries */
            int32_t  idnMax;         /* max index into dense number table */
            int32_t  cbDnOffset;     /* offset to start dense number table */
            int32_t  ipdMax;         /* number of procedures */
            int32_t  cbPdOffset;     /* offset to procedure descriptor table */
            int32_t  isymMax;        /* number of local symbols */
            int32_t  cbSymOffset;    /* offset to start of local symbols */
            int32_t  ioptMax;        /* max index into optimization symbol entries */
            int32_t  cbOptOffset;    /* offset to optimization symbol entries */
            int32_t  iauxMax;        /* number of auxillary symbol entries */
            int32_t  cbAuxOffset;    /* offset to start of auxillary symbol entries */
            int32_t  issMax;         /* max index into local strings */
            int32_t  cbSsOffset;     /* offset to start of local strings */
            int32_t  issExtMax;      /* max index into external strings */
            int32_t  cbSsExtOffset;  /* offset to start of external strings */
            int32_t  ifdMax;         /* number of file descriptor entries */
            int32_t  cbFdOffset;     /* offset to file descriptor table */
            int32_t  crfd;           /* number of relative file descriptor entries */
            int32_t  cbRfdOffset;    /* offset to relative file descriptor table */
            int32_t  iextMax;        /* max index into external symbols */
            int32_t  cbExtOffset;    /* offset to start of external symbol entries */

            void swap() {
                magic         = bswap16(magic);
                vstamp        = bswap16(vstamp);
                ilineMax      = bswap32(ilineMax);
                cbLine        = bswap32(cbLine);
                cbLineOffset  = bswap32(cbLineOffset);
                idnMax        = bswap32(idnMax);
                cbDnOffset    = bswap32(cbDnOffset);
                ipdMax        = bswap32(ipdMax);
                cbPdOffset    = bswap32(cbPdOffset);
                isymMax       = bswap32(isymMax);
                cbSymOffset   = bswap32(cbSymOffset);
                ioptMax       = bswap32(ioptMax);
                cbOptOffset   = bswap32(cbOptOffset);
                iauxMax       = bswap32(iauxMax);
                cbAuxOffset   = bswap32(cbAuxOffset);
                issMax        = bswap32(issMax);
                cbSsOffset    = bswap32(cbSsOffset);
                issExtMax     = bswap32(issExtMax);
                cbSsExtOffset = bswap32(cbSsExtOffset);
                ifdMax        = bswap32(ifdMax);
                cbFdOffset    = bswap32(cbFdOffset);
                crfd          = bswap32(crfd);
                cbRfdOffset   = bswap32(cbRfdOffset);
                iextMax       = bswap32(iextMax);
                cbExtOffset   = bswap32(cbExtOffset);
            }

            void relocate(uint32_t offset) {
                cbLineOffset  -= offset;
                cbDnOffset    -= offset;
                cbPdOffset    -= offset;
                cbSymOffset   -= offset;
                cbOptOffset   -= offset;
                cbAuxOffset   -= offset;
                cbSsOffset    -= offset;
                cbSsExtOffset -= offset;
                cbFdOffset    -= offset;
                cbRfdOffset   -= offset;
                cbExtOffset   -= offset;
            }

            inline std::vector<FDR> read_fdrs(const char* data) {
                std::vector<FDR> fdrs(ifdMax);
                const FDR* p = reinterpret_cast<const FDR*>(data + cbFdOffset);
                fdrs.assign(p, p + ifdMax);
                for (FDR& fdr : fdrs)
                    fdr.swap();
                return fdrs;
            }

            inline std::vector<AUX> read_auxs(const char* data) {
                std::vector<AUX> auxs(iauxMax);
                const AUX* p = reinterpret_cast<const AUX*>(data + cbAuxOffset);
                auxs.assign(p, p + iauxMax);
                for (AUX& aux : auxs)
                    aux.swap();
                return auxs;
            }

            inline std::vector<PDR> read_pdrs(const char* data) {
                std::vector<PDR> pdrs(ipdMax);
                const PDR* p = reinterpret_cast<const PDR*>(data + cbPdOffset);
                pdrs.assign(p, p + ipdMax);
                for (PDR& pdr : pdrs)
                    pdr.swap();
                return pdrs;
            }

            inline std::vector<SYMR> read_symrs(const char* data) {
                std::vector<SYMR> symrs(isymMax);
                const SYMR* p = reinterpret_cast<const SYMR*>(data + cbSymOffset);
                symrs.assign(p, p + isymMax);
                for (SYMR& symr : symrs)
                    symr.swap();
                return symrs;
            }

        };
        bool parse_mdebug(const N64Recomp::ElfParsingConfig& elf_config, const char* mdebug_section, uint32_t mdebug_offset, N64Recomp::Context& context, N64Recomp::DataSymbolMap& data_syms);
    }
}

#endif
