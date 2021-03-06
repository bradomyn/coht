/**
 * @file libxmem.c
 *
 * @brief XMEM Reflective Memory Library Implementation
 *
 * @author Julian Lewis
 *
 * @date Created on 09/02/2005
 *
 * @version 1.1 Emilio G. Cota 16/01/2009
 *
 * @version 1.0 Julian Lewis
 */
#include <unistd.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <mqueue.h>
#include <stdlib.h>
#include <ctype.h>
#include <time.h>
#include <sys/shm.h>

#include <xmemDrvr.h>
#include <libxmem.h>
#include <adler32.h>

/*! @name device specific backend code
 *
 */
//@{
#define LN 128 //!< length of string (e.g. for full filenames)

static void		*attach_address[XmemMAX_TABLES];

static XmemDrvrSegTable seg_tab;
static int 		segcnt = 0;

static XmemDrvrNodeTable node_tab;
static int 		nodecnt = 0;

static XmemNodeId 	my_nid = 0;	//!< My node ID
static int        	warm_start = 0; //!< Warm vs Cold start
/* see description in function XmemCheckForWarmStart for further info on this */

static XmemMarkersMask	markers_mask = XmemMarkersDISABLE;
//@}

static char gbConfigPath[LN] = "";

/**
 * @brief Set the default path for initialisation files
 *
 * @param pbPath - Path where the configuration files are stored
 *
 * @return XmemError
 */
XmemError XmemSetPath(char *pbPath)
{
	if (pbPath == NULL)
		return XmemErrorSUCCESS;

	if (strlen(pbPath) < (LN - 20)) {
		strcpy(gbConfigPath, pbPath);
		if (gbConfigPath[strlen(gbConfigPath) - 1] != '/')
			strcat(gbConfigPath, "/");
	}

	return XmemErrorSUCCESS;
}

/**
 * @brief Form the full path of a given configuration file
 *
 * @param name - file name
 *
 * @return full path of the given file name
 */
char *XmemGetFile(char *name)
{
	static char path[LN];
	char        *configpath;

	configpath = strlen(gbConfigPath) > 0 ? gbConfigPath : XMEM_PATH;
	sprintf(path, "%s%s", configpath, name);
	return path;
}

/*! @name Static variables, constants and functions
 *
 * These are not exported to the users of this library.
 */
//@{
#include "./vmic/VmicLib.c"
#include "./network/NetworkLib.c"
#include "./shmem/ShmemLib.c"

typedef struct {
	XmemNodeId    (*GetAllNodeIds)();
	XmemError     (*RegisterCallback)();
	XmemEventMask (*Wait)();
	XmemEventMask (*Poll)();
	XmemError     (*SendTable)();
	XmemError     (*RecvTable)();
	XmemError     (*SendMessage)();
	XmemTableId   (*CheckTables)();
	XmemError     (*SendSoftWakeup)();
} XmemLibRoutines;

static int 		libinitialized = 0;
static XmemEventMask 	libcallmask = 0;
static XmemLibRoutines 	routines;
static void (*libcallback)(XmemCallbackStruct *cbs) = NULL;

static char *estr[XmemErrorCOUNT] = {
	"No Error",
	"Timeout expired while waiting for interrupt",
	"The Xmem library is not initialized",
	"Write access to that table is denied for this node",
	"Could not read table descriptors from file: " SEG_TABLE_NAME,
	"Syntax error in table descriptors file: " SEG_TABLE_NAME,
	"Could not read node descriptors from file: " NODE_TABLE_NAME,
	"Syntax error in node descriptors file: " NODE_TABLE_NAME,
	"There are currently no tables defined",
	"That table is not defined in: " SEG_TABLE_NAME,
	"That node is not defined in: " NODE_TABLE_NAME,
	"Illegal message ID",
	"A run time hardware IO error has occured, see: IOError",
	"System error, see: errno",
	"Incoherent markers: header/footer mismatch",
	"Not enough memory",
	"Checksum error"
};

/*
 * Marker's (header/footer) implementation
 *
 * Tables may or may not be wrapped by markers (header/footer).
 * This is enabled via a configurable parameter and has to be transparent
 * to the users of the library.
 * If markers are enabled, the resulting available space on a given
 * table is smaller (but not by much, check the structs below).
 * To take account of this size/offset mangling, we refer to the real (i.e.
 * physical XMEM addresses) as private (priv), and the addresses that users
 * operate with as public (pub).
 * NOTE: The simple markers implementation is not atomic; use the flag
 * XmemMarkersATOMIC if you really need to ensure atomicity, at the price of
 * using a bounce buffer for each access.
 */

struct header {
	uint32_t	val;
	uint32_t	checksum;
	uint32_t	size;
} __attribute__((__packed__));

struct footer {
	uint32_t	val;
};

#define XMEM_H_SIZE	sizeof(struct header)
#define XMEM_F_SIZE	sizeof(struct footer)
#define XMEM_HF_SIZE	((XMEM_H_SIZE)+(XMEM_F_SIZE))

#define XMEM_H_ELEMS	((XMEM_H_SIZE)/sizeof(uint32_t))
#define XMEM_F_ELEMS	((XMEM_F_SIZE)/sizeof(uint32_t))
#define XMEM_HF_ELEMS	((XMEM_HF_SIZE)/sizeof(uint32_t))

/*
 * header's element offset
 * Note. We call 'element offset' an offset whose unit is 4 bytes.
 */
static int __h_eloff(int pub_eloff)
{
	return pub_eloff;
}

/* physical's element offset of a given address */
static int phys_eloff(int pub_eloff)
{
	if (markers_mask & XmemMarkersENABLE)
		return pub_eloff + XMEM_H_ELEMS;
	return pub_eloff;
}

/* footer's offset */
static int __f_eloff(int pub_elems, int pub_eloff)
{
	return phys_eloff(pub_eloff) + pub_elems;
}

static int priv_to_pub_elems(int priv_elems)
{
	if (markers_mask & XmemMarkersENABLE)
		return priv_elems - XMEM_HF_ELEMS;
	return priv_elems;
}

/**
 * XmemReadNodeTableFile - Reads the node table
 *
 * @param : none
 *
 * The node table is in the default place (NODE_TABLE_NAME).
 *
 * @return Appropriate error code (XmemError)
 */
static XmemError XmemReadNodeTableFile()
{
	int 	i;
	FILE 	*fp;
	char	c;

	nodecnt = 0;
	umask(0);
	fp = fopen(XmemGetFile(NODE_TABLE_NAME), "r");
	if (NULL == fp)
		return XmemErrorCallback(XmemErrorNODE_TABLE_READ, 0);

	node_tab.Used = 0;
	for (i = 0; i < XmemDrvrNODES;) {
		if (fscanf(fp, "{ %s 0x%lx }\n", node_tab.Descriptors[i].Name,
				&node_tab.Descriptors[i].Id) == 2) {
			node_tab.Used |= node_tab.Descriptors[i].Id;
			nodecnt++;
			i++;
		} else if (fscanf(fp, "%c", &c) == 1) {
			/*
			 * Empty lines, lines with only blank characters and
			 * lines starting with a hash (#) are ignored.
			 */
			if (c == '\n') {
				continue;
			} else if (c == '#') {
				while (fscanf(fp, "%c", &c) != EOF) {
					if (c == '\n')
						break;
				}
			} else if (isspace(c)) {
				while (fscanf(fp, "%c", &c) != EOF) {
					if (c == '\n')
						break;
					if (!isspace(c))
						goto out_err;
				}
			} else {
				goto out_err;
			}
		} else {
			goto out;
		}
	}
out:
	return XmemErrorSUCCESS;
out_err:
	return XmemErrorCallback(XmemErrorNODE_TABLE_SYNTAX, 0);
}


/**
 * XmemReadSegTableFile - Reads the segment table
 *
 * @param : none
 *
 * The segment table is in the default place (SEG_TABLE_NAME).
 *
 * @return Appropriate error code (XmemError)
 */
static XmemError XmemReadSegTableFile()
{
	int 	i;
	FILE 	*fp;
	char	c;

	segcnt = 0;
	umask(0);
	fp = fopen(XmemGetFile(SEG_TABLE_NAME), "r");
	if (NULL == fp)
		return XmemErrorCallback(XmemErrorSEG_TABLE_READ, 0);
	seg_tab.Used = 0;
	for (i = 0; i < XmemDrvrSEGMENTS;) {
		if (fscanf(fp, "{ %s 0x%lx 0x%lx 0x%x 0x%lx 0x%lx }\n",
				seg_tab.Descriptors[i].Name,
				&seg_tab.Descriptors[i].Id,
				&seg_tab.Descriptors[i].Size,
				(unsigned int *)&seg_tab.Descriptors[i].Address,
				&seg_tab.Descriptors[i].Nodes,
				&seg_tab.Descriptors[i].User) == 6) {
			seg_tab.Used |= seg_tab.Descriptors[i].Id;
			segcnt++;
			i++;
		} else if (fscanf(fp, "%c", &c) == 1) {
			/*
			 * Empty lines, lines with only blank characters and
			 * lines starting with a hash (#) are ignored.
			 */
			if (c == '\n') {
				continue;
			} else if (c == '#') {
				while (fscanf(fp, "%c", &c) != EOF) {
					if (c == '\n')
						break;
				}
			} else if (isspace(c)) {
				while (fscanf(fp, "%c", &c) != EOF) {
					if (c == '\n')
						break;
					if (!isspace(c))
						goto out_err;
				}
			} else {
				goto out_err;
			}
		} else {
			goto out;
		}
	}
out:
	return XmemErrorSUCCESS;
out_err:
	return XmemErrorCallback(XmemErrorSEG_TABLE_SYNTAX, 0);
}


/**
 * InitDevice - Local routine to initialise one real device
 *
 * @param device: type of device
 *
 * Remember that device can be VMIC, SHMEM or NETWORK.
 *
 * @return device initialisation on success; Not initialised error otherwise.
 */
static XmemError InitDevice(XmemDevice device)
{
	switch (device) {

	case XmemDeviceVMIC:
		routines.GetAllNodeIds    = VmicGetAllNodeIds;
		routines.RegisterCallback = VmicRegisterCallback;
		routines.Wait             = VmicWait;
		routines.Poll             = VmicPoll;
		routines.SendTable        = VmicSendTable;
		routines.RecvTable        = VmicRecvTable;
		routines.SendMessage      = VmicSendMessage;
		routines.CheckTables      = VmicCheckTables;
		routines.SendSoftWakeup   = VmicSendSoftWakeup;
		return VmicInitialize();

	case XmemDeviceSHMEM:
		routines.GetAllNodeIds    = ShmemGetAllNodeIds;
		routines.RegisterCallback = ShmemRegisterCallback;
		routines.Wait             = ShmemWait;
		routines.Poll             = ShmemPoll;
		routines.SendTable        = ShmemSendTable;
		routines.RecvTable        = ShmemRecvTable;
		routines.SendMessage      = ShmemSendMessage;
		routines.CheckTables      = ShmemCheckTables;
		routines.SendSoftWakeup   = ShmemSendSoftWakeup;
		return ShmemInitialize();

	case XmemDeviceNETWORK:
		routines.GetAllNodeIds    = NetworkGetAllNodeIds;
		routines.RegisterCallback = NetworkRegisterCallback;
		routines.Wait             = NetworkWait;
		routines.Poll             = NetworkPoll;
		routines.SendTable        = NetworkSendTable;
		routines.RecvTable        = NetworkRecvTable;
		routines.SendMessage      = NetworkSendMessage;
		routines.CheckTables      = NetworkCheckTables;
		routines.SendSoftWakeup   = NetworkSendSoftWakeup;
		return NetworkInitialize();

	default:
		break;

	}
	return XmemErrorNOT_INITIALIZED;
}

static unsigned long calc_adler32(void *pub_buf, int pub_elems)
{
	unsigned long adler = zlib_adler32(0L, NULL, 0);

	return zlib_adler32(adler, pub_buf, pub_elems * sizeof(uint32_t));
}

static XmemError evaluate_hf(struct header *header, struct footer *footer,
			     int pub_elems, void *pub_buf)
{
	if (header->val != footer->val)
		return XmemErrorINCOHERENT_MARKERS;

	if (pub_buf != NULL && markers_mask & XmemMarkersCHECKSUM) {
		if (header->checksum != calc_adler32(pub_buf, pub_elems))
			return XmemErrorCHECKSUM;
	}
	return XmemErrorSUCCESS;
}

static XmemError check_markers(XmemTableId table, int pub_elems, int pub_eloff)
{
	struct header	header;
	struct footer	footer;
	XmemError	err;

	if (markers_mask & XmemMarkersDISABLE)
		return XmemErrorSUCCESS;

	/* read header */
	err = routines.RecvTable(table, &header, XMEM_H_ELEMS,
				__h_eloff(pub_eloff));
	if (err != XmemErrorSUCCESS)
		return err;

	/* read footer */
	err = routines.RecvTable(table, &footer, XMEM_F_ELEMS,
				__f_eloff(pub_elems, pub_eloff));
	if (err != XmemErrorSUCCESS)
		return err;

	/* check markers */
	err = evaluate_hf(&header, &footer, pub_elems, NULL);
	if (err != XmemErrorSUCCESS)
		return err;

	return XmemErrorSUCCESS;
}

static void fill_hf(struct header *header, struct footer *footer, int pub_elems,
		    void *pub_buf)
{
	uint32_t randval = rand();

	header->val = randval;
	header->size = pub_elems * sizeof(uint32_t);
	footer->val = randval;

	if (pub_buf != NULL && markers_mask & XmemMarkersCHECKSUM)
		header->checksum = calc_adler32(pub_buf, pub_elems);
}

/*
 * When XmemMarkersATOMIC is set, a bounce buffer is allocated when reading
 * any table. The table is copied atomically to the bounce buffer, and the
 * data coherency is evaluated on that local bounce buffer.
 * If the data are coherent, they're copied to the user's buffer from
 * the bounce buffer. We proceed in a similar fashion for writes.
 * We may want to implement this on a per-segment basis, but for the time
 * being as a per-process parameter seems enough for our needs.
 */
static XmemError send_table_atomic(XmemTableId table, void *buf, int pub_elems,
				   int pub_eloff, int upflag)
{
	size_t		priv_elems = pub_elems + XMEM_HF_ELEMS;
	uint32_t	*bounce = NULL;
	struct header	*header;
	struct footer	*footer;
	XmemError	err;

	bounce = malloc(priv_elems * sizeof(uint32_t));
	if (bounce == NULL)
		return XmemErrorENOMEM;

	/* copy the public data to the bounce buffer */
	memcpy(bounce + XMEM_H_ELEMS, buf, pub_elems * sizeof(uint32_t));

	/* fill in the markers */
	header = (void *)bounce;
	footer = (void *)(bounce + __f_eloff(pub_elems, pub_eloff));
	fill_hf(header, footer, pub_elems, bounce + XMEM_H_ELEMS);

	/* copy the table to XMEM */
	err = routines.SendTable(table, bounce, priv_elems,
				__h_eloff(pub_eloff), upflag);
	if (err != XmemErrorSUCCESS)
		goto out_err;

	free(bounce);
	return XmemErrorSUCCESS;

out_err:
	if (bounce)
		free(bounce);
	return err;
}

static XmemError send_table(XmemTableId table, void *buf, int pub_elems,
			    int pub_eloff, int upflag)
{
	struct header	header;
	struct footer	footer;
	XmemError	err;

	fill_hf(&header, &footer, pub_elems, NULL);

	/* write header, do not send SEGMENT_UPDATE */
	err = routines.SendTable(table, &header, XMEM_H_ELEMS,
				__h_eloff(pub_eloff), 0);
	if (err != XmemErrorSUCCESS)
		return err;

	/* write footer, do not send SEGMENT_UPDATE */
	err = routines.SendTable(table, &footer, XMEM_F_ELEMS,
				__f_eloff(pub_elems, pub_eloff), 0);
	if (err != XmemErrorSUCCESS)
		return err;

	/* write the table itself, send SEGMENT_UPDATE if requested */
	err = routines.SendTable(table, buf, pub_elems, phys_eloff(pub_eloff),
				upflag);
	if (err != XmemErrorSUCCESS)
		return err;

	return XmemErrorSUCCESS;
}

static XmemError receive_table_atomic(XmemTableId table, void *buf,
				      int pub_elems, int pub_eloff)
{
	size_t		priv_elems = pub_elems + XMEM_HF_ELEMS;
	uint32_t	*bounce = NULL;
	struct header	*header;
	struct footer	*footer;
	XmemError	err;

	bounce = malloc(priv_elems * sizeof(uint32_t));
	if (bounce == NULL)
		return XmemErrorENOMEM;

	err = routines.RecvTable(table, bounce, priv_elems,
				__h_eloff(pub_eloff));
	if (err != XmemErrorSUCCESS)
		goto out_err;

	/* check markers */
	header = (void *)bounce;
	footer = (void *)(bounce + __f_eloff(pub_elems, pub_eloff));
	err = evaluate_hf(header, footer, pub_elems, bounce + XMEM_H_ELEMS);
	if (err != XmemErrorSUCCESS)
		goto out_err;

	/* copy from the bounce buffer to the user's buffer */
	memcpy(buf, bounce + XMEM_H_ELEMS, pub_elems * sizeof(uint32_t));

	free(bounce);
	return XmemErrorSUCCESS;

out_err:
	if (bounce)
		free(bounce);
	return err;
}
//@}


/*
 * The following are exported (non-static) Xmem Lib functions
 * These are documented in the header file.
 */


XmemError XmemInitialize(XmemDevice device)
{
	XmemDevice 	fdev;
	XmemDevice 	ldev;
	XmemDevice 	dev;
	XmemError 	err;

	if (libinitialized)
		return XmemErrorSUCCESS;
	bzero((void *)attach_address, XmemMAX_TABLES * sizeof(void *));
	bzero((void *)&node_tab, sizeof(XmemDrvrNodeTable));

	err = XmemReadNodeTableFile();
	if (err != XmemErrorSUCCESS)
		return err;

	bzero((void *) &seg_tab, sizeof(XmemDrvrSegTable));
	err = XmemReadSegTableFile();
	if (err != XmemErrorSUCCESS)
		return err;

	if (device == XmemDeviceANY) {
		fdev = XmemDeviceVMIC;
		ldev = XmemDeviceNETWORK;
	}
	else {
		fdev = device;
		ldev = device;
	}
	for (dev = fdev; dev <= ldev; dev++) {
		err = InitDevice(dev);
		if (err == XmemErrorSUCCESS) {
			libinitialized = 1;
			return err;
		}
	}
	return XmemErrorNOT_INITIALIZED;
}



XmemNodeId XmemWhoAmI()
{
	return my_nid;
}



int XmemCheckForWarmStart()
{
	return warm_start;
}


char *XmemErrorToString(XmemError err)
{
	char 		*cp;
	static char 	result[XmemErrorSTRING_SIZE];

	if (err < 0 || err >= XmemErrorCOUNT)
		cp = "No such error number";
	else
		cp = estr[(int)err]; /* estr: global error string array */
	bzero((void *)result, XmemErrorSTRING_SIZE);
	strcpy(result, cp);
	return result;
}


XmemNodeId XmemGetAllNodeIds()
{
	if (libinitialized)
		return routines.GetAllNodeIds();
	return 0;
}


XmemError XmemRegisterCallback(void (*cb)(XmemCallbackStruct *cbs),
			XmemEventMask mask)
{
	XmemError err;

	if (! libinitialized)
		return XmemErrorNOT_INITIALIZED;
	err = routines.RegisterCallback(cb, mask);
	if (err == XmemErrorSUCCESS) {
		if (mask) {
			libcallmask |= mask;
			libcallback = cb;
		}
		else {
			libcallmask = 0;
			libcallback = NULL;
		}
	}
	return err;
}


XmemEventMask XmemGetRegisteredEvents()
{
	return (XmemEventMask)libcallmask;
}


XmemEventMask XmemWait(int timeout)
{
	if (libinitialized)
		return routines.Wait(timeout);
	return 0;
}


XmemEventMask XmemPoll()
{
	if (libinitialized)
		return routines.Poll();
	return 0;
}


XmemError XmemSendTable(XmemTableId table, void *buf, int elems,
			int offset, int upflag)
{
	if (!libinitialized)
		return XmemErrorNOT_INITIALIZED;

	if (markers_mask & XmemMarkersDISABLE) {
		return routines.SendTable(table, buf, elems, phys_eloff(offset),
					upflag);
	}

	if (markers_mask & XmemMarkersATOMIC)
		return send_table_atomic(table, buf, elems, offset, upflag);

	return send_table(table, buf, elems, offset, upflag);
}


XmemError XmemRecvTable(XmemTableId table, void *buf, int elems,
			int offset)
{
	XmemError err;

	if (!libinitialized)
		return XmemErrorNOT_INITIALIZED;

	if (markers_mask & XmemMarkersATOMIC)
		return receive_table_atomic(table, buf, elems, offset);

	err = check_markers(table, elems, offset);
	if (err != XmemErrorSUCCESS)
		return err;

	return routines.RecvTable(table, buf, elems, phys_eloff(offset));
}


XmemError XmemSendMessage(XmemNodeId nodes, XmemMessage *mess)
{
	if (libinitialized)
		return routines.SendMessage(nodes, mess);
	return XmemErrorNOT_INITIALIZED;
}


XmemTableId XmemCheckTables()
{
	if (libinitialized)
		return routines.CheckTables();
	return XmemErrorNOT_INITIALIZED;
}


XmemError XmemErrorCallback(XmemError err, unsigned long ioe)
{
	XmemCallbackStruct cbs;

	if (!libcallback)
		return err;
	bzero((void *)&cbs, sizeof(XmemCallbackStruct));
	switch (err) {

	case XmemErrorSUCCESS:
		break;

	case XmemErrorTIMEOUT:

		cbs.Mask = XmemEventMaskTIMEOUT;
		if (libcallmask & XmemEventMaskTIMEOUT)
			libcallback(&cbs);
		break;

	case XmemErrorIO:

		cbs.Mask = XmemEventMaskIO;
		cbs.Data = ioe;
		if (libcallmask & XmemEventMaskIO)
			libcallback(&cbs);
		break;

	case XmemErrorSYSTEM:

		cbs.Mask = XmemEventMaskSYSTEM;
		cbs.Data = ioe;
		if (libcallmask & XmemEventMaskSYSTEM)
			libcallback(&cbs);
		break;

	default:

		cbs.Mask = XmemEventMaskSOFTWARE;
		cbs.Data = (unsigned long)err;
		if (libcallmask & XmemEventMaskSOFTWARE)
			libcallback(&cbs);
		break;
	}
	return err;
}


int XmemGetKey(char *name)
{
	int 	i;
	int	key;

	key = 0;
	if (NULL == name)
		return key;
	for (i = 0; i < strlen(name); i++)
		key = (key << 1) + (int)name[i];
	return key;
}


void *XmemGetSharedMemory(XmemTableId tid)
{
	int 		tnum, msk;
	unsigned long   bytes, smemid;
	int		elems;
	uint32_t	user;
	key_t 	smemky;
	XmemError 	err;
	XmemNodeId 	nid;
	void		*table;
	char 		*cp;

	if (! libinitialized)
		goto error;
	for (tnum = 0; tnum < XmemMAX_TABLES; tnum++) {
		msk = 1 << tnum;
		if (! (msk & tid))
			continue;
		if (attach_address[tnum])
			return attach_address[tnum];
		else
			break; /* it doesn't exist yet */
	}


	cp = XmemGetTableName(tid);
	if (!cp)
		goto error;

	err = XmemGetTableDesc(tid, &elems, &nid, &user);
	if (err != XmemErrorSUCCESS)
		goto error;

	bytes = elems * sizeof(uint32_t);
	smemky = XmemGetKey(cp);
	smemid = shmget(smemky, bytes, 0666);

	if (smemid == -1) {
		if (ENOENT != errno)
			goto error;
		/* segment does not exist; create it */
		smemid = shmget(smemky, bytes, IPC_CREAT | 0666);
		if (-1 == smemid)
			goto error;
		/* attach memory segment to smemid */
		table = shmat(smemid, (char *)0, 0);
		if (-1 == (int)table)
			goto error;
		if (tnum < XmemMAX_TABLES)
			attach_address[tnum] = table;

		err = XmemRecvTable(tid, table, elems, 0);
		if (XmemErrorSUCCESS != err)
			goto error;
		return table;
	}
	else { /* segment was already there */
		table = shmat(smemid, (char *)0, 0);
		if (-1 == (int)table)
			goto error;
		if (tnum < XmemMAX_TABLES)
			attach_address[tnum] = table;
		return table;
	}
error:
	XmemErrorCallback(XmemErrorSYSTEM, errno);
	return NULL;
}


XmemTableId XmemGetAllTableIds()
{
	return seg_tab.Used;
}


char *XmemGetNodeName(XmemNodeId node)
{
	int i;
	unsigned long msk;

	if (!xmem) /* global variable, means a device is open */
		return (char *)0;
	for (i = 0; i < XmemMAX_NODES; i++) {
		msk = 1 << i;
		if (node_tab.Used & msk && node == node_tab.Descriptors[i].Id)
			return node_tab.Descriptors[i].Name;
	}
	XmemErrorCallback(XmemErrorNO_SUCH_NODE, 0);
	return (char *)0;
}


XmemNodeId XmemGetNodeId(XmemName name)
{
	int 		i;
	unsigned long	msk;

	if (!xmem)
		return 0;
	for (i = 0; i < XmemMAX_NODES; i++) {
		msk = 1 << i;
		if (strcmp(name, node_tab.Descriptors[i].Name) == 0 &&
		    node_tab.Used & msk)
			return node_tab.Descriptors[i].Id;
	}
	XmemErrorCallback(XmemErrorNO_SUCH_NODE, 0);
	return 0;
}


char * XmemGetTableName(XmemTableId table)
{
	int 		i;
	unsigned long	msk;

	if (!xmem)
		return (char *)0;
	for (i = 0; i < XmemMAX_TABLES; i++) {
		msk = 1 << i;
		if (seg_tab.Used & msk && table == seg_tab.Descriptors[i].Id)
			return seg_tab.Descriptors[i].Name;
	}
	XmemErrorCallback(XmemErrorNO_SUCH_TABLE, 0);
	return (char *)0;
}



XmemTableId XmemGetTableId(XmemName name)
{
	int		i;
	unsigned long	msk;

	if (!xmem)
		return 0;
	for (i = 0; i < XmemMAX_TABLES; i++) {
		msk = 1 << i;
		if (strcmp(name,seg_tab.Descriptors[i].Name) == 0 &&
		    seg_tab.Used & msk)
			return seg_tab.Descriptors[i].Id;
	}
	XmemErrorCallback(XmemErrorNO_SUCH_TABLE, 0);
	return 0;
}



XmemError XmemGetTableDesc(XmemTableId table, int *elems,
			XmemNodeId *nodes, uint32_t *user)
{
	int 		i;
	unsigned long	msk;

	if (!xmem)
		return XmemErrorCallback(XmemErrorNOT_INITIALIZED, 0);
	for (i = 0; i < XmemMAX_TABLES; i++) {
		msk = 1 << i;
		if (seg_tab.Used & msk && table == seg_tab.Descriptors[i].Id) {
			*elems = priv_to_pub_elems(seg_tab.Descriptors[i].Size /
					sizeof(uint32_t));
			*nodes = seg_tab.Descriptors[i].Nodes;
			*user  = seg_tab.Descriptors[i].User;
			return XmemErrorSUCCESS;
		}
	}
	return XmemErrorCallback(XmemErrorNO_SUCH_TABLE, 0);
}


XmemError XmemReadTableFile(XmemTableId tid)
{
	int 		i, cnt;
	unsigned long	msk;
	unsigned long   bytes;
	int		elems;
	uint32_t	user;
	XmemNodeId 	nodes;
	XmemError	err;
	char 		*cp;
	char		tbnam[64];
	FILE 		*fp;
	void		*table;

	for (i = 0; i < XmemMAX_TABLES; i++) {
		msk = 1 << i;
		if (! (msk & tid))
			continue;
		cp = XmemGetTableName(msk);
		err = XmemGetTableDesc(msk, &elems, &nodes, &user);
		if (XmemErrorSUCCESS != err)
			return err;
		table = XmemGetSharedMemory(msk);
		if (!table)
			return XmemErrorNO_SUCH_TABLE;

		bzero((void *)tbnam, 64);
		strcat(tbnam, cp);
		umask(0);

		fp = fopen(XmemGetFile(tbnam), "r");
		if (!fp)
			return XmemErrorCallback(XmemErrorSYSTEM, errno);
		bytes = elems * sizeof(uint32_t);
		cnt = fread(table, bytes, 1, fp);
		if (cnt <= 0)
			err = XmemErrorCallback(XmemErrorSYSTEM, errno);
		fclose(fp);
		fp = NULL;

		if (XmemErrorSUCCESS != err)
			return err;
	}
	return XmemErrorSUCCESS;
}


XmemError XmemWriteTableFile(XmemTableId tid)
{
	int 		i, cnt;
	unsigned long	msk;
	unsigned long	bytes;
	int		elems;
	uint32_t	user;
	XmemError 	err;
	XmemNodeId 	nodes;
	char 		*cp;
	char 		tbnam[64];
	void		*table;
	FILE		*fp;


	for (i = 0; i < XmemMAX_TABLES; i++) {
		msk = 1 << i;
		if (! (msk & tid))
			continue;
		cp = XmemGetTableName(msk);
		err = XmemGetTableDesc(msk, &elems, &nodes, &user);
		if (XmemErrorSUCCESS != err)
			return err;
		table = XmemGetSharedMemory(msk);
		if (!table)
			return XmemErrorNO_SUCH_TABLE;

		bzero((void *)tbnam, 64);
		strcat(tbnam, cp);
		umask(0);

		fp = fopen(XmemGetFile(tbnam), "w");
		if (!fp)
			return XmemErrorCallback(XmemErrorSYSTEM, errno);
		bytes = elems * sizeof(uint32_t);
		cnt = fwrite(table, bytes, 1, fp);
		if (cnt <= 0)
			err = XmemErrorCallback(XmemErrorSYSTEM,errno);
		fclose(fp);
		fp = NULL;

		if (err != XmemErrorSUCCESS)
			return err;
	}
	return XmemErrorSUCCESS;
}

XmemError XmemSendSoftWakeup(uint32_t nodeid, uint32_t data)
{
	if (!libinitialized)
		return XmemErrorNOT_INITIALIZED;
	return routines.SendSoftWakeup(nodeid, data);
}

/**
 * XmemLibUsleep - Sleep for 'delay' us.
 *
 * @param dly: desired delay, in us
 *
 */
void XmemLibUsleep(int dly)
{
	struct timespec rqtp, rmtp;

	rqtp.tv_sec = 0;
	rqtp.tv_nsec = dly * 1000;
	nanosleep(&rqtp, &rmtp);
}

XmemMarkersMask XmemSetMarkersMask(XmemMarkersMask mask)
{
	XmemMarkersMask omask = markers_mask;

	/*
	 * mask clean-up: enforce single ENABLE/DISABLE. Require one of them.
	 */
	if (mask & XmemMarkersENABLE) {
		if (mask & XmemMarkersDISABLE)
			mask = XmemMarkersDISABLE;
	} else {
		mask = XmemMarkersDISABLE;
	}
	mask &= XmemMarkersALL;

	if (mask & XmemMarkersCHECKSUM && !(mask & XmemMarkersATOMIC))
		mask &= ~XmemMarkersCHECKSUM;

	if (mask != 0)
		markers_mask = mask;
	return omask;
}
