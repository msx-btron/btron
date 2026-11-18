/*
 * test_virtio_blk.c
 * Unit test for VirtIO-Block Driver & BlkDev integration.
 *
 * Verifies:
 *  - VirtIO-Block device probing, capacity determination, and sector R/W
 *  - BlkDev interface translation (512-byte sectors to logical blocks)
 *  - MBR partition parsing on VirtIO-Block (blk_mbr_find_btron_partition)
 *  - Direct BTRON volume mounting (vol_mount) on VirtIO partition slice
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <drivers/virtio/virtio_blk.h>
#include <fs/block.h>
#include <fs/volume.h>
#include <fs/vol_api.h>

void uart_puts_raw(const char *str) {
    (void)str;
}

UW btron_fs_now_stamp(void) {
    return 100000;
}

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
extern void virtio_blk_set_mock_storage(int dev_idx, void *buf, uint64_t size_bytes);
#endif

int main(void) {
    printf("=== Running VirtIO-Block Driver Unit Tests ===\n");

    /* Allocate 16MB mock storage image */
    size_t disk_size = 16 * 1024 * 1024;
    uint8_t *disk_mem = (uint8_t *)calloc(1, disk_size);
    assert(disk_mem != NULL);

    /* 1. Create a dummy MBR partition table in sector 0 */
    /* MBR Signature */
    disk_mem[510] = 0x55;
    disk_mem[511] = 0xAA;

    /* Partition 1: Type 0x13 (BTRON), start sector 2048, size 20480 sectors (10MB) */
    uint8_t *p1 = disk_mem + 446;
    p1[4] = 0x13; /* BTRON type */
    uint32_t start_sec = 2048;
    uint32_t count_sec = 20480;
    memcpy(p1 + 8, &start_sec, 4);
    memcpy(p1 + 12, &count_sec, 4);

    /* Attach to VirtIO-Block unit 0 */
    virtio_blk_set_mock_storage(0, disk_mem, disk_size);

    assert(virtio_blk_get_device_count() == 1);
    assert(virtio_blk_get_capacity(0) == disk_size / 512);

    /* 2. Test Sector R/W */
    uint8_t test_pattern[512];
    for (int i = 0; i < 512; i++) test_pattern[i] = (uint8_t)(i ^ 0xA5);

    int rc = virtio_blk_write_sectors(0, 100, test_pattern, 1);
    assert(rc == 0);

    uint8_t read_back[512];
    rc = virtio_blk_read_sectors(0, 100, read_back, 1);
    assert(rc == 0);
    assert(memcmp(test_pattern, read_back, 512) == 0);
    printf("  [PASS] VirtIO-Block sector write & read verified.\n");

    /* 3. Test BlkDev interface */
    BlkDev *vdev = blk_virtio_create(0);
    assert(vdev != NULL);
    assert(vdev->block_size == 512);
    assert(vdev->nblocks == disk_size / 512);

    /* 4. Test MBR scanning on VirtIO BlkDev */
    BlkDev *part = blk_mbr_find_btron_partition(vdev, 1024);
    assert(part != NULL);
    assert(part->block_size == 1024);
    printf("  [PASS] VirtIO-Block MBR partition slice discovered.\n");

    /* 5. Format & Mount BTRON volume on the partition */
    rc = vol_format(part, 256, part->nblocks, "TEST_VIRTIO");
    assert(rc == 0);

    Volume *vol = vol_mount(part);
    assert(vol != NULL);
    assert(strcmp(vol_name(vol), "TEST_VIRTIO") == 0);
    printf("  [PASS] Mounted BTRON volume on VirtIO partition: \"%s\"\n", vol_name(vol));

    vol_umount(vol);
    free(disk_mem);

    printf("=== All VirtIO-Block Tests Passed! ===\n");
    return 0;
}
