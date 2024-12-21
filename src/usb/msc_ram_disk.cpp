// Provides a RAM-backed MSC disk that can be connected to the host. This is
// intended to be very small, just big enough for a README.txt or to do
// basic testing.
//
// Copyright (C) 2024 akuker
// Copyright (c) 2019 Ha Thach (tinyusb.org)
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along
// with this program. If not, see <https://www.gnu.org/licenses/>.

#include <stdint.h>
#include <string.h>
#include "msc_disk.h"
#include "msc_ram_disk.h"

namespace USB
{

  MscRamDisk::MscRamDisk(bool is_writable) : MscDisk(RAM_DISK), is_writable_(is_writable)
  {
    // size_bytes_ = size_bytes / DISK_BLOCK_SIZE;
    sectorsize = DISK_BLOCK_SIZE;
    sectorcount = DISK_BLOCK_COUNT;
    ansiVersion = 0;
    deviceType = S2S_CFG_FIXED;
    target_id = MscDisk::RAM_DISK;
    // Initialize with a file system
    memcpy(ram_disk_, readme_disk_, sizeof(readme_disk_));
  }

  status_byte_t MscRamDisk::ReadCapacity(uint32_t *sectorcount, uint32_t *sectorsize, sense_key_type *sense_key)
  {
    *sectorcount = DISK_BLOCK_COUNT;
    *sectorsize = DISK_BLOCK_SIZE;
    if (sense_key != nullptr)
    {
      *sense_key = eNoSense; // No error
    }
    return status_byte_t::eGood;
  }
  bool MscRamDisk::IsWritable()
  {
    return is_writable_;
  }
  status_byte_t MscRamDisk::TestUnitReady(sense_key_type *sense_key)
  {
    if (sense_key != nullptr)
    {
      *sense_key = eNoSense; // No error
    }
    return status_byte_t::eGood;
  }
  status_byte_t MscRamDisk::RequestSense(sense_key_type *sense_key)
  {
    if (sense_key != nullptr)
    {
      *sense_key = eNoSense; // No error
    }
    return status_byte_t::eGood;
  }
  status_byte_t MscRamDisk::StartStopUnit(uint8_t power_condition, bool start, bool load_eject, sense_key_type *sense_key)
  {
    (void)power_condition;
    (void)start;
    (void)load_eject;
    if (sense_key != nullptr)
    {
      *sense_key = eNoSense; // No error
    }
    return status_byte_t::eGood;
  }
  uint32_t MscRamDisk::Read10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t buffersize)
  {
    if (lba >= DISK_BLOCK_COUNT)
      return -1;

    uint8_t const *addr = ram_disk_[lba] + offset;
    memcpy(buffer, addr, buffersize);

    return (int32_t)buffersize;
  }
  uint32_t MscRamDisk::Write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t buffersize)
  {

    // out of ramdisk
    if (lba >= DISK_BLOCK_COUNT)
      return -1;

    uint8_t *addr = ram_disk_[lba] + offset;
    memcpy(addr, buffer, buffersize);

    return (int32_t)buffersize;
  }

  static const char ram_disk[] = "RAM Disk";
  char *MscRamDisk::toString()
  {
    return (char *)ram_disk;
  }

//--------------------------------------------------------------------+
// LUN 0
//--------------------------------------------------------------------+
#define BLUESCSI_README_CONTENTS                                               \
  "Welcome to BlueSCSI!\n\r\n\rIf you're seeing this message, your "           \
  "BlueSCSI didn't detect any SCSI drives.\n\r\n\rPlease visit the following " \
  "URL for more info:\n\r       https://bluescsi.com/docs/Troubleshooting"

  const uint8_t MscRamDisk::readme_disk_[DISK_BLOCK_COUNT][DISK_BLOCK_SIZE] =
      {
          //------------- Block0: Boot Sector -------------//
          // byte_per_sector    = DISK_BLOCK_SIZE; fat12_sector_num_16  = DISK_BLOCK_NUM;
          // sector_per_cluster = 1; reserved_sectors = 1;
          // fat_num            = 1; fat12_root_entry_num = 16;
          // sector_per_fat     = 1; sector_per_track = 1; head_num = 1; hidden_sectors = 0;
          // drive_number       = 0x80; media_type = 0xf8; extended_boot_signature = 0x29;
          // filesystem_type    = "FAT12   "; volume_serial_number = 0x1234; volume_label = "TinyUSB 0  ";
          // FAT magic code at offset 510-511
          {
              0xEB, 0x3C, 0x90, 0x4D, 0x53, 0x44, 0x4F, 0x53, 0x35, 0x2E, 0x30, 0x00, 0x02, 0x01, 0x01, 0x00,
              0x01, 0x10, 0x00, 0x10, 0x00, 0xF8, 0x01, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x29, 0x34, 0x12, 0x00, 0x00, 'B', 'l', 'u', 'e', 'S',
              'C', 'S', 'I', ' ', ' ', ' ', 0x46, 0x41, 0x54, 0x31, 0x32, 0x20, 0x20, 0x20, 0x00, 0x00,

              // Zero up to 2 last bytes of FAT magic code
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x55, 0xAA},

          //------------- Block1: FAT12 Table -------------//
          {
              0xF8, 0xFF, 0xFF, 0xFF, 0x0F // // first 2 entries must be F8FF, third entry is cluster end of readme file
          },

          //------------- Block2: Root Directory -------------//
          {
              // first entry is volume label
              'B', 'l', 'u', 'e', 'S', 'C', 'S', 'I', ' ', ' ', ' ', 0x08, 0x00, 0x00, 0x00, 0x00,
              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4F, 0x6D, 0x65, 0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
              // second entry is readme file
              'R', 'E', 'A', 'D', '_', 'M', 'E', ' ', 'T', 'X', 'T', 0x20, 0x00, 0xC6, 0x52, 0x6D,
              0x65, 0x43, 0x65, 0x43, 0x00, 0x00, 0x88, 0x6D, 0x65, 0x43, 0x02, 0x00,
              sizeof(BLUESCSI_README_CONTENTS) - 1, 0x00, 0x00, 0x00 // readme's files size (4 Bytes)
          },

          //------------- Block3: Readme Content -------------//
          BLUESCSI_README_CONTENTS};
} // end USB namespace