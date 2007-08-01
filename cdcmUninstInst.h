/* $Id: cdcmUninstInst.h,v 1.2 2007/08/01 15:07:21 ygeorgie Exp $ */
/*
; Module Name:	cdcmUninstInst.h
; Module Descr:	All concerning driver installation and uninstallation is
;		located here.
; Date:         June, 2007.
; Author:       Georgievskiy Yury, Alain Gagnaire. CERN AB/CO.
;
;
; -----------------------------------------------------------------------------
; Revisions of cdcmUninstInst.h: (latest revision on top)
;
; #.#   Name       Date       Description
; ---   --------   --------   -------------------------------------------------
; 3.0   ygeorgie   01/08/07   Full Lynx-like installation behaviour.
; 2.0   ygeorgie   09/07/07   Production release, CVS controlled.
; 1.0	ygeorgie   28/06/07   Initial version.
*/
#ifndef _CDCM_UNINST_INST_H_INCLUDE_
#define _CDCM_UNINST_INST_H_INCLUDE_

#include <elf.h> /* for endianity business */
//#ifdef __linux__
#include <stdarg.h>
#include <stdio.h>
//#else  /* __Lynx__ */
//#include <print.h>
//#endif

/* swap bytes */
static inline void __endian(const void *src, void *dest, unsigned int size)
{
  unsigned int i;
  for (i = 0; i < size; i++)
    ((unsigned char*)dest)[i] = ((unsigned char*)src)[size - i - 1];
}

/* find out current endianity */
static inline int __my_endian()
{
  static int my_endian = ELFDATANONE;
  union { short s; char c[2]; } endian_test;
  
  if (my_endian != ELFDATANONE)	/* already defined */
    return(my_endian);
  
  endian_test.s = 1;
  if (endian_test.c[1] == 1) my_endian = ELFDATA2MSB;
  else if (endian_test.c[0] == 1) my_endian = ELFDATA2LSB;
  else abort();

  return(my_endian);
}

/* assert data to Big Endian */
#define ASSERT_MSB(x)				\
({						\
  typeof(x) __x;				\
  typeof(x) __val = (x);			\
  if (__my_endian() == ELFDATA2LSB)		\
    __endian(&(__val), &(__x), sizeof(__x));	\
  else						\
    __x = x;					\
  __x;						\
})

static inline void mperr(char *token, ...)
{
  char errstr[256];
  va_list ap;
  va_start(ap, token);
  vsnprintf(errstr, sizeof(errstr),  token, ap);
  va_end(ap);
  perror(errstr);
}

/* predefined option characters that will be parsed by default. They should
   not be used by the module specific part to avoid collision */
typedef enum _tagDefaultOptionsChars {
  P_T_GRP     = 'G',	/* group designator */
  P_T_LUN     = 'U',	/* Logical Unit Number */
  P_T_ADDR    = 'O',	/* first base address */
  P_T_N_ADDR  = 'M',	/* second base address */
  P_T_ILEV    = 'L',	/* interrupt level */
  P_T_IVECT   = 'V',	/* interrupt vector */
  P_T_CHAN    = 'C',	/* channel amount for the module */
  P_T_FLG     = 'I',	/* driver flag */
  P_T_TRANSP  = 'T',	/* driver transparent parameter */
  P_T_HELP    = 'h',	/* help */
  P_T_SEPAR   = ',',	/* separator between module entries */

  P_ST_ADDR   = ' ',	/* slave first base address */
  P_ST_N_ADDR =	' '	/* slave second base address */
} def_opt_t;

/* vectors that will be called (if not NULL) by the module-specific install
   programm to handle specific part of the module installation procedure */
struct module_spec_inst_ops {
  void  (*mso_educate)();       /* for user education */
  char* (*mso_get_optstr)();	/* module-spesific options in 'getopt' form */
  int   (*mso_parse_opt)(char, char*); /* parse module-spesific option */
  char* (*mso_get_mod_name)(); /* module-specific name. COMPULSORY vector */
};

extern struct module_spec_inst_ops cdcm_inst_ops;

#ifdef __linux__
#define BLOCKDRIVER	1
#define CHARDRIVER	0
int dr_install(char*, int);
//int dr_uninstall(int);
int cdv_install(char*, int, int);
//ind cdv_uninstall(int);
#else  /* __Lynx__ */
static inline int makedev(int major, int minor)
{
  int dev_num;
  dev_num = (major << 8) + minor;
  return(dev_num);
}
#endif /* __Lynx__ */

struct list_head* cdcm_vme_arg_parser(int, char *[], char *[]);

#endif /* _CDCM_UNINST_INST_H_INCLUDE_ */
