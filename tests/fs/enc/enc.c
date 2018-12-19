#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <errno.h>
#include <limits.h>
#include <openenclave/enclave.h>
#include <openenclave/internal/hexdump.h>
#include <openenclave/internal/keys.h>
#include <openenclave/internal/muxfs.h>
#include <openenclave/internal/oefs.h>
#include <openenclave/internal/tests.h>
#include <openenclave/internal/time.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "../../../fs/common/strarr.h"
#include "../../../fs/cpio/commands.h"
#include "../../../fs/cpio/cpio.h"
#include "fs_t.h"
#include "test_default_fs_macro.h"

extern oe_fs_t oe_sgxfs;
extern oe_fs_t oe_hostfs;

static const char* _mkpath(
    char buf[PATH_MAX],
    const char* target,
    const char* path)
{
    strlcpy(buf, target, PATH_MAX);
    strlcat(buf, path, PATH_MAX);
    return buf;
}

static void _test_default_fs(const char* tmp_dir)
{
    char path[PATH_MAX];
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    char buf[sizeof(alphabet)];
    FILE* os;

    _mkpath(path, tmp_dir, "/default_fs.test");

    OE_TEST((os = fopen(path, "wb")) != NULL);
    OE_TEST(fwrite(alphabet, 1, sizeof(alphabet), os) == sizeof(alphabet));
    OE_TEST(fclose(os) == 0);

    OE_TEST((os = fopen(path, "rb")) != NULL);
    OE_TEST(fread(buf, 1, sizeof(alphabet), os) == sizeof(alphabet));
    OE_TEST(fclose(os) == 0);

    OE_TEST(memcmp(buf, alphabet, sizeof(buf)) == 0);

    OE_TEST(remove(path) == 0);
}

static void _test_alphabet_file(oe_fs_t* fs, const char* tmp_dir)
{
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    char buf[sizeof(alphabet)];
    const size_t N = 1600;
    FILE* stream;
    size_t m = 0;
    char path[PATH_MAX];

    _mkpath(path, tmp_dir, "/alphabet");

    stream = oe_fopen(fs, path, "w");
    OE_TEST(stream != NULL);

    /* Write to the file */
    for (size_t i = 0; i < N; i++)
    {
        ssize_t n = fwrite(alphabet, 1, sizeof(alphabet), stream);
        OE_TEST(n == sizeof(alphabet));
        m += n;
    }

    OE_TEST(m == sizeof(alphabet) * N);
    OE_TEST(fflush(stream) == 0);
    OE_TEST(fclose(stream) == 0);

    /* Reopen the file for read. */
    stream = oe_fopen(fs, path, "r");
    OE_TEST(stream != NULL);

    /* Read from the file. */
    for (size_t i = 0, m = 0; i < N; i++)
    {
        ssize_t n = fread(buf, 1, sizeof(buf), stream);
        OE_TEST(n == sizeof(buf));
        OE_TEST(memcmp(buf, alphabet, sizeof(alphabet)) == 0);
        // printf("buf{%s}\n", buf);
        m += n;
    }

    OE_TEST(m == sizeof(alphabet) * N);
    fclose(stream);
}

static void _test_dirs(oe_fs_t* fs, const char* tmp_dir)
{
    DIR* dir;
    struct dirent* entry;
    size_t m = 0;

    OE_TEST((dir = oe_opendir(fs, tmp_dir)) != NULL);

    while ((entry = readdir(dir)))
    {
        m++;

        if (strcmp(entry->d_name, ".") == 0)
            continue;

        if (strcmp(entry->d_name, "..") == 0)
            continue;

        if (strcmp(entry->d_name, "alphabet") == 0)
            continue;

        if (strcmp(entry->d_name, "cpio.file") == 0)
            continue;

        if (strcmp(entry->d_name, "cpio.dir") == 0)
            continue;

        if (strcmp(entry->d_name, "test_sgxfs_with_key") == 0)
            continue;

        if (strcmp(entry->d_name, "test.oefs") == 0)
            continue;

        printf("ERROR: found file: %s\n", entry->d_name);
        OE_TEST(false);
    }

    OE_TEST(m >= 3);

    closedir(dir);
}

static const char* _basename(const char* path)
{
    const char* p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static void _test_cpio(
    oe_fs_t* fs,
    const char* src_dir,
    const char* tmp_dir,
    const char* msg)
{
    char tests_dir[PATH_MAX];
    char cpio_file[PATH_MAX];
    char cpio_dir[PATH_MAX];

    uint64_t t2 = oe_get_time();

    _mkpath(tests_dir, src_dir, "/tests");
    _mkpath(cpio_file, tmp_dir, "/cpio.file");
    _mkpath(cpio_dir, tmp_dir, "/cpio.dir");

    oe_fs_set_default(fs);
    {
        oe_strarr_t paths1 = OE_STRARR_INITIALIZER;
        oe_strarr_t paths2 = OE_STRARR_INITIALIZER;

        OE_TEST(oe_cpio_pack(tests_dir, cpio_file) == 0);
        mkdir(cpio_dir, 0777);
        OE_TEST(oe_cpio_unpack(cpio_file, cpio_dir) == 0);

        OE_TEST(oe_lsr(tests_dir, &paths2) == 0);
        OE_TEST(oe_lsr(cpio_dir, &paths1) == 0);

        OE_TEST(paths1.size == paths1.size);

        oe_strarr_sort(&paths1);
        oe_strarr_sort(&paths2);

        for (size_t i = 0; i < paths1.size; i++)
        {
            const char* filename1 = _basename(paths1.data[i]);
            const char* filename2 = _basename(paths2.data[i]);
            OE_TEST(strcmp(filename1, filename2) == 0);
        }

        /* Compare the alphabet file. */
        {
            char file1[PATH_MAX];
            char file2[PATH_MAX];

            _mkpath(file1, src_dir, "/tests/fs/alphabet");
            _mkpath(file2, cpio_dir, "/fs/alphabet");

            OE_TEST(oe_cmp(file1, file1) == 0);
            OE_TEST(oe_cmp(file1, file2) == 0);
        }

        oe_strarr_release(&paths1);
        oe_strarr_release(&paths2);
    }
    oe_fs_set_default(NULL);

    uint64_t t1 = oe_get_time();
    printf("_test_cpio(): %s: %lf seconds\n", msg, (t1 - t2) / 1000.0);
}

static void _test_sgxfs_with_key(const char* tmp_dir)
{
    FILE* stream;
    char path[PATH_MAX];
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    char buf[sizeof(alphabet)];
    uint8_t key[16];
    uint8_t wrong_key[16];

    OE_TEST(oe_random(key, sizeof(key)) == OE_OK);
    OE_TEST(oe_random(wrong_key, sizeof(wrong_key)) == OE_OK);
    OE_TEST(memcmp(key, wrong_key, sizeof(wrong_key)) != 0);

    strlcpy(path, tmp_dir, sizeof(path));
    strlcat(path, "/test_sgxfs_with_key", sizeof(path));

    /* Write the alphabet to the file. */
    OE_TEST((stream = oe_fopen(&oe_sgxfs, path, "wbk", key)));
    OE_TEST(fwrite(alphabet, 1, sizeof(alphabet), stream) == sizeof(alphabet));
    OE_TEST(fclose(stream) == 0);

    /* Make sure that opening the file with the wrong key fails. */
    OE_TEST((stream = oe_fopen(&oe_sgxfs, path, "rbk", wrong_key)) == NULL);

    /* Read the alphabet back from the file. */
    OE_TEST((stream = oe_fopen(&oe_sgxfs, path, "rbk", key)));
    OE_TEST(fread(buf, 1, sizeof(buf), stream) == sizeof(buf));
    OE_TEST(memcmp(buf, alphabet, sizeof(buf)) == 0);
    OE_TEST(fclose(stream) == 0);

    OE_TEST(oe_remove(&oe_sgxfs, path) == 0);
}

static int _create_oefs_device_file(const char* path, size_t nblks)
{
    int ret = -1;
    FILE* os;
    uint8_t block[OEFS_BLOCK_SIZE];
    size_t total_nblks;

    uint64_t t0 = oe_get_time();

    OE_TEST(oefs_calculate_total_blocks(nblks, &total_nblks) == 0);

    if (!(os = oe_fopen(&oe_hostfs, path, "w")))
        goto done;

    memset(block, 0, sizeof(block));

    for (size_t i = 0; i < total_nblks; i++)
    {
        if (fwrite(block, 1, sizeof(block), os) != sizeof(block))
            goto done;
    }

    uint64_t t1 = oe_get_time();
    printf("_create_oefs_device_file(): %lf\n", (t1 - t0) / 1000.0);

    ret = 0;

done:

    if (os)
        fclose(os);

    return ret;
}

static void _test_oefs(const char* src_dir, const char* tmp_dir)
{
    oe_fs_t oefs = OE_FS_INITIALIZER;
    char source[PATH_MAX];
    _mkpath(source, tmp_dir, "/test.oefs");
    size_t nbytes = 2 * 4194304;
    size_t nblks = nbytes / OEFS_BLOCK_SIZE;
    uint8_t static_key[OEFS_KEY_SIZE] = {
        0x0f, 0xf0, 0x31, 0xe3, 0x93, 0xdf, 0x46, 0x7b, 0x9a, 0x33, 0xe8,
        0x3c, 0x55, 0x11, 0xac, 0x52, 0x9e, 0xd4, 0xb1, 0xad, 0x10, 0x16,
        0x4f, 0xd9, 0x92, 0x19, 0x93, 0xcc, 0xa9, 0x0e, 0xcb, 0xed,
    };
    const uint8_t* key = static_key;

    /* Create a zero-filled file on the host (if it does not already exist). */
    {
        struct stat buf;

        if (oe_stat(&oe_hostfs, source, &buf) != 0)
        {
            OE_TEST(_create_oefs_device_file(source, nblks) == 0);
        }
    }

    /* Force auto-generation of the key. */
    key = NULL;

    oe_hex_dump(key, sizeof(key));

    /* Initialize OEFS */
    {
        uint64_t t0 = oe_get_time();
        OE_TEST(oe_oefs_initialize(&oefs, source, key) == 0);
        uint64_t t1 = oe_get_time();
        printf("oe_oefs_initialize(): %lf\n", (t1 - t0) / 1000.0);
    }

    oe_fs_set_default(&oefs);
    OE_TEST(oe_mkdir(&oefs, "/tmp", 0777) == 0);
    _test_alphabet_file(&oefs, "/tmp");
    oe_fs_set_default(NULL);

    /* Register oefs with the multiplexer. */
    OE_TEST(oe_muxfs_register_fs(&oe_muxfs, "/oefs", &oefs) == 0);

    {
        uint64_t t0 = oe_get_time();

        for (size_t i = 0; i < 1000; i++)
            _test_alphabet_file(&oe_muxfs, "/oefs/tmp");

        uint64_t t1 = oe_get_time();
        printf("_test_alphabet_file(): %lf\n", (t1 - t0) / 1000.0);
    }

    /* Test the multiplexer with HOSTFS and OEFS. */
    {
        char mux_src_dir[PATH_MAX];
        const char mux_tmp_dir[] = "/oefs/tmp";
        _mkpath(mux_src_dir, "/hostfs", src_dir);
        _test_cpio(&oe_muxfs, mux_src_dir, mux_tmp_dir, "oefs");
    }

    /* Unregister oefs with the multiplexer. */
    OE_TEST(oe_muxfs_unregister_fs(&oe_muxfs, "/oefs") == 0);

    oe_release(&oefs);
}

void enc_test(const char* src_dir, const char* bin_dir)
{
    static char tmp_dir[PATH_MAX];
    struct stat buf;

    /* Create the temporary directory (if it does not already exist). */
    {
        _mkpath(tmp_dir, bin_dir, "/tests/fs/tmp");

        if (oe_stat(&oe_hostfs, tmp_dir, &buf) != 0)
            OE_TEST(oe_mkdir(&oe_hostfs, tmp_dir, 0777) == 0);
    }

    oe_fs_set_default(&oe_hostfs);
    _test_default_fs(tmp_dir);
    oe_fs_set_default(NULL);

    oe_fs_set_default(&oe_hostfs);
    _test_default_fs(tmp_dir);
    oe_fs_set_default(NULL);

    _test_alphabet_file(&oe_sgxfs, tmp_dir);
    _test_alphabet_file(&oe_hostfs, tmp_dir);
    _test_dirs(&oe_hostfs, tmp_dir);
    _test_dirs(&oe_sgxfs, tmp_dir);
    _test_cpio(&oe_hostfs, src_dir, tmp_dir, "hostfs");

    /* Test the multiplexer: hostfs -> hostfs */
    {
        char mux_src_dir[PATH_MAX];
        char mux_tmp_dir[PATH_MAX];
        _mkpath(mux_src_dir, "/hostfs", src_dir);
        _mkpath(mux_tmp_dir, "/hostfs", tmp_dir);
        _test_cpio(&oe_muxfs, mux_src_dir, mux_tmp_dir, "hostfs:muxfs");
    }

    /* Test the multiplexer: hostfs -> sgxfs */
    {
        char mux_src_dir[PATH_MAX];
        char mux_tmp_dir[PATH_MAX];
        _mkpath(mux_src_dir, "/hostfs", src_dir);
        _mkpath(mux_tmp_dir, "/sgxfs", tmp_dir);
        _test_cpio(&oe_muxfs, mux_src_dir, mux_tmp_dir, "sgxfs");
    }

    /* Test the use of the OE_DEFAULT_FS macro. */
    test_default_fs_macro(tmp_dir);

    _test_sgxfs_with_key(tmp_dir);

    _test_oefs(src_dir, tmp_dir);
}

OE_SET_ENCLAVE_SGX(
    1,         /* ProductID */
    1,         /* SecurityVersion */
    true,      /* AllowDebug */
    16 * 1024, /* HeapPageCount */
    2 * 4096,  /* StackPageCount */
    2);        /* TCSCount */