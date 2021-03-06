/*
wiiload zip manage the transfer of full zip file.
*/
#include <ctype.h>
#include "globals.h"
#include "zip/unzip.h"
#include "wiiload/wiiload.h"
#include "mystring.h"
#include "devices.h"

#define DU_ONLYROOT 3
#define DU_UNZIPAPP 2	// Application is installed in temp folder
#define DU_INSTAPP 1	// Application can be installed
#define DU_NONE 0		// No valid application found

static int ZipCheck (char *path)	// Dols contains a list of found dols
	{
	char fn[200];
	unzFile uf;
	unz_file_info fi;
	
	int masterDolCnt = 0, masterDirCnt = 0; 
	int execFound = 0;
	
	Debug("ZipCheck opening: %s", path);

	uf = unzOpen (path);
	if (!uf)
		return DU_NONE;	// unable to open the zip
		
	fsop_FileExist (path);
	
	Debug("ZipCheck opening: 0x%X", (int)uf);
	
	// Dounzip should count the dols in the zip.
	// if 1 dol in one folder is found, it is a hb package and will be installed in selected app (on sd or usb, in postloader)
	// otherwise (if there are dol inside) it will be unpacked in temp folder and a list of dol wii be shown

	unzGoToFirstFile(uf);
	do
		{
		if (unzGetCurrentFileInfo (uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK) break;
		// Debug("ZipCheck unzGetCurrentFileInfo: %s", fn);
		
		if (fn[strlen(fn) - 1] == '/') // It is a path
			{
			Debug("ZipCheck folder: %s (%d)", fn, fsop_CountFolderTree (fn));
			
			if (fsop_CountFolderTree (fn) == 1)
				masterDirCnt ++;
			}
		else
			{
			if ((ms_strstr (fn, ".dol") || ms_strstr (fn, ".elf")))
				{
				if (fsop_CountFolderTree (fn) == 2) // fsop_CountFolderTree must return 2 <path>/<filename>
					masterDolCnt ++;
				
				execFound ++;
				}
			}
		
		if (unzGoToNextFile(uf) != UNZ_OK) break;
		}
	while (true);
	
	unzClose (uf);
	
	Debug("ZipCheck complete: %d, %d", masterDirCnt, masterDolCnt);
	
	if (masterDirCnt == 1 && masterDolCnt == 1)
		return DU_INSTAPP;
		
	if (execFound)
		return DU_UNZIPAPP;

	return DU_NONE;
	}

int ZipUnpack (char *path, char *target, char *dols, int *errcnt)
	{
	int ioerr, err, bytes;
	u8 *b;
	int bsize = 65536; // 64Kb
	char fn[200];
	char outpath[200];
	char buff[200];
	FILE *f;
	unzFile uf;
	unz_file_info fi;
	int execFound = 0;
	int count = 0;
	
	if (errcnt) *errcnt = 0;
	
	grlib_Redraw ();
	grlib_PushScreen ();

	Debug("ZipUnpack source: %s", path);
	Debug("ZipUnpack target: %s", target);
	
	uf = unzOpen (path);
	Debug("ZipUnpack opening: 0x%X", (int)uf);

	if (!uf)
		return false;	// unable to open the zip
	
	// Let's create folder struct
	count = 0;
	unzGoToFirstFile(uf);
	do
		{
		if (unzGetCurrentFileInfo (uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK) break;
		
		if (fn[strlen(fn) - 1] == '/') // It is a path
			{
			sprintf (outpath, "%s/%s", target, fn);
			fsop_CreateFolderTree (outpath);
			
			Video_WaitPanel (TEX_HGL, 0, "Creating folders %d", count ++);
			}

		if (unzGoToNextFile(uf) != UNZ_OK) break;
		}
	while (true);
	
	
	// Ok, we can unpack the data
	b = malloc (bsize);
	
	// Let's create folder struct
	ioerr = 0;
	
	unzGoToFirstFile(uf);
	do
		{
		if (unzGetCurrentFileInfo (uf, &fi, fn, sizeof(fn), NULL, 0, NULL, 0) != UNZ_OK) break;

		if (fn[strlen(fn) - 1] != '/') // It is a file
			{
			err = unzOpenCurrentFilePassword(uf,NULL); // Open the file inside the zip (password = NULL)
			if (err != UNZ_OK) 
				{
				Debug ("unzOpenCurrentFilePassword: %d", err);
				break;
				} // file inside the zip is open
		 
			// Copy contents of the file inside the zip to the buffer
			sprintf (outpath, "%s/%s", target, fn);
			f = fopen (outpath , "wb");
			if (f)
				{
				do 
					{
					Video_WaitPanel (TEX_HGL, 0, "Unpacking file %d", count+1);
					
					bytes = unzReadCurrentFile (uf,b,bsize);
					if (bytes < 0) 
						{
						err = -1;
						break;
						}
					// copy the buffer to a string
					if (bytes > 0) 
						fwrite (b, bytes, 1, f);
					}
				while (bytes > 0);
				
				fclose(f);
				
				if (dols && (ms_strstr (fn, ".dol") || ms_strstr (fn, ".elf")))
					{
					sprintf (buff, "{%02d}%s", execFound, fn);
					execFound ++;
					
					strcat (dols, buff);
					
					Debug ("ZipCheck adding: %s", buff);
					}
					
				count ++;
				}
			else
				ioerr++;
			
			if (err != UNZ_OK) 
				break;
		 
			err = unzCloseCurrentFile (uf);  // close the zipfile
			if (err!=UNZ_OK) 
				{
				Debug ("unzCloseCurrentFile: %d", err);
				break;
				}
			}
		if (unzGoToNextFile(uf) != UNZ_OK) break;
		}
	while (true);
	
	free(b); // free up buffer memory	
	
	unzClose (uf);
	
	Debug (dols);
	
	if (errcnt) *errcnt = ioerr;
	
	if (ioerr) 
		return false;
		
	return execFound;
	}
	
#define MAXDOLS 10
void SelectDol(char *dol, char *target)
	{
	char code[8];
	char fn[MAXDOLS][128];
	char menu[1024];
	char *p;
	
	int items, i, j;
	
	*menu = '\0';
	
	p = dol;
	for (i = 0; i < MAXDOLS; i++)
		{
		sprintf (code, "{%02d}", i);
		p = strstr (p, code);
		
		if (p)
			{
			p += strlen(code);
			j = 0;
			while (*p != '\0' && *p != '{')
				{
				fn[i][j++] = *p;
				p++;
				}
			fn[i][j++] = 0;
			}
		else
			break;
		}
		
	items = i;
	
	for (i = 0; i < items; i++)
		{
		grlib_menuAddItem (menu, 100+i, fn[i]);
		}

	grlib_menuAddSeparator (menu);
	grlib_menuAddItem (menu, 0, "Cancel");
	
	int ret = grlib_menu (0, "Select dol/elf to run", menu);
	
	if (ret <= 0) return;
	
	ret -= 100;
	sprintf (wiiload.fullpath, "%s/%s", target, fn[ret]);
	wiiload.status = WIILOAD_HBREADY;
	}
	
void WiiloadZipMenu (void)
	{
	char dols[1024];
	char menu[1024];
	char path[300];
	char nametarget[300];
	
	wiiload.status = WIILOAD_IDLE;
	
	*nametarget = '\0';
	if (strlen(wiiload.filename))
		{
		strcpy (nametarget, wiiload.filename);

		char *p = ms_strstr (nametarget, ".zip");
		if (p) *p = '\0'; // remove extension
		}
	
	sprintf (path, "%s/%s", vars.tempPath, WIILOAD_ZIPFILE);
	int type = ZipCheck (path);
	
	*menu = '\0';
	*dols = '\0';
	
	if (type == DU_NONE)
		{
		grlib_menu (0, "Attention\n\nThe zip file doesn't contain any dol. This is unsupported", "Close");
		return;
		}
		
	if (type == DU_INSTAPP)
		{
		char title[256];
		
		if (devices_Get(DEV_SD)) grlib_menuAddItem (menu, 10, "Install to SD device");
		if (devices_Get(DEV_USB)) grlib_menuAddItem (menu, 20, "Install to USB device");
		
		grlib_menuAddSeparator (menu);
		
		grlib_menuAddItem (menu, 0, "Cancel");

		sprintf (title, "Installable ZIP package received\n\nPlease select where your package should be installed.");
		int ret = grlib_menu (0, title, menu);

		char target[300];
		if (ret == 10)
			{
			sprintf (target, "%s://apps", devices_Get(DEV_SD));
			ZipUnpack (path, target, dols, NULL);
			}
		if (ret == 20)
			{
			sprintf (target, "%s://apps", devices_Get(DEV_USB));
			ZipUnpack (path, target, dols, NULL);
			}
		}
		
	if (type == DU_UNZIPAPP)
		{
		char title[256];

		if (devices_Get(DEV_SD)) 
			{
			grlib_menuAddItem (menu, 10, "Unpack to sd://");
			grlib_menuAddItem (menu, 11, "Unpack to sd://apps");
			if (strlen (nametarget))
				grlib_menuAddItem (menu, 12, "Unpack to sd://apps/%s", nametarget);
			grlib_menuAddSeparator (menu);
			}
		if (devices_Get(DEV_USB))
			{
			grlib_menuAddItem (menu, 20, "Unpack to usb://");
			grlib_menuAddItem (menu, 21, "Unpack to usb://apps");
			if (strlen (nametarget))
				grlib_menuAddItem (menu, 22, "Unpack to usb://apps/%s", nametarget);
			grlib_menuAddSeparator (menu);
			}
		
		grlib_menuAddItem (menu, 30, "Unpack to temp folder and run contained dol");
		grlib_menuAddSeparator (menu);
		
		grlib_menuAddItem (menu, 0, "Cancel");

		sprintf (title, "Installable ZIP package received\n\nPlease select where your package should be installed.");
		int ret = grlib_menu (0, title, menu);

		char target[300];
		if (ret == 10)
			{
			sprintf (target, "%s://", devices_Get(DEV_SD));
			ZipUnpack (path, target, dols, NULL);
			}
		if (ret == 11)
			{
			sprintf (target, "%s://apps", devices_Get(DEV_SD));
			ZipUnpack (path, target, dols, NULL);
			}
		if (ret == 20)
			{
			sprintf (target, "%s://", devices_Get(DEV_USB));
			ZipUnpack (path, target, dols, NULL);
			}
		if (ret == 21)
			{
			sprintf (target, "%s://apps", devices_Get(DEV_USB));
			ZipUnpack (path, target, dols, NULL);
			}
		if (ret == 30)
			{
			sprintf (target, "%s", vars.tempPath);
			ZipUnpack (path, vars.tempPath, dols, NULL);
			}
		
		if (ret > 0)
			{
			SelectDol (dols, target);
			}
		}
	}

// This function will check for file type... sending postloader.dol or neekbooter.dol or a wad will change behaveur
bool WiiloadCheck (void)
	{
	char path[256];

	if (stricmp (wiiload.filename, "postloader.dol") == 0)
		{
		int ret = grlib_menu (0, "wiiload: postLoader.dol received", "Update postLoader installation##1|Boot it without updating##2|Cancel##-1");
		
		if (ret <= 0)
			{
			wiiload.status = WIILOAD_IDLE;
			return false;
			}
			
		if (ret == 1)
			{
			vars.saveExtendedConf = 1;
			
			int i;
			for (i = 0; i < DEV_MAX; i++)
				{
				if (devices_Get(i)) 
					{
					sprintf (path, "%s://apps/postloader/boot.dol", devices_Get(i));
					Video_WaitPanel (TEX_HGL, 0, "Please wait...|Updating '%s'", path);
					
					if (fsop_FileExist (path))
						{
						fsop_CopyFile (wiiload.fullpath, path, NULL);
						}
					}
				}
			}
		}

	if (stricmp (wiiload.filename, "neekbooter.dol") == 0)
		{
		grlib_menu (0, "wiiload: neekbooter.dol updated", "  OK  ");

		int i;
		for (i = 0; i < DEV_MAX; i++)
			{
			if (devices_Get(i)) 
				{
				sprintf (path, "%s://neekbooter.dol", devices_Get(i));
				
				if (fsop_FileExist (path))
					{
					fsop_CopyFile (wiiload.fullpath, path, NULL);
					}
				}
			}
		}
	
	if (ms_strstr (wiiload.filename, ".wad"))
		{
		int i;
		
		for (i = 0; i < DEV_MAX; i++)
			{
			if (devices_Get(i)) 
				{
				Video_WaitPanel (TEX_HGL, 0, "Please wait...|Copying file...");
				
				sprintf (path, "%s://wad", devices_Get(i));
				fsop_MakeFolder (path);
				
				sprintf (path, "%s://wad/%s", devices_Get(i), wiiload.filename);
				if (fsop_CopyFile (wiiload.fullpath, path, 0))
					{
					char title[256];
					
					sprintf (title, "wiiload: your wad has been copied to\n'%s'", path);
					grlib_menu (0, title, "  OK  ");
					break;
					}
				else
					{
					char title[256];
					
					sprintf (title, "wiiload: there was an error copying\n'%s'\nPlese check free space on device", path);
					grlib_menu (0, title, "  OK  ");
					}
				}
			}
		
		wiiload.status = WIILOAD_IDLE;
		
		Debug ("returning false...");
		return false; // We doesn't want run anything
		}
		
	return true;
	}
