/*
 * test_storage_images.c
 * Validates generated USB, HDD, and VirtIO disk images.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fs/block.h>
#include <fs/volume.h>
#include <fs/vol_api.h>

void uart_puts_raw(const char *str) { (void)str; }
UW btron_fs_now_stamp(void) { return 100000; }

int main(void) {
    printf("=== Validating Generated Storage Images ===\n");

    /* 1. Open USB image with blk_file */
    BlkDev *usb_dev = blk_file_create(".build/btron-uefi-usb.img", 0, 0);
    assert(usb_dev != NULL);
    /* blk_file defaults to 1024; set block_size to 512 for sector scanning */
    usb_dev->block_size = 512;
    usb_dev->nblocks   *= 2;

    /* 2. Find Partition 2 (/SYS) */
    BlkDev *part_sys = blk_mbr_find_btron_partition_idx(usb_dev, 0, 1024);
    assert(part_sys != NULL);

    Volume *sys_vol = vol_mount(part_sys);
    assert(sys_vol != NULL);
    assert(strcmp(vol_name(sys_vol), "SYS") == 0 || strncmp(vol_name(sys_vol), "BTRON-", 6) == 0);
    printf("  [PASS] Mounted Partition 2 (/SYS) from USB image. Volume: \"%s\", Free blocks: %u\n",
           vol_name(sys_vol), (unsigned)vol_free_blocks(sys_vol));

    /* 3. Find Partition 3 (/ANDERS) */
    BlkDev *part_anders = blk_mbr_find_btron_partition_idx(usb_dev, 1, 1024);
    assert(part_anders != NULL);

    Volume *anders_vol = vol_mount(part_anders);
    assert(anders_vol != NULL);
    assert(strcmp(vol_name(anders_vol), "ANDERS") == 0);
    printf("  [PASS] Mounted Partition 3 (/ANDERS) from USB image. Volume: \"%s\", Free blocks: %u\n",
           vol_name(anders_vol), (unsigned)vol_free_blocks(anders_vol));

    vol_umount(sys_vol);
    vol_umount(anders_vol);
    blk_file_close(usb_dev);

    /* 4. Validate installed HDD image */
    BlkDev *hdd_dev = blk_file_create(".build/btron-uefi-hdd.img", 0, 0);
    assert(hdd_dev != NULL);
    hdd_dev->block_size = 512;
    hdd_dev->nblocks   *= 2;

    BlkDev *hdd_part = blk_mbr_find_btron_partition_idx(hdd_dev, 0, 1024);
    assert(hdd_part != NULL);
    Volume *hdd_vol = vol_mount(hdd_part);
    assert(hdd_vol != NULL);
    printf("  [PASS] Mounted /SYS from installed HDD image: \"%s\"\n", vol_name(hdd_vol));

    vol_umount(hdd_vol);
    blk_file_close(hdd_dev);

    /* 5. Validate unified btron_disk.img (if present) & normative ER_* error codes */
    {
        unsigned char blank[4096];
        memset(blank, 0, sizeof(blank));
        BlkDev *bdev = blk_mem_create(blank, (UW)sizeof(blank), 0);
        assert(bdev != NULL);
        bdev->block_size = 512;
        bdev->nblocks   *= 2;

        ER er = E_OK;
        assert(blk_mbr_find_btron_partition_ex(bdev, 0, 1024, &er) == NULL);
        assert(er == ER_PART_NOMBR);

        /* Write Protective MBR (0xEE) without valid GPT header -> ER_PART_BADGPT */
        blank[446 + 4] = 0xEE;
        blank[510] = 0x55;
        blank[511] = 0xAA;
        assert(blk_mbr_find_btron_partition_ex(bdev, 0, 1024, &er) == NULL);
        assert(er == ER_PART_BADGPT);

        /* Clear 0xEE -> valid empty MBR -> ER_PART_NOTFOUND */
        blank[446 + 4] = 0x00;
        assert(blk_mbr_find_btron_partition_ex(bdev, 0, 1024, &er) == NULL);
        assert(er == ER_PART_NOTFOUND);

        blk_destroy(bdev);
        printf("  [PASS] Normative partition error codes (ER_PART_NOMBR, ER_PART_BADGPT, ER_PART_NOTFOUND) verified.\n");
    }

    /* 6. Validate GPT disk image built by mk_uefi_storage.py --gpt if present */
    BlkDev *gpt_dev = blk_file_create(".build/test_gpt_disk.img", 0, 0);
    if (gpt_dev) {
        gpt_dev->block_size = 512;
        gpt_dev->nblocks   *= 2;
        ER er = E_OK;
        BlkDev *g0 = blk_mbr_find_btron_partition_ex(gpt_dev, 0, 1024, &er);
        assert(g0 != NULL && er == E_OK);
        Volume *gv = vol_mount(g0);
        assert(gv != NULL);
        printf("  [PASS] Mounted Protective GPT /SYS partition: \"%s\"\n", vol_name(gv));
        vol_umount(gv);
        blk_partition_release(g0);
        blk_file_close(gpt_dev);
    }

    printf("=== All Storage Image Validations Passed! ===\n");
    return 0;
}
