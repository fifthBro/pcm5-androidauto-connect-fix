/*
 * Android Auto Device State Repair Tool for Porsche PCM5 (MH2P) - Partition 1008 Fix
 *
 * Copyright (c) 2025 fifthBro (https://github.com/fifthBro/pcm5-androidauto-connect-fix)
 *
 * This tool fixes device states in partition 1008 that were incorrectly set to NATIVE_SELECTED
 * state due to the bug in DeviceManager$DeviceActivationRequestHandler.moveSelectionMarker().
 *
 * This file is licensed under CC BY-NC-SA 4.0.
 * https://creativecommons.org/licenses/by-nc-sa/4.0/
 * See the LICENSE file in the project root for full license text.
 * NOT FOR COMMERCIAL USE
 *
 * The bug caused device userAcceptState strings in partition 1008 to be set to "NATIVE_SELECTED"
 * instead of "DISCLAIMER_ACCEPTED", preventing Android Auto from working.
 *
 * Storage Format (Java serialization):
 *   [8 bytes: CRC32 as long (big-endian)]
 *   [4 bytes: version = 3]
 *   [4 bytes: device count]
 *   [for each device:]
 *     - deviceUniqueId (UTF-8 with 2-byte length prefix)
 *     - smartphoneType (UTF-8 with 2-byte length prefix)
 *     - boolean: has name
 *     - name (UTF-8 with 2-byte length prefix, if has name)
 *     - userAcceptState (UTF-8 with 2-byte length prefix) <- This is what we fix
 *     - wasDisclaimerPreviouslyAccepted (boolean)
 *     - storeUserAcceptState (boolean)
 *     - lastmode (4-byte int)
 *     - lastConnectionType (UTF-8 with 2-byte length prefix)
 *
 * Usage:
 *     fix_partition_1008_v2 [--list] [--dry-run] [--fix] [--db-path PATH]
 *
 * Compilation:
 *     gcc -o fix_partition_1008_v2 fix_partition_1008_v2.c -lsqlite3 -lz
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>
#include <sqlite3.h>

#define DEFAULT_DB_PATH "/mnt/persist_new/persistence/persistence.sqlite"
#define PARTITION_NAME 1008
#define DEVICE_LIST_KEY 1

/* Binary patterns to search and replace */
static const unsigned char PATTERN_NATIVE_SELECTED[] = {0x00, 0x0f, 'N', 'A', 'T', 'I', 'V', 'E', '_', 'S', 'E', 'L', 'E', 'C', 'T', 'E', 'D'};
static const unsigned char PATTERN_DISCLAIMER_ACCEPTED[] = {0x00, 0x13, 'D', 'I', 'S', 'C', 'L', 'A', 'I', 'M', 'E', 'R', '_', 'A', 'C', 'C', 'E', 'P', 'T', 'E', 'D'};
#define PATTERN_NATIVE_LEN 17
#define PATTERN_DISCLAIMER_LEN 21

typedef struct {
    int list_only;
    int dry_run;
    int do_fix;
    int no_backup;
    const char *db_path;
} args_t;

typedef struct {
    int position;
    char device_name[256];
} device_issue_t;

/* Calculate CRC32 the same way Java does */
static uint32_t calculate_crc32(const unsigned char *data, size_t len) {
    return crc32(0L, data, len);
}

/* Read 64-bit big-endian value */
static uint64_t read_be64(const unsigned char *buf) {
    return ((uint64_t)buf[0] << 56) | ((uint64_t)buf[1] << 48) |
           ((uint64_t)buf[2] << 40) | ((uint64_t)buf[3] << 32) |
           ((uint64_t)buf[4] << 24) | ((uint64_t)buf[5] << 16) |
           ((uint64_t)buf[6] << 8)  | ((uint64_t)buf[7]);
}

/* Write 64-bit big-endian value */
static void write_be64(unsigned char *buf, uint64_t val) {
    buf[0] = (val >> 56) & 0xff;
    buf[1] = (val >> 48) & 0xff;
    buf[2] = (val >> 40) & 0xff;
    buf[3] = (val >> 32) & 0xff;
    buf[4] = (val >> 24) & 0xff;
    buf[5] = (val >> 16) & 0xff;
    buf[6] = (val >> 8) & 0xff;
    buf[7] = val & 0xff;
}

/* Read 16-bit big-endian value */
static uint16_t read_be16(const unsigned char *buf) {
    return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

/* Read 32-bit big-endian value */
static uint32_t read_be32(const unsigned char *buf) {
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

/* Look up partition ID for partition 1008 */
static int lookup_partition_id(sqlite3 *db, int *partition_id_out) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT id, name, version FROM \"persistence-partitions\" WHERE name = ? OR name = ?";
    char name_str[16];
    int rc;

    sprintf(name_str, "%d", PARTITION_NAME);

    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, name_str, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, PARTITION_NAME);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        fprintf(stderr, "[ERROR] Partition %d not found in database\n", PARTITION_NAME);
        sqlite3_finalize(stmt);
        return -1;
    }

    *partition_id_out = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return 0;
}

/* Parse device names from blob for display purposes */
static int parse_device_names(const unsigned char *blob, size_t blob_len,
                               device_issue_t *devices, int max_devices) {
    size_t offset = 16; /* Skip header (8 bytes CRC32 + 4 bytes version + 4 bytes count) */
    uint32_t device_count;
    int parsed = 0;

    if (blob_len < 16) return 0;

    device_count = read_be32(blob + 12);

    for (uint32_t i = 0; i < device_count && parsed < max_devices; i++) {
        uint16_t str_len;
        int has_name;

        if (offset + 2 > blob_len) break;

        /* Skip deviceUniqueId */
        str_len = read_be16(blob + offset);
        offset += 2 + str_len;
        if (offset > blob_len) break;

        /* Skip smartphoneType */
        str_len = read_be16(blob + offset);
        offset += 2 + str_len;
        if (offset + 1 > blob_len) break;

        /* Read name */
        has_name = blob[offset] != 0;
        offset += 1;

        if (has_name) {
            if (offset + 2 > blob_len) break;
            str_len = read_be16(blob + offset);
            offset += 2;
            if (offset + str_len > blob_len) break;

            /* Copy device name */
            size_t copy_len = str_len < 255 ? str_len : 255;
            memcpy(devices[parsed].device_name, blob + offset, copy_len);
            devices[parsed].device_name[copy_len] = '\0';
            offset += str_len;
        } else {
            strcpy(devices[parsed].device_name, "unknown");
        }

        /* Store userAcceptState position */
        devices[parsed].position = offset;

        if (offset + 2 > blob_len) break;
        str_len = read_be16(blob + offset);
        offset += 2 + str_len;
        if (offset > blob_len) break;

        /* Skip rest of device data */
        offset += 1 + 1 + 4; /* wasDisclaimer + storeState + lastmode */
        if (offset + 2 > blob_len) break;
        str_len = read_be16(blob + offset);
        offset += 2 + str_len; /* lastConnectionType */

        parsed++;
    }

    return parsed;
}

/* Find all occurrences of NATIVE_SELECTED pattern */
static int find_issues(const unsigned char *blob, size_t blob_len,
                       device_issue_t *issues, int max_issues) {
    int count = 0;
    device_issue_t parsed_devices[32];
    int parsed_count;

    /* Parse device names for better reporting */
    parsed_count = parse_device_names(blob, blob_len, parsed_devices, 32);

    /* Find all NATIVE_SELECTED patterns */
    for (size_t pos = 0; pos + PATTERN_NATIVE_LEN <= blob_len && count < max_issues; pos++) {
        if (memcmp(blob + pos, PATTERN_NATIVE_SELECTED, PATTERN_NATIVE_LEN) == 0) {
            issues[count].position = pos;

            /* Find matching device name */
            strcpy(issues[count].device_name, "unknown");
            for (int i = 0; i < parsed_count; i++) {
                if (parsed_devices[i].position == pos) {
                    strcpy(issues[count].device_name, parsed_devices[i].device_name);
                    break;
                }
            }

            count++;
        }
    }

    return count;
}

/* Create backup of database */
static int create_backup(const char *db_path) {
    char backup_path[512];
    char cmd[1024];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);

    strftime(backup_path, sizeof(backup_path),
             "%Y%m%d_%H%M%S", tm_info);
    snprintf(cmd, sizeof(cmd), "cp \"%s\" \"%s.backup_%s\"",
             db_path, db_path, backup_path);

    if (system(cmd) != 0) {
        fprintf(stderr, "[ERROR] Failed to create backup\n");
        return -1;
    }

    printf("\n[INFO] Created backup: %s.backup_%s\n\n", db_path, backup_path);
    return 0;
}

/* Main fix function */
static int fix_database(args_t *args) {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    int partition_id;
    const unsigned char *value_blob;
    size_t blob_len;
    int rc;
    device_issue_t issues[32];
    int issue_count;

    /* Open database */
    rc = sqlite3_open(args->db_path, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Cannot open database: %s\n", sqlite3_errmsg(db));
        return 1;
    }

    /* Look up partition ID */
    if (lookup_partition_id(db, &partition_id) != 0) {
        sqlite3_close(db);
        return 1;
    }

    printf("[INFO] Found partition %d with ID: %d\n\n", PARTITION_NAME, partition_id);

    /* Get device list blob */
    const char *sql = "SELECT value FROM \"persistence-data\" WHERE partition = ? AND key = ?";
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Failed to prepare statement: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_int(stmt, 1, partition_id);
    sqlite3_bind_int(stmt, 2, DEVICE_LIST_KEY);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        printf("[INFO] No device list found (Key %d not present)\n", DEVICE_LIST_KEY);
        printf("[INFO] This is normal if no devices have been paired yet\n");
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 0;
    }

    value_blob = sqlite3_column_blob(stmt, 0);
    blob_len = sqlite3_column_bytes(stmt, 0);

    printf("[INFO] Found device list (Key %d): %zu bytes\n", DEVICE_LIST_KEY, blob_len);

    /* Validate current CRC32 */
    if (blob_len < 8) {
        fprintf(stderr, "[ERROR] Blob too small (%zu bytes), expected at least 8 bytes\n", blob_len);
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        return 1;
    }

    uint64_t stored_crc32 = read_be64(value_blob);
    uint32_t calculated_crc32 = calculate_crc32(value_blob + 8, blob_len - 8);

    printf("[INFO] Stored CRC32:     0x%08x\n", (uint32_t)stored_crc32);
    printf("[INFO] Calculated CRC32: 0x%08x\n", calculated_crc32);

    if (stored_crc32 != calculated_crc32) {
        printf("[WARN] CRC32 mismatch detected! This blob may have been corrupted.\n");
        printf("[WARN] System will reject this data on next boot.\n");
    } else {
        printf("[INFO] CRC32 valid\n");
    }

    /* Copy blob to modifiable buffer */
    unsigned char *blob_copy = malloc(blob_len);
    memcpy(blob_copy, value_blob, blob_len);

    sqlite3_finalize(stmt);

    /* Find all issues */
    issue_count = find_issues(blob_copy, blob_len, issues, 32);

    if (issue_count == 0) {
        printf("[SUCCESS] No NATIVE_SELECTED states found - all devices are OK!\n");
        free(blob_copy);
        sqlite3_close(db);
        return 0;
    }

    printf("[INFO] Found %d device(s) with NATIVE_SELECTED state\n", issue_count);
    for (int i = 0; i < issue_count; i++) {
        printf("  - Position %d: %s\n", issues[i].position, issues[i].device_name);
    }
    printf("\n");

    if (args->list_only) {
        printf("[LIST] Devices that need fixing:\n");
        for (int i = 0; i < issue_count; i++) {
            printf("  - %s: NATIVE_SELECTED -> DISCLAIMER_ACCEPTED\n", issues[i].device_name);
        }
        free(blob_copy);
        sqlite3_close(db);
        return issue_count;
    }

    /* Calculate new blob size */
    size_t new_blob_len = blob_len + (issue_count * (PATTERN_DISCLAIMER_LEN - PATTERN_NATIVE_LEN));
    unsigned char *new_blob = malloc(new_blob_len);

    /* Build new blob with replacements (process in reverse order to maintain positions) */
    size_t write_pos = 0;
    size_t read_pos = 0;

    while (read_pos < blob_len) {
        /* Check if we're at an issue position */
        int at_issue = 0;
        for (int i = 0; i < issue_count; i++) {
            if (read_pos == issues[i].position) {
                at_issue = 1;
                break;
            }
        }

        if (at_issue) {
            /* Replace NATIVE_SELECTED with DISCLAIMER_ACCEPTED */
            memcpy(new_blob + write_pos, PATTERN_DISCLAIMER_ACCEPTED, PATTERN_DISCLAIMER_LEN);
            write_pos += PATTERN_DISCLAIMER_LEN;
            read_pos += PATTERN_NATIVE_LEN;
        } else {
            new_blob[write_pos++] = blob_copy[read_pos++];
        }
    }

    /* Recalculate CRC32 for the modified payload */
    uint32_t new_crc32 = calculate_crc32(new_blob + 8, new_blob_len - 8);
    write_be64(new_blob, new_crc32);

    if (args->dry_run) {
        printf("[DRY-RUN] Would fix the following:\n");
        for (int i = 0; i < issue_count; i++) {
            printf("  - %s: NATIVE_SELECTED -> DISCLAIMER_ACCEPTED\n", issues[i].device_name);
        }
        printf("\n");
        printf("[DRY-RUN] Blob size: %zu -> %zu bytes (%+zd bytes)\n",
               blob_len, new_blob_len, new_blob_len - blob_len);
        printf("[DRY-RUN] New CRC32 would be: 0x%08x\n", new_crc32);

        free(blob_copy);
        free(new_blob);
        sqlite3_close(db);
        return 0;
    }

    if (!args->do_fix) {
        fprintf(stderr, "[ERROR] Corrupted states found but --fix not specified\n");
        free(blob_copy);
        free(new_blob);
        sqlite3_close(db);
        return 1;
    }

    /* Create backup */
    if (!args->no_backup) {
        if (create_backup(args->db_path) != 0) {
            fprintf(stderr, "[ERROR] Aborting for safety\n");
            free(blob_copy);
            free(new_blob);
            sqlite3_close(db);
            return 1;
        }
    }

    /* Update database */
    const char *update_sql = "UPDATE \"persistence-data\" SET value = ? WHERE partition = ? AND key = ?";
    rc = sqlite3_prepare_v2(db, update_sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "[ERROR] Failed to prepare update: %s\n", sqlite3_errmsg(db));
        free(blob_copy);
        free(new_blob);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_bind_blob(stmt, 1, new_blob, new_blob_len, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, partition_id);
    sqlite3_bind_int(stmt, 3, DEVICE_LIST_KEY);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "[ERROR] Failed to update database: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        free(blob_copy);
        free(new_blob);
        sqlite3_close(db);
        return 1;
    }

    sqlite3_finalize(stmt);

    printf("[SUCCESS] Fixed %d device(s) in device list\n", issue_count);
    for (int i = 0; i < issue_count; i++) {
        printf("  - %s: NATIVE_SELECTED -> DISCLAIMER_ACCEPTED\n", issues[i].device_name);
    }
    printf("\n");
    printf("[INFO] Blob size: %zu -> %zu bytes (%+zd bytes)\n",
           blob_len, new_blob_len, new_blob_len - blob_len);
    printf("[INFO] Updated CRC32: 0x%08x -> 0x%08x\n", (uint32_t)stored_crc32, new_crc32);
    printf("[INFO] Database changes committed\n");
    printf("[INFO] Android Auto should now work after reconnecting affected phones\n");

    free(blob_copy);
    free(new_blob);
    sqlite3_close(db);
    return 0;
}

static void print_usage(const char *progname) {
    printf("Usage: %s [--list] [--dry-run] [--fix] [--db-path PATH] [--no-backup]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("  --list        List device states (no changes)\n");
    printf("  --dry-run     Preview what would be changed (no changes)\n");
    printf("  --fix         Actually apply the fix (modifies database)\n");
    printf("  --db-path     Path to persistence database (default: %s)\n", DEFAULT_DB_PATH);
    printf("  --no-backup   Skip backup creation\n");
}

int main(int argc, char *argv[]) {
    args_t args = {0};
    args.db_path = DEFAULT_DB_PATH;
    int action_count = 0;

    /* Parse arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--list") == 0) {
            args.list_only = 1;
            action_count++;
        } else if (strcmp(argv[i], "--dry-run") == 0) {
            args.dry_run = 1;
            action_count++;
        } else if (strcmp(argv[i], "--fix") == 0) {
            args.do_fix = 1;
            action_count++;
        } else if (strcmp(argv[i], "--no-backup") == 0) {
            args.no_backup = 1;
        } else if (strcmp(argv[i], "--db-path") == 0) {
            if (i + 1 < argc) {
                args.db_path = argv[++i];
            } else {
                fprintf(stderr, "[ERROR] --db-path requires an argument\n");
                return 1;
            }
        } else {
            fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (action_count == 0) {
        fprintf(stderr, "[ERROR] No action specified. Use --list, --dry-run, or --fix\n");
        print_usage(argv[0]);
        return 1;
    }

    if (action_count > 1) {
        fprintf(stderr, "[ERROR] Only one action can be specified\n");
        return 1;
    }

    printf("======================================================================\n");
    printf("Android Auto Device State Repair Tool\n");
    printf("Porsche PCM5 (MH2P) - Partition 1008 Fix\n");
    printf("Copyright (c) 2025 fifthBro\n");
    printf("https://github.com/fifthBro/pcm5-androidauto-connect-fix\n");
    printf("\n");
    printf("This file is licensed under CC BY-NC-SA 4.0.\n");
    printf("https://creativecommons.org/licenses/by-nc-sa/4.0/\n");
    printf("See the LICENSE file in the project root for full license text.\n");
    printf("NOT FOR COMMERCIAL USE\n");
    printf("======================================================================\n");
    printf("Database: %s\n", args.db_path);
    if (args.list_only) {
        printf("Mode: LIST (show corrupted devices)\n");
    } else if (args.dry_run) {
        printf("Mode: DRY-RUN (preview changes)\n");
    } else if (args.do_fix) {
        printf("Mode: FIX (will modify database)\n");
    }
    printf("\n");

    int result = fix_database(&args);

    printf("\n");
    printf("======================================================================\n");
    printf("SUMMARY\n");
    printf("======================================================================\n");
    if (result == 0) {
        printf("Devices actually fixed: %s\n", args.do_fix ? "see above" : "0");
        printf("Errors encountered: 0\n");
        if (args.do_fix) {
            printf("\n[INFO] Database has been modified.\n");
        }
    } else {
        printf("Exit code: %d\n", result);
    }
    printf("\n");
    printf("======================================================================\n");
    printf("\n");

    return result;
}
