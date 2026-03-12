/*
 * test_scsi.c - SCSI Mid-Layer Unit Tests
 *
 * Tests core SCSI data structures, CDB construction, and sense parsing.
 */

#include <kern/console.h>
#include <drivers/storage/scsi/scsi.h>
#include <stdio.h>
#include <string.h>
#include "tests.h"

/* Mock transport execute function */
static int mock_execute_calls = 0;
static int mock_execute_return = 0;

static int mock_execute(scsi_link_t *link, scsi_request_t *req) {
    (void)link;
    mock_execute_calls++;
    req->status = SCSI_STATUS_GOOD;
    req->data_xfer = req->data_len;
    return mock_execute_return;
}

static scsi_link_t mock_link = {
    .name = "mock",
    .execute = mock_execute,
    .reset_device = NULL,
    .reset_bus = NULL,
    .priv = NULL
};

void test_scsi(void) {
    char buf[128];
    int pass = 0, fail = 0;
    
    kprint("=== SCSI Mid-Layer Tests ===\n");
    
    /* Re-initialize SCSI layer for clean state */
    scsi_init();
    
    /*
     * Test 1: CDB construction - TEST UNIT READY
     */
    {
        uint8_t cdb[6];
        scsi_cdb_test_unit_ready(cdb);
        if (cdb[0] == SCSI_CMD_TEST_UNIT_READY && 
            cdb[1] == 0 && cdb[2] == 0 && cdb[3] == 0 && cdb[4] == 0 && cdb[5] == 0) {
            kprint("PASS: CDB TEST UNIT READY\n");
            pass++;
        } else {
            kprint("FAIL: CDB TEST UNIT READY\n");
            fail++;
        }
    }
    
    /*
     * Test 2: CDB construction - INQUIRY
     */
    {
        uint8_t cdb[6];
        scsi_cdb_inquiry(cdb, 36);
        if (cdb[0] == SCSI_CMD_INQUIRY && cdb[4] == 36) {
            kprint("PASS: CDB INQUIRY\n");
            pass++;
        } else {
            kprint("FAIL: CDB INQUIRY\n");
            fail++;
        }
    }

    /*
     * Test 2b: CDB construction - READ CAPACITY (10)
     */
    {
        uint8_t cdb[10];
        scsi_cdb_read_capacity_10(cdb);
        if (cdb[0] == SCSI_CMD_READ_CAPACITY_10 &&
            cdb[1] == 0 && cdb[2] == 0 && cdb[3] == 0 &&
            cdb[4] == 0 && cdb[5] == 0 && cdb[6] == 0 &&
            cdb[7] == 0 && cdb[8] == 0 && cdb[9] == 0) {
            kprint("PASS: CDB READ CAPACITY(10)\n");
            pass++;
        } else {
            kprint("FAIL: CDB READ CAPACITY(10)\n");
            fail++;
        }
    }
    
    /*
     * Test 3: CDB construction - READ(10)
     */
    {
        uint8_t cdb[10];
        scsi_cdb_read_10(cdb, 0x12345678, 0x00FF);
        if (cdb[0] == SCSI_CMD_READ_10 &&
            cdb[2] == 0x12 && cdb[3] == 0x34 && cdb[4] == 0x56 && cdb[5] == 0x78 &&
            cdb[7] == 0x00 && cdb[8] == 0xFF) {
            kprint("PASS: CDB READ(10) big-endian\n");
            pass++;
        } else {
            sprintf(buf, "FAIL: CDB READ(10) [%02x %02x %02x %02x %02x %02x]\n",
                    cdb[2], cdb[3], cdb[4], cdb[5], cdb[7], cdb[8]);
            kprint(buf);
            fail++;
        }
    }
    
    /*
     * Test 4: CDB construction - WRITE(10)
     */
    {
        uint8_t cdb[10];
        scsi_cdb_write_10(cdb, 0xAABBCCDD, 0x0102);
        if (cdb[0] == SCSI_CMD_WRITE_10 &&
            cdb[2] == 0xAA && cdb[3] == 0xBB && cdb[4] == 0xCC && cdb[5] == 0xDD &&
            cdb[7] == 0x01 && cdb[8] == 0x02) {
            kprint("PASS: CDB WRITE(10) big-endian\n");
            pass++;
        } else {
            kprint("FAIL: CDB WRITE(10) big-endian\n");
            fail++;
        }
    }
    
    /*
     * Test 5: Byte order helpers
     */
    {
        uint8_t data[4] = {0x12, 0x34, 0x56, 0x78};
        uint16_t v16 = scsi_be16(data);
        uint32_t v32 = scsi_be32(data);
        
        if (v16 == 0x1234 && v32 == 0x12345678) {
            kprint("PASS: scsi_be16/be32\n");
            pass++;
        } else {
            sprintf(buf, "FAIL: scsi_be16=%04x scsi_be32=%08x\n", v16, (unsigned)v32);
            kprint(buf);
            fail++;
        }
    }
    
    /*
     * Test 6: scsi_put_be16/be32
     */
    {
        uint8_t data[4] = {0, 0, 0, 0};
        scsi_put_be16(data, 0xABCD);
        scsi_put_be32(data, 0xDEADBEEF);
        
        if (data[0] == 0xDE && data[1] == 0xAD && data[2] == 0xBE && data[3] == 0xEF) {
            kprint("PASS: scsi_put_be32\n");
            pass++;
        } else {
            kprint("FAIL: scsi_put_be32\n");
            fail++;
        }
    }
    
    /*
     * Test 7: Device allocation
     */
    {
        scsi_device_t *dev = scsi_device_alloc();
        if (dev != NULL) {
            kprint("PASS: scsi_device_alloc\n");
            pass++;
            scsi_device_free(dev);
        } else {
            kprint("FAIL: scsi_device_alloc returned NULL\n");
            fail++;
        }
    }
    
    /*
     * Test 8: Request allocation
     */
    {
        scsi_request_t *req = scsi_request_alloc();
        if (req != NULL && req->state == SCSI_REQ_STATE_PENDING) {
            kprint("PASS: scsi_request_alloc\n");
            pass++;
            scsi_request_free(req);
        } else {
            kprint("FAIL: scsi_request_alloc\n");
            fail++;
        }
    }
    
    /*
     * Test 9: Transport registration
     */
    {
        int ret = scsi_register_link(&mock_link);
        if (ret == 0) {
            kprint("PASS: scsi_register_link\n");
            pass++;
        } else {
            kprint("FAIL: scsi_register_link\n");
            fail++;
        }
    }
    
    /*
     * Test 10: Device registration and lookup
     */
    {
        scsi_device_t *dev = scsi_device_alloc();
        dev->bus = 0;
        dev->target = 1;
        dev->lun = 2;
        dev->type = SCSI_TYPE_DISK;
        strcpy(dev->vendor, "TEST");
        strcpy(dev->product, "DEVICE");
        dev->link = &mock_link;
        
        int ret = scsi_device_register(dev);
        scsi_device_t *found = scsi_device_lookup(0, 1, 2);
        
        if (ret == 0 && found == dev) {
            kprint("PASS: scsi_device_register/lookup\n");
            pass++;
        } else {
            kprint("FAIL: scsi_device_register/lookup\n");
            fail++;
        }
        
        scsi_device_unregister(dev);
        scsi_device_free(dev);
    }

    /*
     * Test 10b: Duplicate device registration is rejected
     */
    {
        scsi_device_t *dev1 = scsi_device_alloc();
        scsi_device_t *dev2 = scsi_device_alloc();
        int ret1;
        int ret2;

        dev1->bus = 0;
        dev1->target = 3;
        dev1->lun = 0;
        dev1->type = SCSI_TYPE_DISK;
        dev1->link = &mock_link;

        dev2->bus = 0;
        dev2->target = 3;
        dev2->lun = 0;
        dev2->type = SCSI_TYPE_DISK;
        dev2->link = &mock_link;

        ret1 = scsi_device_register(dev1);
        ret2 = scsi_device_register(dev2);

        if (ret1 == 0 && ret2 < 0) {
            kprint("PASS: scsi_device_register duplicate detection\n");
            pass++;
        } else {
            sprintf(buf, "FAIL: duplicate register ret1=%d ret2=%d\n", ret1, ret2);
            kprint(buf);
            fail++;
        }

        scsi_device_unregister(dev1);
        scsi_device_free(dev1);
        scsi_device_free(dev2);
    }
    
    /*
     * Test 11: Sense key parsing (fixed format)
     */
    {
        uint8_t sense[18] = {0x70, 0, 0x05, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0x24, 0x00};
        int key = scsi_sense_key(sense, 18);
        int asc = scsi_sense_asc(sense, 18);
        int ascq = scsi_sense_ascq(sense, 18);
        int keys_ok = 1;

        for (int expected = 0; expected <= 0x0F; expected++) {
            sense[2] = (uint8_t)expected;
            if (scsi_sense_key(sense, sizeof(sense)) != expected) {
                keys_ok = 0;
                sprintf(buf, "FAIL: sense key decode expected=%d got=%d\n",
                        expected, scsi_sense_key(sense, sizeof(sense)));
                kprint(buf);
                break;
            }
        }

        sense[2] = 0x05;
        if (keys_ok && key == SCSI_SENSE_ILLEGAL_REQUEST &&
            asc == 0x24 && ascq == 0x00) {
            kprint("PASS: Sense parsing (fixed format)\n");
            pass++;
        } else if (keys_ok) {
            sprintf(buf, "FAIL: Sense parsing key=%d asc=%02x ascq=%02x\n", key, asc, ascq);
            kprint(buf);
            fail++;
        } else {
            fail++;
        }
    }
    
    /*
     * Test 12: Sense string lookup
     */
    {
        /* Test fallback to key description */
        const char *str1 = scsi_sense_string(SCSI_SENSE_MEDIUM_ERROR, 0, 0);

        /* Test specific ASC/ASCQ description */
        const char *str2 = scsi_sense_string(SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0x00);

        /* Test unit attention ASC/ASCQ */
        const char *str3 = scsi_sense_string(SCSI_SENSE_UNIT_ATTENTION, 0x29, 0x00);

        int ok = 1;
        if (strcmp(str1, "Medium Error") != 0) {
            sprintf(buf, "FAIL: scsi_sense_string(key=3, 0, 0) returned '%s'\n", str1);
            kprint(buf);
            ok = 0;
        }
        if (strcmp(str2, "Invalid field in CDB") != 0) {
            sprintf(buf, "FAIL: scsi_sense_string(key=5, 0x24, 0) returned '%s'\n", str2);
            kprint(buf);
            ok = 0;
        }
        if (strcmp(str3, "Power on, reset, or bus device reset occurred") != 0) {
            sprintf(buf, "FAIL: scsi_sense_string(key=6, 0x29, 0) returned '%s'\n", str3);
            kprint(buf);
            ok = 0;
        }

        if (ok) {
            kprint("PASS: scsi_sense_string (including ASC/ASCQ)\n");
            pass++;
        } else {
            fail++;
        }
    }

    /*
     * Test 12b: Request pool exhaustion and reuse
     */
    {
        scsi_request_t *reqs[32];
        int ok = 1;

        memset(reqs, 0, sizeof(reqs));
        for (int i = 0; i < 32; i++) {
            reqs[i] = scsi_request_alloc();
            if (reqs[i] == NULL) {
                ok = 0;
                sprintf(buf, "FAIL: request alloc returned NULL at slot %d\n", i);
                kprint(buf);
                break;
            }
        }

        if (ok && scsi_request_alloc() != NULL) {
            ok = 0;
            kprint("FAIL: request pool exhaustion did not return NULL\n");
        }

        if (reqs[0] != NULL) {
            scsi_request_free(reqs[0]);
            reqs[0] = scsi_request_alloc();
            if (reqs[0] == NULL) {
                ok = 0;
                kprint("FAIL: request pool reuse failed\n");
            }
        }

        for (int i = 0; i < 32; i++) {
            if (reqs[i] != NULL) {
                scsi_request_free(reqs[i]);
            }
        }

        if (ok) {
            kprint("PASS: scsi_request pool exhaustion/reuse\n");
            pass++;
        } else {
            fail++;
        }
    }
    
    /*
     * Test 13: Command execution via mock transport
     */
    {
        scsi_device_t *dev = scsi_device_alloc();
        dev->bus = 0;
        dev->target = 2;
        dev->lun = 0;
        dev->link = &mock_link;
        scsi_device_register(dev);
        
        mock_execute_calls = 0;
        mock_execute_return = 0;
        
        uint8_t cdb[6];
        scsi_cdb_test_unit_ready(cdb);
        int ret = scsi_execute_sync(dev, cdb, 6, NULL, 0, 0, 5000);
        
        if (ret == 0 && mock_execute_calls == 1) {
            kprint("PASS: scsi_execute_sync\n");
            pass++;
        } else {
            sprintf(buf, "FAIL: scsi_execute_sync ret=%d calls=%d\n", ret, mock_execute_calls);
            kprint(buf);
            fail++;
        }
        
        scsi_device_unregister(dev);
        scsi_device_free(dev);
    }
    
    /* Summary */
    sprintf(buf, "=== SCSI Tests: %d passed, %d failed ===\n", pass, fail);
    kprint(buf);
}

void run_scsi_tests(void) {
    test_scsi();
}
