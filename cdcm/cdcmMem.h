/**
 * @file cdcmMem.h
 *
 * @brief All CDCM memory handling definitions are located here.
 *
 * @author Georgievskiy Yury, Alain Gagnaire. CERN AB/CO.
 *
 * @date Created on 17/02/2007
 *
 * Many thanks to Julian Lewis and Nicolas de Metz-Noblat.
 *
 * @version $Id: cdcmMem.h,v 1.4 2009/01/09 10:26:03 ygeorgie Exp $
 */
#ifndef _CDCM_MEM_H_INCLUDE_
#define _CDCM_MEM_H_INCLUDE_

#define CDCM_ALLOC_DEF 16     //!< how many mem block allocated at one shot

/* 
   next are memory flags.
   NOTE: that memory allocation method depends on requested mem size.
         If size is less then 128Kb - than kmalloc is used.
	 If size is bigger - than vmalloc is used.
*/
#define CDCM_M_FLG_SHORT_TERM (1<<3) //!< short term memory
#define CDCM_M_FLG_LONG_TERM  (1<<4) //!< long term memory
#define CDCM_M_FLG_IOCTL      (1<<5) //!< claimed by ioctl call
#define CDCM_M_FLG_READ       (1<<6) //!< claimed by read  call
#define CDCM_M_FLG_WRITE      (1<<7) //!< claimed by write call
#define CDCM_M_FLG_APP        (1<<8) //!< claimed by the user driver

typedef struct _cdcmMem {
  struct list_head cm_list;     /* linked list */
  char   *cmPtr; /* pointer to allocated memory */
  size_t  cmSz;	 /* allocated size in bytes */
  int     cmFlg; /* memory flags. See CDCM_M_FLG_xxx definitions.
		    Also _IOC_NONE (1U),  _IOC_WRITE (2U) and  _IOC_READ (4U)
		    are set as a flags */
} cdcmm_t;

int      cdcm_mem_init(void);
int      cdcm_mem_free(char*);
int      cdcm_mem_cleanup_all(void);
char*    cdcm_mem_alloc(size_t, int);
cdcmm_t* cdcm_mem_find_block(char*);

#endif /* _CDCM_MEM_H_INCLUDE_ */