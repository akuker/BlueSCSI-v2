// Defines the base characteristics of a disk that is attached to TinyUSB
// which uses the MSC (Mass Storage Class) USB interface to communicate
// with the host.
//
// Copyright (C) 2024 akuker
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

#include <vector>
#include <stdint.h>
#include <memory>
#include <string>
#include "scsi2sd.h"

#pragma once

extern "C"
{
    extern bool g_scsi_msc_mode;
    extern bool g_disable_usb_cdc;
    void msc_disk_init(void);
}

namespace USB
{

    class MscDisk
    {
    public:
        // Information about drive
        int target_id = -1;
        uint32_t sectorsize = 0;
        uint32_t sectorcount = 0;
        uint32_t sectorcount_all = 0;
        uint32_t sectors_done = 0;
        uint32_t max_sector_per_transfer = 512;
        uint32_t badSectorCount = 0;
        uint8_t ansiVersion = 0;
        uint8_t maxRetryCount = 0;
        int deviceType = (int)S2S_CFG_FIXED;
        uint8_t inquiry_data[36] = {0};

        std::string vendor_id = "TinyUSB";
        std::string product_id = "Mass Storage";
        std::string product_rev = "1.0";


        const std::string getVendorId() const
        {
            return vendor_id;
        }
        const std::string getProductId() const
        {
            return product_id;
        }
        const std::string getProductRev() const
        {
            return product_rev;
        }

        static const int RAM_DISK = -2;
        virtual ~MscDisk() = default;

        virtual bool Inquiry(bool refresh_required = false) = 0;
        virtual bool ReadCapacity(uint32_t *sectorcount, uint32_t *sectorsize) = 0;
        virtual bool IsWritable() = 0;
        virtual bool TestUnitReady() = 0;
        virtual bool RequestSense(uint8_t *sense_key) = 0;
        virtual bool StartStopUnit(uint8_t power_condition, bool start, bool load_eject) = 0;
        virtual uint32_t Read10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t buffersize) = 0;
        virtual uint32_t Write10(uint32_t lba, uint32_t offset, uint8_t *buffer, uint32_t buffersize) = 0;

        int32_t UnhandledScsiCommand(uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize);

        static std::shared_ptr<USB::MscDisk> GetMscDiskByScsiId(uint8_t target_id);
        static std::shared_ptr<USB::MscDisk> GetMscDiskByLun(uint8_t lun);
        static void AddMscDisk(std::shared_ptr<USB::MscDisk> disk)
        {
            DiskList.push_back(disk);
        }
        static std::vector<std::shared_ptr<MscDisk>> DiskList;

    protected:
        // Constructor can't be called directly, since this is an abstract class
        explicit MscDisk(uint8_t id) : target_id(id)
        {
        }
    };

}
