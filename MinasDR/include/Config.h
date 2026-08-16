/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: Config.h
 * Description: Configuration constants for SD_MMC pins.
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

// SD_MMC 1-bit wiring for MinasDR.
#define SD_MMC_CMD 15
#define SD_MMC_CLK 14
#define SD_MMC_D0  2

// The receiver creates trial_XXXX_owner.csv and trial_XXXX_nonowner.csv.
#define TRIAL_FILE_PREFIX "/trial_"

#endif // CONFIG_H
