//--------------------------------------------------------------
//File name:    elf.c
//--------------------------------------------------------------
#include <stdio.h>
#include <tamtypes.h>
#include <loadfile.h>
#include <kernel.h>
#include <sifrpc.h>
#include <string.h>
#include <fileio.h>
#include <sys/stat.h>
#include <fileXio_rpc.h>
#include <sys/fcntl.h>

#define MAX_PATH 1025

extern u8 *loader_elf;
extern int size_loader_elf;
extern u8 *fakehost_irx;
extern int size_fakehost_irx;

// ELF-loading stuff
#define ELF_MAGIC		0x464c457f
#define ELF_PT_LOAD		1

//------------------------------
typedef struct
{
	u8	ident[16];			// struct definition for ELF object header
	u16	type;
	u16	machine;
	u32	version;
	u32	entry;
	u32	phoff;
	u32	shoff;
	u32	flags;
	u16	ehsize;
	u16	phentsize;
	u16	phnum;
	u16	shentsize;
	u16	shnum;
	u16	shstrndx;
} elf_header_t;
//------------------------------
typedef struct
{
	u32	type;				// struct definition for ELF program section header
	u32	offset;
	void	*vaddr;
	u32	paddr;
	u32	filesz;
	u32	memsz;
	u32	flags;
	u32	align;
} elf_pheader_t;

#define MOUNT_LIMIT 4
#define MAX_NAME 256
char mountedParty[MOUNT_LIMIT][MAX_NAME];
int gen_fd[256]; //Allow up to 256 generic file handles
int gen_io[256]; //For each handle, also memorize io type
int	latestMount = -1;
int fileMode =  FIO_S_IRUSR | FIO_S_IWUSR | FIO_S_IXUSR | FIO_S_IRGRP | FIO_S_IWGRP | FIO_S_IXGRP | FIO_S_IROTH | FIO_S_IWOTH | FIO_S_IXOTH;

//--------------------------------------------------------------
//End of data declarations

//--------------------------------------------------------------
//Start of function code

//--------------------------------------------------------------
void unmountParty(int party_ix)
{
	char pfs_str[6];

	strcpy(pfs_str, "pfs0:");
	pfs_str[3] += party_ix;
	if(fileXioUmount(pfs_str) < 0)
		return; //leave variables unchanged if unmount failed (remember true state)
	if(party_ix < MOUNT_LIMIT){
		mountedParty[party_ix][0] = 0;
	}
	if(latestMount==party_ix)
		latestMount=-1;
}

//--------------------------------------------------------------
int mountParty(const char *party)
{
	int i, j;
	char pfs_str[6];

	for(i=0; i<MOUNT_LIMIT; i++){ //Here we check already mounted PFS indexes
		if(!strcmp(party, mountedParty[i]))
			goto return_i;
	}

	for(i=0, j=-1; i<MOUNT_LIMIT; i++){ //Here we search for a free PFS index
		if(mountedParty[i][0] == 0){
			j=i;
			break;
		}
	}

	if(j == -1){ //Here we search for a suitable PFS index to unmount
		for(i=0; i<MOUNT_LIMIT; i++){
			if(i!=latestMount){
				j=i;
				break;
			}
		}
		unmountParty(j);
	}
	//Here j is the index of a free PFS mountpoint
	//But 'free' only means that the main uLE program isn't using it
	//If the ftp server is running, that may have used the mountpoints

	//RA NB: The old code to reclaim FTP partitions was seriously bugged...

	i = j;
	strcpy(pfs_str, "pfs0:");

	pfs_str[3] = '0'+i;
	if(fileXioMount(pfs_str, party, FIO_MT_RDWR) < 0){ //if FTP stole it
		for(i=0; i<=4; i++){ //for loop to kill FTP partition mountpoints
			if(i!=latestMount){ //if unneeded by uLE
				unmountParty(i);  //unmount partition mountpoint
				pfs_str[3] = '0'+i; //prepare to reuse that mountpoint
				if(fileXioMount(pfs_str, party, FIO_MT_RDWR) >= 0)
					break; //break from the loop on successful mount
			} //ends if unneeded by uLE
		} //ends for loop to kill FTP partition mountpoints
		//Here i indicates what happened above with the following meanings:
		//0..4==Success after trying i mountpoints,  5==Failure
		if(i>4)
			return -1;
	} //ends if clause for mountpoints stolen by FTP
	strcpy(mountedParty[i], party);
return_i:
	latestMount = i;
	return i;
}

//--------------------------------------------------------------
void genLimObjName(char *uLE_path, int reserve)
{
	char	*p, *q, *r;
	int	limit = 256; //enforce a generic limit of 256 characters
	int	folder_flag = (uLE_path[strlen(uLE_path)-1] == '/'); //flag folder object
	int overflow;

	if(!strncmp(uLE_path, "mc", 2) || !strncmp(uLE_path, "vmc", 3))
		limit = 32;    //enforce MC limit of 32 characters

	if(folder_flag)                  //if path ends with path separator
		uLE_path[strlen(uLE_path)-1]=0;  //  remove final path separator (temporarily)

	p = uLE_path;    //initially assume a pure object name (quite insanely :))
	if((q=strchr(p, ':')) != NULL)   //if a drive separator is present
		p = q+1;                       //  object name may start after drive separator
	if((q=strrchr(p, '/')) != NULL)  //If there's any path separator in the string
		p = q+1;                       //  object name starts after last path separator
	limit -= reserve;                //lower limit by reserved character space
	overflow = strlen(p)-limit;      //Calculate length of string to remove (if positive)
	if((limit<=0)||(overflow<=0))    //if limit invalid, or not exceeded
		goto limited;                  //  no further limitation is needed
	if((q=strrchr(p, '.')) == NULL)  //if there's no extension separator
		goto limit_end;                //limitation must be done at end of full name
	r = q-overflow;                  //r is the place to recopy file extension
	if(r > p){                       //if this place is above string start
		strcpy(r, q);                  //remove overflow from end of prefix part
		goto limited;                  //which concludes the limitation
	}//if we fall through here, the prefix part was too short for the limitation needed
limit_end:
	p[limit] = 0;                  //  remove overflow from end of full name
limited:

	if(folder_flag)                  //if original path ended with path separator
		strcat(uLE_path, "/");         //  reappend final path separator after name
}
//------------------------------
//endfunc genLimObjName

//--------------------------------------------------------------
int genOpen(char *path, int mode)
{
	int i, fd, io;

	genLimObjName(path, 0);
	for(i=0; i<256; i++)
		if(gen_fd[i] < 0)
			break;
	if(i > 255)
		return -1;

	if(!strncmp(path, "pfs", 3)){
		fd = fileXioOpen(path, mode, fileMode);
		io = 1;
	}else{
		fd = fioOpen(path, mode);
		io = 0;
	}
	if(fd < 0)
		return fd;

	gen_fd[i] = fd;
	gen_io[i] = io;
	return i;
}
//------------------------------
//endfunc genOpen

//--------------------------------------------------------------
int genLseek(int fd, int where, int how)
{
	if((fd < 0) || (fd > 255) || (gen_fd[fd] < 0))
		return -1;

	if(gen_io[fd])
		return fileXioLseek(gen_fd[fd], where, how);
	else
		return fioLseek(gen_fd[fd], where, how);
}
//------------------------------
//endfunc genLseek

//--------------------------------------------------------------
int genRead(int fd, void *buf, int size)
{
	if((fd < 0) || (fd > 255) || (gen_fd[fd] < 0))
		return -1;

	if(gen_io[fd])
		return fileXioRead(gen_fd[fd], buf, size);
	else
		return fioRead(gen_fd[fd], buf, size);
}
//------------------------------
//endfunc genRead

//--------------------------------------------------------------
int genClose(int fd)
{
	int ret;

	if((fd < 0) || (fd > 255) || (gen_fd[fd] < 0))
		return -1;

	if(gen_io[fd]==1)
		ret = fileXioClose(gen_fd[fd]);
	else if(gen_io[fd]==0)
		ret = fioClose(gen_fd[fd]);
	else
		return -1;
	gen_fd[fd] = -1;
	return ret;
}
//------------------------------
//endfunc genClose

//--------------------------------------------------------------
// checkELFheader Tests for valid ELF file 
// Modified version of loader from Independence
//	(C) 2003 Marcus R. Brown <mrbrown@0xd6.org>
//--------------------------------------------------------------
int checkELFheader(char *path)
{
	elf_header_t elf_head;
	u8 *boot_elf = (u8 *) &elf_head;
	elf_header_t *eh = (elf_header_t *) boot_elf;
	int fd, size=0, ret;
	char fullpath[MAX_PATH], tmp[MAX_PATH], *p;

	strcpy(fullpath,path);
	if(	!strncmp(fullpath, "mc", 2)
		||!strncmp(fullpath, "rom", 3)
		||!strncmp(fullpath, "cdrom", 5)
		||!strncmp(fullpath, "cdfs", 4)
		){; //fullpath is already correct
	}else if(!strncmp(fullpath, "hdd0:", 5)) {
		p = &path[5];
		if(*p == '/')
			p++;
		sprintf(tmp, "hdd0:%s", p);
		p = strchr(tmp, '/');
		sprintf(fullpath, "pfs0:%s", p);
		*p = 0;
		if( (ret = mountParty(tmp)) < 0)
			goto error;
		fullpath[3] += ret;
	}else if(!strncmp(fullpath, "mass", 4)){
		char *pathSep;

		pathSep = strchr(path, '/');
		if(pathSep && (pathSep-path<7) && pathSep[-1]==':')
			strcpy(fullpath+(pathSep-path), pathSep+1);
	}else if(!strncmp(fullpath, "host:", 5)){
		if(path[5] == '/')
			strcpy(fullpath+5, path+6);
	} else {
		return 0;  //return 0 for unrecognized device
	}
	if ((fd = genOpen(fullpath, O_RDONLY)) < 0) 
		goto error;
	size = genLseek(fd, 0, SEEK_END);
	if (!size){
		genClose(fd);
		goto error;
	}
	genLseek(fd, 0, SEEK_SET);
	genRead(fd, boot_elf, sizeof(elf_header_t));
	genClose(fd);

	if ((_lw((u32)&eh->ident) != ELF_MAGIC) || eh->type != 2)
		goto error;
	
	return 1;  //return 1 for successful check
error:
	return -1; //return -1 for failed check
}
//------------------------------
//End of func:  int checkELFheader(const char *path)
//--------------------------------------------------------------
// RunLoaderElf loads LOADER.ELF from program memory and passes
// args of selected ELF and partition to it
// Modified version of loader from Independence
//	(C) 2003 Marcus R. Brown <mrbrown@0xd6.org>
//------------------------------
void RunLoaderElf(char *filename, char *party)
{
	u8 *boot_elf;
	elf_header_t *eh;
	elf_pheader_t *eph;
	void *pdata;
	int ret, i;
	char *argv[2];

	if((!strncmp(party, "hdd0:", 5)) && (!strncmp(filename, "pfs0:", 5))){
		char fakepath[128], *p;
		if(0 > fileXioMount("pfs0:", party, FIO_MT_RDONLY)){
			//Some error occurred, it could be due to something else having used pfs0
			unmountParty(0);  //So we try unmounting pfs0, to try again
			if(0 > fileXioMount("pfs0:", party, FIO_MT_RDONLY))
				return;  //If it still fails, we have to give up...
		}
		strcpy(fakepath,filename);
		p=strrchr(fakepath,'/');
		if(p==NULL) strcpy(fakepath,"pfs0:");
		else
		{
			p++;
			*p='\0';
		}
		//printf("Loading fakehost.irx %i bytes\n", size_fakehost_irx);
		//printf("Faking for path \"%s\" on partition \"%s\"\n", fakepath, party);
		SifExecModuleBuffer(&fakehost_irx, size_fakehost_irx, strlen(fakepath), fakepath, &ret);
		
	}

/* NB: LOADER.ELF is embedded  */
	boot_elf = (u8 *)&loader_elf;
	eh = (elf_header_t *)boot_elf;
	if (_lw((u32)&eh->ident) != ELF_MAGIC)
		while (1);

	eph = (elf_pheader_t *)(boot_elf + eh->phoff);

/* Scan through the ELF's program headers and copy them into RAM, then
									zero out any non-loaded regions.  */
	for (i = 0; i < eh->phnum; i++)
	{
		if (eph[i].type != ELF_PT_LOAD)
		continue;

		pdata = (void *)(boot_elf + eph[i].offset);
		memcpy(eph[i].vaddr, pdata, eph[i].filesz);

		if (eph[i].memsz > eph[i].filesz)
			memset(eph[i].vaddr + eph[i].filesz, 0,
					eph[i].memsz - eph[i].filesz);
	}

/* Let's go.  */
	fioExit();
	SifInitRpc(0);
	SifExitRpc();
	FlushCache(0);
	FlushCache(2);

	argv[0] = filename;
	argv[1] = party;
	
	ExecPS2((void *)eh->entry, 0, 2, argv);
}
//------------------------------
//End of func:  void RunLoaderElf(char *filename, char *party)
//--------------------------------------------------------------
//End of file:  elf.c
//--------------------------------------------------------------
