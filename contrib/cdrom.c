/*
 * cdrom.c: This file handles all the CDROM routines, in BitchX
 *
 * Written by Tom Zickel
 * a.k.a. IceBreak on the irc
 *
 * Copyright(c) 1996
 * Modified Colten Edwards aka panasync.
 *
 */

#include "irc.h"
#include "ircaux.h"
#include "./cdrom.h"
#include "output.h"
#include "misc.h"
#include "vars.h"

#define cparse(s) convert_output_format(s, NULL, NULL)

static int drive = 0;

static char cdrom_prompt[]="%gC%Gd%gROM%w";

static struct cdrom_tochdr hdr;
static struct cdrom_etocentry TocEntry[101];
static struct cdrom_ti ti;

static char *cd_device = NULL;


static int check_cdrom_str(void)
{
	if (cd_device)
		return 1;
	put_it("%s: CD_DEVICE (/SET)  - The name of the CDROM device",cparse(cdrom_prompt));
	return 0;
}

static void lba2msf(int lba, unsigned char *msf)
{
	lba += CD_BLOCK_OFFSET;
	msf[0] = lba / (CD_SECS*CD_FRAMES);
	lba %= CD_SECS*CD_FRAMES;
	msf[1] = lba / CD_FRAMES;
	msf[2] = lba % CD_FRAMES;
}

int cd_init(char *dev)
{
unsigned char msf_ent[3];
unsigned char msf1_ent[3];
int i, rc;

	if (!dev || ((drive = open(dev, O_RDWR)) < 0)) 
		return (-1);

	if ((rc = ioctl(drive, CDROMREADTOCHDR, &hdr)) == -1)
	{
		put_it("%s: can't get TocHeader (error %d).",cparse(cdrom_prompt), rc);
		return (-2);
	}

	for (i=1;i<=hdr.cdth_trk1+1;i++)
	{
		if (i!=hdr.cdth_trk1+1) 
			TocEntry[i].cdte_track = i;
		else 
			TocEntry[i].cdte_track = CDROM_LEADOUT;
		TocEntry[i].cdte_format = CDROM_LBA;
		TocEntry[i].avoid=TocEntry[i].cdte_ctrl & CDROM_DATA_TRACK ? 1 : 0;
		if ((rc = ioctl(drive,CDROMREADTOCENTRY,&TocEntry[i])) != 0)
			put_it("%s: can't get TocEntry #%d (error %d).",cparse(cdrom_prompt), i,rc);
		else
			lba2msf(TocEntry[i].cdte_addr.lba,&msf_ent[0]);
	}

	for (i=1;i<=hdr.cdth_trk1+1;i++)
	{
		ioctl(drive,CDROMREADTOCENTRY,&TocEntry[i]);
		lba2msf(TocEntry[i].cdte_addr.lba,&msf_ent[0]);
		ioctl(drive,CDROMREADTOCENTRY,&TocEntry[i+1]);
		lba2msf(TocEntry[i+1].cdte_addr.lba,&msf1_ent[0]);
		TocEntry[i].length = (msf1_ent[0] * 60 + msf1_ent[1])-
			(msf_ent[0] * 60 + msf_ent[1]);
	}
	return (hdr.cdth_trk1);
}

static int check_mount(char *device)
{
FILE *fp;
struct mntent *mnt;

	if ((fp = setmntent(MOUNTED, "r")) == NULL)
		return 0;
	
	while ((mnt = getmntent (fp)) != NULL)
	{
		if ((strcmp (mnt->mnt_type, "iso9660") == 0) && (strcmp (mnt->mnt_fsname, device) == 0))
		{
			endmntent(fp);
			return 0;
		}
	}
	endmntent (fp);
	return 1;
}

void set_cd_device(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
char *str;
int code;
	if (!(str = next_arg(args , &args)))
	{
		return;
	}
	if (drive) 
  		close(drive);
	if (check_mount(str) == 0)
	{
		put_it("%s: ERROR: CDROM is already mounted, please unmount, and try again",cparse(cdrom_prompt));
		new_free(&cd_device);
		return;
	}

	if ((code = cd_init(str)) < 0)
	{
		put_it("%s: ERROR(%d): Could not initalize the CDROM, check if a disk is inside",cparse(cdrom_prompt), code);
		new_free(&cd_device);
		return;
	}
	put_it("%s: CDROM device is now set to - %s",cparse(cdrom_prompt),str);
	malloc_strcpy(&cd_device, str);
}

void cd_stop(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
	if (!check_cdrom_str())
		return;
	put_it("%s: Stopped playing cdrom",cparse(cdrom_prompt));
	ioctl(drive,CDROMSTOP);
}

void cd_eject(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
	if (!check_cdrom_str())
		return;

	put_it("%s: ejected cdrom tray",cparse(cdrom_prompt));
	ioctl(drive,CDROMEJECT);
	close(drive);
	drive=0;
}

void cd_play(IrcCommandDll *interp, char *command, char *args, char *subargs)
{

int tn;
char *trackn;
unsigned char first, last;
struct cdrom_tochdr tocHdr;
	
	if (!check_cdrom_str() || !drive)
		return;
	
	if (args && *args)
	{
		trackn=next_arg(args, &args);
		tn=atoi(trackn);

	        ioctl(drive,CDROMREADTOCHDR,&tocHdr);

	        first = tocHdr.cdth_trk0;
	        last = tocHdr.cdth_trk1;
	        ti.cdti_trk0=tn;

	        if (ti.cdti_trk0<first) 
	        	ti.cdti_trk0=first;
	        if (ti.cdti_trk0>last) 
	        	ti.cdti_trk0=last;

	        ti.cdti_ind0=0;
	        ti.cdti_trk1=last;
	        ti.cdti_ind1=0;

	        if (TocEntry[tn].avoid==0)
	        {
			ioctl(drive,CDROMSTOP);
			ioctl(drive,CDROMPLAYTRKIND,&ti);
		        put_it("%s: Playing track number #%d",cparse(cdrom_prompt),tn);
	        }
	        else
	        	put_it("%s: Cannot play track #%d (Might be data track)",cparse(cdrom_prompt),tn);
	}
        else
	        put_it("%s: Usage: /cdplay <track number>",cparse(cdrom_prompt));

}

void cd_list(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
int i;
unsigned char msf_ent[3];
struct cdrom_subchnl subchnl;

	if (!check_cdrom_str())
		return;
	ioctl(drive,CDROMSUBCHNL,&subchnl);
	for (i=1;i<=hdr.cdth_trk1;i++)
	{
		if ((subchnl.cdsc_audiostatus==CDROM_AUDIO_PLAY) && (subchnl.cdsc_trk == i))
		{
			lba2msf(TocEntry[i].cdte_addr.lba,&msf_ent[0]);
			put_it("%s: Track #%02d: %02d:%02d %02d:%02d (*)",
				cparse(cdrom_prompt),
				TocEntry[i].cdte_track,
				msf_ent[0],
				msf_ent[1],
				(TocEntry[i].length/60) % 60,
				TocEntry[i].length % 60);
		}
		else
		{
			lba2msf(TocEntry[i].cdte_addr.lba,&msf_ent[0]);
			put_it("%s: Track #%02d: %02d:%02d %02d:%02d",
				cparse(cdrom_prompt),
				TocEntry[i].cdte_track,
				msf_ent[0],
				msf_ent[1],
				(TocEntry[i].length/60) % 60,
				TocEntry[i].length % 60);
		}
	}
}

void cd_volume(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
char *left, *right;
struct cdrom_volctrl volctrl;

	if (!check_cdrom_str())
		return;

	if (args && *args)
	{
		left=next_arg(args, &args);
		right=next_arg(args, &args);
		if (left && *left)
			volctrl.channel0=atoi(left);
		if (right && *right)
			volctrl.channel1=atoi(right);
		ioctl(drive,CDROMVOLCTRL,&volctrl);
		put_it("%s: CDROM Volume is now <%d> <%d>",cparse(cdrom_prompt),volctrl.channel0,volctrl.channel1);
	}
	else
		put_it("%s: Usage: /cdvol <left> <right>",cparse(cdrom_prompt));
}

void cd_pause(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
static int cpause = 0;
	if (!check_cdrom_str())
		return;
	ioctl(drive, !cpause?CDROMPAUSE:CDROMRESUME);
	cpause ^= 1;
}

void cd_help(IrcCommandDll *interp, char *command, char *args, char *subargs)
{
	put_it("%s: CDPLAY            - Play a CDROM Track Number",cparse(cdrom_prompt));
	put_it("%s: CDSTOP            - Make the CDROM Stop playing",cparse(cdrom_prompt));
	put_it("%s: CDEJECT           - Eject the CDROM Tray",cparse(cdrom_prompt));
	put_it("%s: CDVOL             - Set's the CDROM Volume",cparse(cdrom_prompt));
	put_it("%s: CDLIST            - List of CDROM tracks",cparse(cdrom_prompt));
	put_it("%s: CDPAUSE           - Pause/resume the CDROM",cparse(cdrom_prompt));
}

int
Cdrom_Init(interp)
    IrcCommandDll **interp;		/* Interpreter in which the package is
				 * to be made available. */
{
	IrcCommandDll *new;
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdstop");
	new->func = cd_stop;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdplay");
	new->func = cd_play;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdeject");
	new->func = cd_eject;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdlist");
	new->func = cd_list;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdhelp");
	new->func = cd_help;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdvolume");
	new->func = cd_volume;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cdpause");
	new->func = cd_pause;
	add_to_list((List **)interp, (List *)new);
	new = (IrcCommandDll *) new_malloc(sizeof(IrcCommandDll));
	new->name = m_strdup("cddevice");
	new->func = set_cd_device;
	add_to_list((List **)interp, (List *)new);
	put_it("%s: Module loaded and ready. /cddevice <dev> to start", cparse(cdrom_prompt));
	put_it("%s: /cdhelp for list of new commands.", cparse(cdrom_prompt));
	return 0;
}