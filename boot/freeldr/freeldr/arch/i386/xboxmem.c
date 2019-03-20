/*
 *  FreeLoader
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Note: much of this code was based on knowledge and/or code developed
 * by the Xbox Linux group: http://www.xbox-linux.org
 */


#include <freeldr.h>
#include <arch/pc/x86common.h>

#include <debug.h>

DBG_DEFAULT_CHANNEL(MEMORY);

static ULONG InstalledMemoryMb = 0;
static ULONG AvailableMemoryMb = 0;

#define TEST_SIZE     0x200
#define TEST_PATTERN1 0xaa
#define TEST_PATTERN2 0x55

VOID
XboxMemInit(VOID)
{
  UCHAR ControlRegion[TEST_SIZE];
  PVOID MembaseTop = (PVOID)(64 * 1024 * 1024);
  PVOID MembaseLow = (PVOID)0;

  (*(PULONG)(0xfd000000 + 0x100200)) = 0x03070103 ;
  (*(PULONG)(0xfd000000 + 0x100204)) = 0x11448000 ;

  WRITE_PORT_ULONG((ULONG*) 0xcf8, CONFIG_CMD(0, 0, 0x84));
  WRITE_PORT_ULONG((ULONG*) 0xcfc, 0x7ffffff);             /* Prep hardware for 128 Mb */

  InstalledMemoryMb = 64;
  memset(ControlRegion, TEST_PATTERN1, TEST_SIZE);
  memset(MembaseTop, TEST_PATTERN1, TEST_SIZE);
  __wbinvd();

  if (0 == memcmp(MembaseTop, ControlRegion, TEST_SIZE))
    {
      /* Looks like there is memory .. maybe a 128MB box */
      memset(ControlRegion, TEST_PATTERN2, TEST_SIZE);
      memset(MembaseTop, TEST_PATTERN2, TEST_SIZE);
      __wbinvd();
      if (0 == memcmp(MembaseTop, ControlRegion, TEST_SIZE))
        {
          /* Definitely looks like there is memory */
          if (0 == memcmp(MembaseLow, ControlRegion, TEST_SIZE))
            {
              /* Hell, we find the Test-string at 0x0 too ! */
              InstalledMemoryMb = 64;
            }
          else
            {
              InstalledMemoryMb = 128;
            }
        }
    }

  /* Set hardware for amount of memory detected */
  WRITE_PORT_ULONG((ULONG*) 0xcf8, CONFIG_CMD(0, 0, 0x84));
  WRITE_PORT_ULONG((ULONG*) 0xcfc, InstalledMemoryMb * 1024 * 1024 - 1);

  AvailableMemoryMb = InstalledMemoryMb;
}

#if 0
FREELDR_MEMORY_DESCRIPTOR XboxMemoryMap[2];

PFREELDR_MEMORY_DESCRIPTOR
XboxMemGetMemoryMap(ULONG *MemoryMapSize)
{
      /* Available RAM block */
      XboxMemoryMap[0].BasePage = 0x100000;
      XboxMemoryMap[0].PageCount = (AvailableMemoryMb -1) * 1024 * 1024 / MM_PAGE_SIZE;
      XboxMemoryMap[0].MemoryType = LoaderFree;

      /* Video memory */
      XboxMemoryMap[1].BasePage = AvailableMemoryMb * 1024 * 1024 / MM_PAGE_SIZE;
      XboxMemoryMap[1].PageCount = (InstalledMemoryMb - AvailableMemoryMb) * 1024 * 1024 / MM_PAGE_SIZE;
      XboxMemoryMap[1].MemoryType = LoaderFirmwarePermanent;

  *MemoryMapSize = 2;

  return XboxMemoryMap;
}
#endif

PVOID
XboxMemReserveMemory(ULONG MbToReserve)
{
  if (0 == InstalledMemoryMb)
    {
      /* Hmm, seems we're not initialized yet */
      XboxMemInit();
    }

  if (AvailableMemoryMb < MbToReserve)
    {
      /* Can't satisfy the request */
      return NULL;
    }

  AvailableMemoryMb -= MbToReserve;

  /* Top of available memory points to the space just reserved */
  return (PVOID) (AvailableMemoryMb * 1024 * 1024);
}




// DBG_DEFAULT_CHANNEL(MEMORY);

#define ULONGLONG_ALIGN_DOWN_BY(size, align) \
    ((ULONGLONG)(size) & ~((ULONGLONG)(align) - 1))

#define ULONGLONG_ALIGN_UP_BY(size, align) \
    (ULONGLONG_ALIGN_DOWN_BY(((ULONGLONG)(size) + align - 1), align))

#define MAX_BIOS_DESCRIPTORS 80ul
FREELDR_MEMORY_DESCRIPTOR PcMemoryMap[MAX_BIOS_DESCRIPTORS + 1];
ULONG PcMapCount;

ULONG
AddMemoryDescriptor(
    IN OUT PFREELDR_MEMORY_DESCRIPTOR List,
    IN ULONG MaxCount,
    IN PFN_NUMBER BasePage,
    IN PFN_NUMBER PageCount,
    IN TYPE_OF_MEMORY MemoryType);

static
VOID
ReserveMemory(
    ULONG_PTR BaseAddress,
    SIZE_T Size,
    TYPE_OF_MEMORY MemoryType,
    PCHAR Usage)
{
    ULONG_PTR BasePage, PageCount;
    ULONG i;

    BasePage = BaseAddress / PAGE_SIZE;
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(BaseAddress, Size);

    for (i = 0; i < PcMapCount; i++)
    {
        /* Check for conflicting descriptor */
        if ((PcMemoryMap[i].BasePage < BasePage + PageCount) &&
            (PcMemoryMap[i].BasePage + PcMemoryMap[i].PageCount > BasePage))
        {
            /* Check if the memory is free */
            if (PcMemoryMap[i].MemoryType != LoaderFree)
            {
                FrLdrBugCheckWithMessage(
                    MEMORY_INIT_FAILURE,
                    __FILE__,
                    __LINE__,
                    "Failed to reserve memory in the range 0x%Ix - 0x%Ix for %s",
                    BaseAddress,
                    Size,
                    Usage);
            }
        }
    }

    /* Add the memory descriptor */
    PcMapCount = AddMemoryDescriptor(PcMemoryMap,
                                     MAX_BIOS_DESCRIPTORS,
                                     BasePage,
                                     PageCount,
                                     MemoryType);
}

static
VOID
SetMemory(
    ULONG_PTR BaseAddress,
    SIZE_T Size,
    TYPE_OF_MEMORY MemoryType)
{
    ULONG_PTR BasePage, PageCount;

    BasePage = BaseAddress / PAGE_SIZE;
    PageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(BaseAddress, Size);

    /* Add the memory descriptor */
    PcMapCount = AddMemoryDescriptor(PcMemoryMap,
                                     MAX_BIOS_DESCRIPTORS,
                                     BasePage,
                                     PageCount,
                                     MemoryType);
}

PFREELDR_MEMORY_DESCRIPTOR
XboxMemGetMemoryMap(ULONG *MemoryMapSize)
{
    ULONG i;

    TRACE("XboxMemGetMemoryMap()\n");

    // XboxMemCheckUsableMemorySize();

    /* Setup some protected ranges */
    SetMemory(0x000000, 0x01000, LoaderFirmwarePermanent); // Realmode IVT / BDA
    SetMemory(0x0A0000, 0x50000, LoaderFirmwarePermanent); // Video memory
    SetMemory(0x0F0000, 0x10000, LoaderSpecialMemory); // ROM
    SetMemory(0xFFF000, 0x01000, LoaderSpecialMemory); // unusable memory (do we really need this?)

    /* Reserve some static ranges for freeldr */
    ReserveMemory(0x1000, STACKLOW - 0x1000, LoaderFirmwareTemporary, "BIOS area");
    ReserveMemory(STACKLOW, STACKADDR - STACKLOW, LoaderOsloaderStack, "FreeLdr stack");
    ReserveMemory(FREELDR_BASE, FrLdrImageSize, LoaderLoadedProgram, "FreeLdr image");

    SetMemory(
      FREELDR_BASE + FrLdrImageSize,
      (AvailableMemoryMb * 1024 * 1024) - (FREELDR_BASE + FrLdrImageSize),
      LoaderFree);

    ReserveMemory(
      AvailableMemoryMb * 1024 * 1024,
      (InstalledMemoryMb-AvailableMemoryMb) * 1024 * 1024,
      LoaderFirmwarePermanent,
      "Video RAM");


    /* Default to 1 page above freeldr for the disk read buffer */
    DiskReadBuffer = (PUCHAR)ALIGN_UP_BY(FREELDR_BASE + FrLdrImageSize, PAGE_SIZE);
    DiskReadBufferSize = PAGE_SIZE;

    /* Scan for free range above freeldr image */
    for (i = 0; i < PcMapCount; i++)
    {
        if ((PcMemoryMap[i].BasePage > (FREELDR_BASE / PAGE_SIZE)) &&
            (PcMemoryMap[i].MemoryType == LoaderFree))
        {
            /* Use this range for the disk read buffer */
            DiskReadBuffer = (PVOID)(PcMemoryMap[i].BasePage * PAGE_SIZE);
            DiskReadBufferSize = min(PcMemoryMap[i].PageCount * PAGE_SIZE,
                                     MAX_DISKREADBUFFER_SIZE);
            break;
        }
    }

    TRACE("DiskReadBuffer=%p, DiskReadBufferSize=%lx\n",
          DiskReadBuffer, DiskReadBufferSize);

    /* Now reserve the range for the disk read buffer */
    ReserveMemory((ULONG_PTR)DiskReadBuffer,
                  DiskReadBufferSize,
                  LoaderFirmwareTemporary,
                  "Disk read buffer");

    TRACE("Dumping resulting memory map:\n");
    for (i = 0; i < PcMapCount; i++)
    {
        TRACE("BasePage=0x%lx, PageCount=0x%lx, Type=%s\n",
              PcMemoryMap[i].BasePage,
              PcMemoryMap[i].PageCount,
              MmGetSystemMemoryMapTypeString(PcMemoryMap[i].MemoryType));
    }

    *MemoryMapSize = PcMapCount;
    return PcMemoryMap;
}

















/* EOF */
