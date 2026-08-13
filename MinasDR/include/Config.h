/**
 * ============================================================================
 * Project: MinasDR (Data Receiver)
 * File: Config.h
 * Description: Configuration constants for SD_MMC pins.
 * ============================================================================
 */

#ifndef CONFIG_H
#define CONFIG_H

 // --- SD_MMC Pin Definitions (Matches your original code) ---
#define SD_MMC_CMD 15
#define SD_MMC_CLK 14
#define SD_MMC_D0  2

// Persistent Master Log File for ML Dataset Collection
#define MASTER_LOG_FILE "/minas_master_dataset.csv"

#endif // CONFIG_H
