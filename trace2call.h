#define DEBUG 0
/* ---------------------------trace 2 call---------------------------- */
#ifndef _TRACETOCALL_H
#define _TRACETOCALL_H 1

#define ACT_READ '0'
#define ACT_WRITE '1'

//#define _CG_LIMIT 1

#endif // TRACETOCALL

extern void IO_Listening(char *trace_file_path, int isWriteOnly, off_t startLBA);
extern int BandOrBlock;
