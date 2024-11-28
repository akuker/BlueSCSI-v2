/*
 *  ZuluSCSI
 *  Copyright (c) 2022 Rabbit Hole Computing
 *
 * Main program for initiator mode.
 */

#include "BlueSCSI_config.h"
#include "BlueSCSI_log.h"
#include "BlueSCSI_log_trace.h"
#include "BlueSCSI_initiator.h"
#include "BlueSCSI_usbbridge.h"
#include <BlueSCSI_platform.h>
#include <minIni.h>
#ifndef LIB_FREERTOS_KERNEL
#include "SdFat.h"
#else
#include <string.h>
#endif

#include <scsi2sd.h>
extern "C" {
#include <scsi.h>
}


/*************************************
 * High level initiator mode logic   *
 *************************************/

static struct {
    // Bitmap of all drives that have been imaged
    uint32_t drives_imaged;

    uint8_t initiator_id;

    // Is imaging a drive in progress, or are we scanning?
    bool imaging;

    // Information about currently selected drive
    int target_id;
    uint32_t sectorsize;
    uint32_t sectorcount;
    uint32_t sectorcount_all;
    uint32_t sectors_done;
    uint32_t max_sector_per_transfer;
    uint32_t badSectorCount;
    uint8_t ansiVersion;
    uint8_t maxRetryCount;
    uint8_t deviceType;

    // Retry information for sector reads.
    // If a large read fails, retry is done sector-by-sector.
    int retrycount;
    uint32_t failposition;
    bool ejectWhenDone;

    FsFile target_file;
} g_initiator_state;

// static struct {

//     // Information about currently selected drive
//     int target_id;
//     uint32_t sectorsize;
//     uint32_t sectorcount;
//     uint32_t sectorcount_all;
//     uint32_t sectors_done;
//     uint32_t max_sector_per_transfer;
//     uint32_t badSectorCount;
//     uint8_t ansiVersion;
//     uint8_t maxRetryCount;
//     uint8_t deviceType;

//     // Retry information for sector reads.
//     // If a large read fails, retry is done sector-by-sector.
//     int retrycount;
//     uint32_t failposition;

// } g_device_data;

// // This uses callbacks to run SD and SCSI transfers in parallel
// static struct {
//     uint32_t bytes_sd; // Number of bytes that have been transferred on SD card side
//     uint32_t bytes_sd_scheduled; // Number of bytes scheduled for transfer on SD card side
//     uint32_t bytes_scsi; // Number of bytes that have been scheduled for transfer on SCSI side
//     uint32_t bytes_scsi_done; // Number of bytes that have been transferred on SCSI side

//     uint32_t bytes_per_sector;
//     bool all_ok;
// } g_initiator_transfer;

// Initialization of initiator mode
void scsiUsbBridgeInit()
{
    scsiHostPhyReset();

#ifndef LIB_FREERTOS_KERNEL
    g_initiator_state.initiator_id = ini_getl("SCSI", "InitiatorID", 7, CONFIGFILE);
#else
    g_initiator_state.initiator_id = 7;
#endif

    if (g_initiator_state.initiator_id > 7)
    {
        log("InitiatorID set to illegal value in, ", CONFIGFILE, ", defaulting to 7");
        g_initiator_state.initiator_id = 7;
    } else
    {
        log_f("InitiatorID set to ID %d", g_initiator_state.initiator_id);
    }
#ifndef LIB_FREERTOS_KERNEL
    g_initiator_state.maxRetryCount = ini_getl("SCSI", "InitiatorMaxRetry", 5, CONFIGFILE);
#else
    g_initiator_state.maxRetryCount = 5;
#endif
    // treat initiator id as already imaged drive so it gets skipped
    g_initiator_state.drives_imaged = 1 << g_initiator_state.initiator_id;
    g_initiator_state.imaging = false;
    g_initiator_state.target_id = -1;
    g_initiator_state.sectorsize = 0;
    g_initiator_state.sectorcount = 0;
    g_initiator_state.sectors_done = 0;
    g_initiator_state.retrycount = 0;
    g_initiator_state.failposition = 0;
    g_initiator_state.max_sector_per_transfer = 512;
    g_initiator_state.ansiVersion = 0;
    g_initiator_state.badSectorCount = 0;
    g_initiator_state.deviceType = DEVICE_TYPE_DIRECT_ACCESS;
    g_initiator_state.ejectWhenDone = false;
}



// High level logic of the initiator mode
void scsiUsbBridgeMainLoop()
{
    SCSI_RELEASE_OUTPUTS();
    SCSI_ENABLE_INITIATOR();
    if (g_scsiHostPhyReset)
    {
        log("Executing BUS RESET after aborted command");
        scsiHostPhyReset();
    }

    if (!g_initiator_state.imaging)
    {
        // Scan for SCSI drives one at a time
        g_initiator_state.target_id = (g_initiator_state.target_id + 1) % 8;
        g_initiator_state.sectors_done = 0;
        g_initiator_state.retrycount = 0;
        g_initiator_state.max_sector_per_transfer = 512;
        g_initiator_state.badSectorCount = 0;
        g_initiator_state.ejectWhenDone = false;

        if (!(g_initiator_state.drives_imaged & (1 << g_initiator_state.target_id)))
        {
            delay_with_poll(1000);

            uint8_t inquiry_data[36] = {0};

            LED_ON();
            bool startstopok =
                scsiTestUnitReady(g_initiator_state.target_id) &&
                scsiStartStopUnit(g_initiator_state.target_id, true);

            bool readcapok = startstopok &&
                scsiInitiatorReadCapacity(g_initiator_state.target_id,
                                          &g_initiator_state.sectorcount,
                                          &g_initiator_state.sectorsize);

            bool inquiryok = startstopok &&
                scsiInquiry(g_initiator_state.target_id, inquiry_data);
            g_initiator_state.ansiVersion = inquiry_data[2] & 0x7;
            LED_OFF();

            uint64_t total_bytes = 0;
            if (readcapok)
            {
                log("SCSI ID ", g_initiator_state.target_id,
                    " capacity ", (int)g_initiator_state.sectorcount,
                    " sectors x ", (int)g_initiator_state.sectorsize, " bytes");
                log_f("SCSI-%d: Vendor: %.8s, Product: %.16s, Version: %.4s",
                    g_initiator_state.ansiVersion,
                    &inquiry_data[8],
                    &inquiry_data[16],
                    &inquiry_data[32]);

                // Check for well known ejectable media.
                if(strncmp((char*)(&inquiry_data[8]), "IOMEGA", 6) == 0 &&
                   strncmp((char*)(&inquiry_data[16]), "ZIP", 3) == 0)
                {
                    g_initiator_state.ejectWhenDone = true;
                }
                g_initiator_state.sectorcount_all = g_initiator_state.sectorcount;

                total_bytes = (uint64_t)g_initiator_state.sectorcount * g_initiator_state.sectorsize;
                log("Drive total size is ", (int)(total_bytes / (1024 * 1024)), " MiB");
#ifndef LIB_FREERTOS_KERNEL
                // TODO: sdcard support is not working in FreeRTOS
                if (total_bytes >= 0xFFFFFFFF && SD.fatType() != FAT_TYPE_EXFAT)
                {
                    // Note: the FAT32 limit is 4 GiB - 1 byte
                    log("Image files equal or larger than 4 GiB are only possible on exFAT filesystem");
                    log("Please reformat the SD card with exFAT format to image this drive.");
                    g_initiator_state.sectorsize = 0;
                    g_initiator_state.sectorcount = g_initiator_state.sectorcount_all = 0;
                }
#endif
                if(g_initiator_state.ansiVersion < 0x02)
                {
                    // this is a SCSI-1 drive, use READ6 and 256 bytes to be safe.
                    g_initiator_state.max_sector_per_transfer = 256;
                }
            }
            else if (startstopok)
            {
                log("SCSI ID ", g_initiator_state.target_id, " responds but ReadCapacity command failed");
                log("Possibly SCSI-1 drive? Attempting to read up to 1 GB.");
                g_initiator_state.sectorsize = 512;
                g_initiator_state.sectorcount = g_initiator_state.sectorcount_all = 2097152;
                g_initiator_state.max_sector_per_transfer = 128;
            }
            else
            {
                log("* No response from SCSI ID ", g_initiator_state.target_id);
                g_initiator_state.sectorsize = 0;
                g_initiator_state.sectorcount = g_initiator_state.sectorcount_all = 0;
            }

            if (inquiryok)
            {
                g_initiator_state.deviceType = inquiry_data[0] & 0x1F;
                if (g_initiator_state.deviceType == DEVICE_TYPE_CD)
                {
                    g_initiator_state.ejectWhenDone = true;
                }
                else if(g_initiator_state.deviceType != DEVICE_TYPE_DIRECT_ACCESS)
                {
                    log("Unhandled device type: ", g_initiator_state.deviceType, ". Handling it as Direct Access Device.");
                }
            }

            if (g_initiator_state.sectorcount > 0)
            {
#ifndef LIB_FREERTOS_KERNEL
                char filename[18] = "";
                int image_num = 0;
                uint64_t sd_card_free_bytes = (uint64_t)SD.vol()->freeClusterCount() * SD.vol()->bytesPerCluster();
                if(sd_card_free_bytes < total_bytes)
                {
                    log("SD Card only has ", (int)(sd_card_free_bytes / (1024 * 1024)), " MiB - not enough free space to image this drive!");
                    g_initiator_state.imaging = false;
                    return;
                }

                do {
                    sprintf(filename, "%s%d_imaged-%03d.%s",
                            (g_initiator_state.deviceType == DEVICE_TYPE_CD) ? "CD" : "HD",
                            g_initiator_state.target_id,
                            ++image_num,
                            (g_initiator_state.deviceType == DEVICE_TYPE_CD) ? "iso" : "hda");
                } while(SD.exists(filename));
                log("Imaging filename: ", filename, ".");
                g_initiator_state.target_file = SD.open(filename, O_WRONLY | O_CREAT | O_TRUNC);
                if (!g_initiator_state.target_file.isOpen())
                {
                    log("Failed to open file for writing: ", filename);
                    return;
                }

                if (SD.fatType() == FAT_TYPE_EXFAT)
                {
                    // Only preallocate on exFAT, on FAT32 preallocating can result in false garbage data in the
                    // file if write is interrupted.
                    log("Preallocating image file");
                    g_initiator_state.target_file.preAllocate((uint64_t)g_initiator_state.sectorcount * g_initiator_state.sectorsize);
                }

                log("Starting to copy drive data to ", filename);
#endif
                g_initiator_state.imaging = true;
            }
        }
    }
    else
    {
        // Copy sectors from SCSI drive to file
        if (g_initiator_state.sectors_done >= g_initiator_state.sectorcount)
        {
            scsiStartStopUnit(g_initiator_state.target_id, false);
            log("Finished imaging drive with id ", g_initiator_state.target_id);
            LED_OFF();

            if (g_initiator_state.sectorcount != g_initiator_state.sectorcount_all)
            {
                log("NOTE: Image size was limited to first 4 GiB due to SD card filesystem limit");
                log("Please reformat the SD card with exFAT format to image this drive fully");
            }

            if(g_initiator_state.badSectorCount != 0)
            {
                log_f("NOTE: There were %d bad sectors that could not be read off this drive.", g_initiator_state.badSectorCount);
            }

            if(!g_initiator_state.ejectWhenDone)
            {
                log("Marking this ID as imaged, wont ask it again.");
                g_initiator_state.drives_imaged |= (1 << g_initiator_state.target_id);
            }
            g_initiator_state.imaging = false;
#ifndef LIB_FREERTOS_KERNEL
            g_initiator_state.target_file.close();
#endif
            return;
        }

        scsiInitiatorUpdateLed();

        // How many sectors to read in one batch?
        uint32_t numtoread = g_initiator_state.sectorcount - g_initiator_state.sectors_done;
        if (numtoread > g_initiator_state.max_sector_per_transfer)
            numtoread = g_initiator_state.max_sector_per_transfer;

        // Retry sector-by-sector after failure
        if (g_initiator_state.sectors_done < g_initiator_state.failposition)
            numtoread = 1;

        uint32_t time_start = millis();
        bool status = scsiInitiatorReadDataToFile(g_initiator_state.target_id,
            g_initiator_state.sectors_done, numtoread, g_initiator_state.sectorsize,
            g_initiator_state.target_file);

        if (!status)
        {
            log("Failed to transfer ", numtoread, " sectors starting at ", (int)g_initiator_state.sectors_done);

            if (g_initiator_state.retrycount < g_initiator_state.maxRetryCount)
            {
                log("Retrying.. ", g_initiator_state.retrycount + 1, "/", (int)g_initiator_state.maxRetryCount);
                delay_with_poll(200);
                // This reset causes some drives to hang and seems to have no effect if left off.
                // scsiHostPhyReset();
                delay_with_poll(200);

                g_initiator_state.retrycount++;
#ifndef LIB_FREERTOS_KERNEL
                g_initiator_state.target_file.seek((uint64_t)g_initiator_state.sectors_done * g_initiator_state.sectorsize);
#endif

                if (g_initiator_state.retrycount > 1 && numtoread > 1)
                {
                    log("Multiple failures, retrying sector-by-sector");
                    g_initiator_state.failposition = g_initiator_state.sectors_done + numtoread;
                }
            }
            else
            {
                log("Retry limit exceeded, skipping one sector");
                g_initiator_state.retrycount = 0;
                g_initiator_state.sectors_done++;
                g_initiator_state.badSectorCount++;
    #ifndef LIB_FREERTOS_KERNEL
                g_initiator_state.target_file.seek((uint64_t)g_initiator_state.sectors_done * g_initiator_state.sectorsize);
    #endif
            }
        }
        else
        {
            g_initiator_state.retrycount = 0;
            g_initiator_state.sectors_done += numtoread;
#ifndef LIB_FREERTOS_KERNEL
            g_initiator_state.target_file.flush();
#endif

            int speed_kbps = numtoread * g_initiator_state.sectorsize / (millis() - time_start);
            log("SCSI read succeeded, sectors done: ",
                  (int)g_initiator_state.sectors_done, " / ", (int)g_initiator_state.sectorcount,
                  " speed ", speed_kbps, " kB/s - ",
                  (int)(100 * (int64_t)g_initiator_state.sectors_done / g_initiator_state.sectorcount), "%");
        }
    }
}



#ifdef LIB_FREERTOS_KERNEL
bool scsiUsbBridgeReadDataToFile(int target_id, uint32_t start_sector, uint32_t sectorcount, uint32_t sectorsize,
                                 int file)
#else
bool scsiUsbBridgeReadDataToFile(int target_id, uint32_t start_sector, uint32_t sectorcount, uint32_t sectorsize,
                                 FsFile &file)
#endif
{
    // int status = -1;

    // // Read6 command supports 21 bit LBA - max of 0x1FFFFF
    // // ref: https://www.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068j.pdf pg 134
    // if (g_initiator_state.ansiVersion < 0x02 || (start_sector < 0x1FFFFF && sectorcount <= 256))
    // {
    //     // Use READ6 command for compatibility with old SCSI1 drives
    //     uint8_t command[6] = {0x08,
    //         (uint8_t)(start_sector >> 16),
    //         (uint8_t)(start_sector >> 8),
    //         (uint8_t)start_sector,
    //         (uint8_t)sectorcount,
    //         0x00
    //     };

    //     // Start executing command, return in data phase
    //     status = scsiInitiatorRunCommand(target_id, command, sizeof(command), NULL, 0, NULL, 0, true);
    // }
    // else
    // {
    //     // Use READ10 command for larger number of blocks
    //     uint8_t command[10] = {0x28, 0x00,
    //         (uint8_t)(start_sector >> 24), (uint8_t)(start_sector >> 16),
    //         (uint8_t)(start_sector >> 8), (uint8_t)start_sector,
    //         0x00,
    //         (uint8_t)(sectorcount >> 8), (uint8_t)(sectorcount),
    //         0x00
    //     };

    //     // Start executing command, return in data phase
    //     status = scsiInitiatorRunCommand(target_id, command, sizeof(command), NULL, 0, NULL, 0, true);
    // }


    // if (status != 0)
    // {
    //     uint8_t sense_key;
    //     scsiRequestSense(target_id, &sense_key);

    //     log("scsiInitiatorReadDataToFile: READ failed: ", status, " sense key ", sense_key);
    //     scsiHostPhyRelease();
    //     return false;
    // }

    // SCSI_PHASE phase;

    // g_initiator_transfer.bytes_scsi = sectorcount * sectorsize;
    // g_initiator_transfer.bytes_per_sector = sectorsize;
    // g_initiator_transfer.bytes_sd = 0;
    // g_initiator_transfer.bytes_sd_scheduled = 0;
    // g_initiator_transfer.bytes_scsi_done = 0;
    // g_initiator_transfer.all_ok = true;

    // while (true)
    // {
    //     platform_poll();

    //     phase = (SCSI_PHASE)scsiHostPhyGetPhase();
    //     if (phase != DATA_IN && phase != BUS_BUSY)
    //     {
    //         break;
    //     }

    //     // Read next block from SCSI bus if buffer empty
    //     if (g_initiator_transfer.bytes_sd == g_initiator_transfer.bytes_scsi_done)
    //     {
    //         initiatorReadSDCallback(0);
    //     }
    //     else
    //     {
    //         // Write data to SD card and simultaneously read more from SCSI
    //         scsiInitiatorUpdateLed();
    //         scsiInitiatorWriteDataToSd(file, true);
    //     }
    // }

    // // Write any remaining buffered data
    // while (g_initiator_transfer.bytes_sd < g_initiator_transfer.bytes_scsi_done)
    // {
    //     platform_poll();
    //     scsiInitiatorWriteDataToSd(file, false);
    // }

    // if (g_initiator_transfer.bytes_sd != g_initiator_transfer.bytes_scsi)
    // {
    //     log("SCSI read from sector ", (int)start_sector, " was incomplete: expected ",
    //          (int)g_initiator_transfer.bytes_scsi, " got ", (int)g_initiator_transfer.bytes_sd, " bytes");
    //     g_initiator_transfer.all_ok = false;
    // }

    // while ((phase = (SCSI_PHASE)scsiHostPhyGetPhase()) != BUS_FREE)
    // {
    //     platform_poll();

    //     if (phase == MESSAGE_IN)
    //     {
    //         uint8_t dummy = 0;
    //         scsiHostRead(&dummy, 1);
    //     }
    //     else if (phase == MESSAGE_OUT)
    //     {
    //         uint8_t identify_msg = 0x80;
    //         scsiHostWrite(&identify_msg, 1);
    //     }
    //     else if (phase == STATUS)
    //     {
    //         uint8_t tmp = 0;
    //         scsiHostRead(&tmp, 1);
    //         status = tmp;
    //         debuglog("------ STATUS: ", tmp);
    //     }
    // }

    // scsiHostPhyRelease();

    // return status == 0 && g_initiator_transfer.all_ok;
    return true;
}



