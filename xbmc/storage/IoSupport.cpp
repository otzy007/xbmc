/*
* XBMC Media Center
* Copyright (c) 2002 d7o3g4q and RUNTiME
* Portions Copyright (c) by the authors of ffmpeg and xvid
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// IoSupport.cpp: implementation of the CIoSupport class.
//
//////////////////////////////////////////////////////////////////////

#include "system.h"
#include "IoSupport.h"
#include "utils/log.h"
#ifdef _WIN32
#include "my_ntddcdrm.h"
#include "WIN32Util.h"
#include "utils/CharsetConverter.h"
#endif
#if defined (_LINUX) && !defined(TARGET_DARWIN) && !defined(__FreeBSD__)
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/cdrom.h>
#endif
#if defined(TARGET_DARWIN)
#include <sys/param.h>
#include <mach-o/dyld.h>
#if defined(TARGET_DARWIN_OSX)
#include <IOKit/IOKitLib.h>
#include <IOKit/IOBSD.h>
#include <IOKit/storage/IOCDTypes.h>
#include <IOKit/storage/IODVDTypes.h>
#include <IOKit/storage/IOMedia.h>
#include <IOKit/storage/IOCDMedia.h>
#include <IOKit/storage/IODVDMedia.h>
#include <IOKit/storage/IOCDMediaBSDClient.h>
#include <IOKit/storage/IODVDMediaBSDClient.h>
#include <IOKit/storage/IOStorageDeviceCharacteristics.h>
#endif
#endif
#ifdef __FreeBSD__
#include <sys/syslimits.h>
#endif
#include "cdioSupport.h"
#include "filesystem/iso9660.h"
#include "MediaManager.h"
#ifdef _LINUX
#include "XHandle.h"
#endif


PVOID CIoSupport::m_rawXferBuffer;

HRESULT CIoSupport::CloseTray()
{
#ifdef HAS_DVD_DRIVE
#if defined(TARGET_DARWIN)
  // FIXME...
#elif defined(__FreeBSD__)
  // NYI
#elif defined(_LINUX)
  char* dvdDevice = CLibcdio::GetInstance()->GetDeviceFileName();
  if (strlen(dvdDevice) != 0)
  {
    int fd = open(dvdDevice, O_RDONLY | O_NONBLOCK);
    if (fd >= 0)
    {
      ioctl(fd, CDROMCLOSETRAY, 0);
      close(fd);
    }
  }
#elif defined(_WIN32)
  return CWIN32Util::CloseTray();
#endif
#endif
  return S_OK;
}

DWORD CIoSupport::GetTrayState()
{
#if defined(_LINUX) || defined(_WIN32)
  return g_mediaManager.GetDriveStatus();
#else
  return DRIVE_NOT_READY;
#endif
}

HRESULT CIoSupport::ToggleTray()
{
#if defined(TARGET_WINDOWS)
  return CWIN32Util::ToggleTray();
#else
  if (GetTrayState() == TRAY_OPEN || GetTrayState() == DRIVE_OPEN)
    return CloseTray();
  else
    return EjectTray();
#endif
}

HANDLE CIoSupport::OpenCDROM()
{
  HANDLE hDevice = 0;

#ifdef HAS_DVD_DRIVE
#if defined(_LINUX)
  int fd = open(CLibcdio::GetInstance()->GetDeviceFileName(), O_RDONLY | O_NONBLOCK);
  hDevice = new CXHandle(CXHandle::HND_FILE);
  hDevice->fd = fd;
  hDevice->m_bCDROM = true;
#elif defined(_WIN32)
  hDevice = CreateFile(g_mediaManager.TranslateDevicePath("",true), GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING,
                       FILE_FLAG_RANDOM_ACCESS, NULL );
#else

  hDevice = CreateFile("\\\\.\\Cdrom0", GENERIC_READ, FILE_SHARE_READ,
                       NULL, OPEN_EXISTING,
                       FILE_FLAG_RANDOM_ACCESS, NULL );

#endif
#endif
  return hDevice;
}

void CIoSupport::AllocReadBuffer()
{
#ifndef _LINUX
  m_rawXferBuffer = GlobalAlloc(GPTR, RAW_SECTOR_SIZE);
#endif
}

void CIoSupport::FreeReadBuffer()
{
#ifndef _LINUX
  GlobalFree(m_rawXferBuffer);
#endif
}

INT CIoSupport::ReadSector(HANDLE hDevice, DWORD dwSector, LPSTR lpczBuffer)

{
  DWORD dwRead;
  DWORD dwSectorSize = 2048;

#if defined(TARGET_DARWIN) && defined(HAS_DVD_DRIVE)
  dk_cd_read_t cd_read;
  memset( &cd_read, 0, sizeof(cd_read) );

  cd_read.sectorArea  = kCDSectorAreaUser;
  cd_read.buffer      = lpczBuffer;

  cd_read.sectorType  = kCDSectorTypeMode1;
  cd_read.offset      = dwSector * kCDSectorSizeMode1;

  cd_read.bufferLength = 2048;

  if( ioctl(hDevice->fd, DKIOCCDREAD, &cd_read ) == -1 )
  {
    return -1;
  }
  return 2048;
#elif defined(_LINUX)
  if (hDevice->m_bCDROM)
  {
    int fd = hDevice->fd;

    // seek to requested sector
    off_t offset = (off_t)dwSector * (off_t)MODE1_DATA_SIZE;

    if (lseek(fd, offset, SEEK_SET) < 0)
    {
      CLog::Log(LOGERROR, "CD: ReadSector Request to read sector %d\n", (int)dwSector);
      CLog::Log(LOGERROR, "CD: ReadSector error: %s\n", strerror(errno));
      OutputDebugString("CD Read error\n");
      return (-1);
    }

    // read data block of this sector
    while (read(fd, lpczBuffer, MODE1_DATA_SIZE) < 0)
    {
      // read was interrupted - try again
      if (errno == EINTR)
        continue;

      // error reading sector
      CLog::Log(LOGERROR, "CD: ReadSector Request to read sector %d\n", (int)dwSector);
      CLog::Log(LOGERROR, "CD: ReadSector error: %s\n", strerror(errno));
      OutputDebugString("CD Read error\n");
      return (-1);
    }

    return MODE1_DATA_SIZE;
  }
#endif
  LARGE_INTEGER Displacement;
  Displacement.QuadPart = ((INT64)dwSector) * dwSectorSize;

  for (int i = 0; i < 5; i++)
  {
    SetFilePointer(hDevice, Displacement.u.LowPart, &Displacement.u.HighPart, FILE_BEGIN);

    if (ReadFile(hDevice, m_rawXferBuffer, dwSectorSize, &dwRead, NULL))
    {
      memcpy(lpczBuffer, m_rawXferBuffer, dwSectorSize);
      return dwRead;
    }
  }

  OutputDebugString("CD Read error\n");
  return -1;
}


INT CIoSupport::ReadSectorMode2(HANDLE hDevice, DWORD dwSector, LPSTR lpczBuffer)
{
#ifdef HAS_DVD_DRIVE
#if defined(TARGET_DARWIN)
  dk_cd_read_t cd_read;

  memset( &cd_read, 0, sizeof(cd_read) );

  cd_read.sectorArea = kCDSectorAreaUser;
  cd_read.buffer = lpczBuffer;

  cd_read.offset       = dwSector * kCDSectorSizeMode2Form2;
  cd_read.sectorType   = kCDSectorTypeMode2Form2;
  cd_read.bufferLength = kCDSectorSizeMode2Form2;

  if( ioctl( hDevice->fd, DKIOCCDREAD, &cd_read ) == -1 )
  {
    return -1;
  }
  return MODE2_DATA_SIZE;
#elif defined(__FreeBSD__)
  // NYI
#elif defined(_LINUX)
  if (hDevice->m_bCDROM)
  {
    int fd = hDevice->fd;
    int lba = (dwSector + CD_MSF_OFFSET) ;
    int m,s,f;
    union
    {
      struct cdrom_msf msf;
      char buffer[2356];
    } arg;

    // convert sector offset to minute, second, frame format
    // since that is what the 'ioctl' requires as input
    f = lba % CD_FRAMES;
    lba /= CD_FRAMES;
    s = lba % CD_SECS;
    lba /= CD_SECS;
    m = lba;

    arg.msf.cdmsf_min0 = m;
    arg.msf.cdmsf_sec0 = s;
    arg.msf.cdmsf_frame0 = f;

    int ret = ioctl(fd, CDROMREADMODE2, &arg);
    if (ret==0)
    {
      memcpy(lpczBuffer, arg.buffer, MODE2_DATA_SIZE); // don't think offset is needed here
      return MODE2_DATA_SIZE;
    }
    CLog::Log(LOGERROR, "CD: ReadSectorMode2 Request to read sector %d\n", (int)dwSector);
    CLog::Log(LOGERROR, "CD: ReadSectorMode2 error: %s\n", strerror(errno));
    CLog::Log(LOGERROR, "CD: ReadSectorMode2 minute %d, second %d, frame %d\n", m, s, f);
    OutputDebugString("CD Read error\n");
    return -1;
  }
#else
  DWORD dwBytesReturned;
  RAW_READ_INFO rawRead = {0};

  // Oddly enough, DiskOffset uses the Red Book sector size
  rawRead.DiskOffset.QuadPart = 2048 * dwSector;
  rawRead.SectorCount = 1;
  rawRead.TrackMode = XAForm2;


  for (int i = 0; i < 5; i++)
  {
    if ( DeviceIoControl( hDevice,
                          IOCTL_CDROM_RAW_READ,
                          &rawRead,
                          sizeof(RAW_READ_INFO),
                          m_rawXferBuffer,
                          RAW_SECTOR_SIZE,
                          &dwBytesReturned,
                          NULL ) != 0 )
    {
      memcpy(lpczBuffer, (char*)m_rawXferBuffer+MODE2_DATA_START, MODE2_DATA_SIZE);
      return MODE2_DATA_SIZE;
    }
    else
    {
      int iErr = GetLastError();
    }
  }
#endif
#endif
  return -1;
}

INT CIoSupport::ReadSectorCDDA(HANDLE hDevice, DWORD dwSector, LPSTR lpczBuffer)
{
  return -1;
}

VOID CIoSupport::CloseCDROM(HANDLE hDevice)
{
  CloseHandle(hDevice);
}

VOID CIoSupport::GetXbePath(char* szDest)
{

#if WIN32
  wchar_t szAppPathW[MAX_PATH] = L"";
  ::GetModuleFileNameW(0, szAppPathW, sizeof(szAppPathW)/sizeof(szAppPathW[0]) - 1);
  CStdStringW strPathW = szAppPathW;
  CStdString strPath;
  g_charsetConverter.wToUTF8(strPathW,strPath);
  strncpy(szDest,strPath.c_str(),strPath.length()+1);
#elif defined(TARGET_DARWIN)
  int      result = -1;
  char     given_path[2*MAXPATHLEN];
  uint32_t path_size = 2*MAXPATHLEN;

  result = _NSGetExecutablePath(given_path, &path_size);
  if (result == 0)
    realpath(given_path, szDest);
#else
  /* Get our PID and build the name of the link in /proc */
  pid_t pid = getpid();
  char linkname[64]; /* /proc/<pid>/exe */
  snprintf(linkname, sizeof(linkname), "/proc/%i/exe", pid);

  /* Now read the symbolic link */
  char buf[1024];
  int ret = readlink(linkname, buf, 1024);
  buf[ret] = 0;
	
  strcpy(szDest, buf);
#endif
}
