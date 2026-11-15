/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : unit_tests.c
 *           Descriptions   : Unit tests of the modules shared by client and server: netascii
 *                            conversion, packet formation/parsing, file block reading and
 *                            writing, file name checks and safe opening below a directory
 *           Objective      : Check the pieces that are hard to observe from the outside
 *                            (pairs split across blocks, malformed packets, symlinks)
 *           Usage          : tests/unit_tests   (run by "make test")
 *           Output         : One "  PASS  <name>" or "  FAIL  <name>" line per check;
 *                            exit status 0 if every check passed
 *
###############################################################################################*/

#define _POSIX_C_SOURCE 200809L     /* mkdtemp(), symlink(), openat() */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../tftp.h"
#include "../file.h"
#include "../netascii.h"
#include "../udp.h"

#define RANDOM_ROUNDS       3000    /* Random netascii round trips          */
#define RANDOM_MAX_LEN      2048    /* Longest random input                 */
#define RANDOM_SEED         12345u
#define CHUNK_MAX           700     /* Largest random encode/decode piece   */
#define TEMP_TEMPLATE       "/tmp/tftp_unit_XXXXXX"
#define PATH_BUF            1024
#define LONG_NAME_LEN       600
#define BIG_BUF             8192

static int g_pass, g_fail;

/*
 * Function    : check
 * Description : Records and prints the result of one check
 */
static void check(int ok, const char *name)
{
    if (ok)
    {
        g_pass++;
        printf("  PASS  %s\n", name);
    }
    else
    {
        g_fail++;
        printf("  FAIL  %s\n", name);
    }
}

/*
 * Function    : encode_all
 * Description : Encodes a whole buffer, out_step bytes of output per call
 * Return      : Encoded length
 */
static size_t encode_all(const unsigned char *in, size_t len, unsigned char *out, size_t out_step)
{
    netascii_encoder_t enc;
    size_t pos = 0, written = 0;

    netascii_encoder_init(&enc);
    while (pos < len || netascii_encoder_pending(&enc))
    {
        size_t consumed;

        written += netascii_encode(&enc, in + pos, len - pos, &consumed, out + written, out_step);
        pos += consumed;
    }
    return written;
}

/*
 * Function    : decode_all
 * Description : Decodes a whole buffer, in_step input bytes per call
 * Return      : Decoded length
 */
static size_t decode_all(const unsigned char *in, size_t len, unsigned char *out, size_t in_step)
{
    netascii_decoder_t dec;
    size_t pos = 0, written = 0;

    netascii_decoder_init(&dec);
    while (pos < len)
    {
        size_t n = (len - pos < in_step) ? len - pos : in_step;

        written += netascii_decode(&dec, in + pos, n, out + written);
        pos += n;
    }
    written += netascii_decode_finish(&dec, out + written);
    return written;
}

/*
 * Function    : test_netascii
 * Description : Fixed examples, split pairs and random round trips
 */
static void test_netascii(void)
{
    static const unsigned char text[] = { 'a', '\n', 'b', '\r', 'c', '\0', 'd' };
    static const unsigned char wire[] = { 'a', '\r', '\n', 'b', '\r', '\0', 'c', '\0', 'd' };
    static unsigned char in[RANDOM_MAX_LEN], enc[2 * RANDOM_MAX_LEN], dec[2 * RANDOM_MAX_LEN + 1];
    unsigned char out[32];
    netascii_decoder_t d;
    size_t n, step;
    int round, ok;

    n = encode_all(text, sizeof text, out, sizeof out);
    check(n == sizeof wire && memcmp(out, wire, n) == 0, "netascii: LF -> CR LF, CR -> CR NUL");
    for (step = 1; step <= 3; step++)
    {
        n = encode_all(text, sizeof text, out, step);
        if (n != sizeof wire || memcmp(out, wire, n) != 0)
            break;
    }
    check(step > 3, "netascii: encoder output split at every position");

    n = decode_all(wire, sizeof wire, out, sizeof wire);
    check(n == sizeof text && memcmp(out, text, n) == 0, "netascii: CR LF -> LF, CR NUL -> CR");
    n = decode_all(wire, sizeof wire, out, 1);
    check(n == sizeof text && memcmp(out, text, n) == 0, "netascii: decoder input split byte by byte");

    netascii_decoder_init(&d);
    n = netascii_decode(&d, (const unsigned char *)"x\r", 2, out);
    check(n == 1 && d.pending_cr, "netascii: CR at end of a block is held back");
    n = netascii_decode(&d, (const unsigned char *)"\nY", 2, out);
    check(n == 2 && out[0] == '\n' && out[1] == 'Y', "netascii: CR LF split across blocks -> LF");

    n = decode_all((const unsigned char *)"a\rb", 3, out, 3);
    check(n == 3 && memcmp(out, "a\rb", 3) == 0, "netascii: invalid bare CR is kept");
    n = decode_all((const unsigned char *)"a\r", 2, out, 2);
    check(n == 2 && memcmp(out, "a\r", 2) == 0, "netascii: CR at end of transfer is kept");

    /* Random data rich in CR, LF and NUL, random split points */
    srand(RANDOM_SEED);
    ok = 1;
    for (round = 0; round < RANDOM_ROUNDS && ok; round++)
    {
        size_t len = (size_t)rand() % RANDOM_MAX_LEN, i, e, d2;

        for (i = 0; i < len; i++)
        {
            int r = rand() % 8;

            in[i] = (r == 0) ? '\r' : (r == 1) ? '\n' : (r == 2) ? '\0' : (unsigned char)rand();
        }
        e = encode_all(in, len, enc, 1 + (size_t)rand() % CHUNK_MAX);
        d2 = decode_all(enc, e, dec, 1 + (size_t)rand() % CHUNK_MAX);
        ok = (d2 == len && memcmp(dec, in, len) == 0);
    }
    check(ok, "netascii: decode(encode(x)) == x for 3000 random inputs");
}

/*
 * Function    : test_packets
 * Description : Packet formation and parsing, including malformed packets
 */
static void test_packets(void)
{
    unsigned char buf[PACKETSIZE + 8];
    char long_name[LONG_NAME_LEN + 1];
    packet_t pkt;
    size_t len;

    len = packet_form_rrq(buf, "dir/file.txt", "octet");
    check(len == 2 + 12 + 1 + 5 + 1 && packet_parse(buf, len, &pkt) == 0 && IS_RRQ(pkt.opcode)
          && strcmp(pkt.filename, "dir/file.txt") == 0 && strcmp(pkt.mode, "octet") == 0,
          "packet: RRQ formed and parsed");
    check(packet_parse(buf, len - 1, &pkt) != 0, "packet: RRQ without final NUL rejected");
    check(packet_parse(buf, 2 + 5, &pkt) != 0, "packet: RRQ without NUL after file name rejected");
    memcpy(buf, "\0\2\0octet\0", 10);
    check(packet_parse(buf, 10, &pkt) != 0, "packet: empty file name rejected");
    memcpy(buf, "\0\2f\0\0", 5);
    check(packet_parse(buf, 5, &pkt) != 0, "packet: empty mode rejected");
    memcpy(buf, "\0\1f\0octet\0tsize\0" "0\0", 18);
    check(packet_parse(buf, 18, &pkt) == 0 && strcmp(pkt.mode, "octet") == 0,
          "packet: RFC 2347 options after the mode are ignored");
    check(packet_parse(buf, 0, &pkt) != 0 && packet_parse(buf, 1, &pkt) != 0,
          "packet: 0 and 1 byte packets rejected");

    memset(long_name, 'n', LONG_NAME_LEN);
    long_name[LONG_NAME_LEN] = '\0';
    check(packet_form_wrq(buf, long_name, "octet") == 0, "packet: request too long is not formed");

    len = packet_form_data(buf, 7, (const unsigned char *)"", 0);
    check(len == HEADER_SIZE && packet_parse(buf, len, &pkt) == 0 && IS_DATA(pkt.opcode)
          && pkt.blocknum == 7 && pkt.data_length == 0, "packet: empty DATA block");
    memset(buf + HEADER_SIZE, 'x', BLIMIT + 1);
    check(packet_parse(buf, PACKETSIZE, &pkt) == 0 && pkt.data_length == BLIMIT,
          "packet: 512-byte DATA accepted");
    check(packet_parse(buf, PACKETSIZE + 1, &pkt) != 0, "packet: 513-byte DATA rejected");
    check(packet_form_data(buf, 1, buf, BLIMIT + 1) == 0, "packet: DATA larger than 512 not formed");
    check(packet_parse(buf, HEADER_SIZE - 1, &pkt) != 0, "packet: DATA shorter than header rejected");

    len = packet_form_ack(buf, 65535);
    check(len == HEADER_SIZE && packet_parse(buf, len, &pkt) == 0 && IS_ACK(pkt.opcode)
          && pkt.blocknum == 65535, "packet: ACK 65535");
    check(packet_parse(buf, 3, &pkt) != 0 && packet_parse(buf, 5, &pkt) != 0,
          "packet: ACK of wrong length rejected");

    len = packet_form_error(buf, ECODE_6, ESTRING_6);
    check(packet_parse(buf, len, &pkt) == 0 && IS_ERROR(pkt.opcode) && pkt.ecode == 6
          && strcmp(pkt.estring, "File already exists.") == 0, "packet: ERROR formed and parsed");
    memcpy(buf, "\0\5\0\1bad\033[2Jmsg", 14);
    check(packet_parse(buf, 14, &pkt) == 0 && pkt.ecode == 1 && strcmp(pkt.estring, "bad?[2Jmsg") == 0,
          "packet: ERROR without NUL accepted, control bytes replaced");
    len = packet_form_error(buf, ECODE_0, long_name);
    check(len == PACKETSIZE && buf[PACKETSIZE - 1] == '\0', "packet: long ERROR message truncated");

    memcpy(buf, "\0\6\0\0", 4);
    check(packet_parse(buf, 4, &pkt) != 0, "packet: opcode 6 (OACK) rejected");
    memcpy(buf, "\0\0\0\0", 4);
    check(packet_parse(buf, 4, &pkt) != 0, "packet: opcode 0 rejected");
    memcpy(buf, "\377\377\0\0", 4);
    check(packet_parse(buf, 4, &pkt) != 0, "packet: opcode 65535 rejected");
}

/*
 * Function    : test_helpers
 * Description : Modes, error strings, errno mapping, ports, name checks
 */
static void test_helpers(void)
{
    unsigned short port = 0;

    check(tftp_parse_mode("OCTET") == TFTP_MODE_OCTET && tftp_parse_mode("NetAscii") == TFTP_MODE_NETASCII
          && tftp_parse_mode("Mail") == TFTP_MODE_MAIL && tftp_parse_mode("binary") == TFTP_MODE_INVALID,
          "modes: names are case-insensitive");
    check(strcmp(tftp_error_string(1), "File not found.") == 0
          && strcmp(tftp_error_string(5), "Unknown transfer ID.") == 0
          && strcmp(tftp_error_string(6), "File already exists.") == 0
          && strcmp(tftp_error_string(7), "No such user.") == 0
          && strcmp(tftp_error_string(8), "Unknown error code.") == 0,
          "errors: RFC 1350 texts");
    check(tftp_errno_to_ecode(ENOENT) == 1 && tftp_errno_to_ecode(EACCES) == 2
          && tftp_errno_to_ecode(ELOOP) == 2 && tftp_errno_to_ecode(ENOSPC) == 3
          && tftp_errno_to_ecode(EFBIG) == 3 && tftp_errno_to_ecode(EEXIST) == 6
          && tftp_errno_to_ecode(EIO) == 0, "errors: errno mapped to TFTP codes");
    check(udp_parse_port("69", &port) == 0 && port == 69 && udp_parse_port("65535", &port) == 0
          && udp_parse_port("0", &port) != 0 && udp_parse_port("65536", &port) != 0
          && udp_parse_port("12a", &port) != 0 && udp_parse_port("", &port) != 0
          && udp_parse_port("-1", &port) != 0, "ports: parsing and range");

    check(file_check_name("a.txt") == 0 && file_check_name("dir/a.txt") == 0
          && file_check_name("...") == 0 && file_check_name("a..b") == 0,
          "names: ordinary names accepted");
    check(file_check_name("") != 0 && file_check_name("/etc/passwd") != 0
          && file_check_name("..") != 0 && file_check_name("../x") != 0
          && file_check_name("a/../../x") != 0 && file_check_name("a/..") != 0,
          "names: absolute paths and '..' rejected");
}

/*
 * Function    : write_file
 * Description : Creates a file with the given content
 */
static int write_file(const char *path, const void *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    int ok;

    if (fd < 0)
        return -1;
    ok = (write(fd, data, len) == (ssize_t)len);
    return (close(fd) == 0 && ok) ? 0 : -1;
}

/*
 * Function    : read_whole
 * Description : Reads a file into buf
 * Return      : Length, or -1 on error
 */
static long read_whole(const char *path, unsigned char *buf, size_t size)
{
    int fd = open(path, O_RDONLY);
    ssize_t n;

    if (fd < 0)
        return -1;
    n = read(fd, buf, size);
    close(fd);
    return (long)n;
}

/*
 * Function    : test_file_blocks
 * Description : Block reading/writing through the file module, in both modes
 */
static void test_file_blocks(const char *dir)
{
    static unsigned char content[BIG_BUF], block[BLIMIT], back[BIG_BUF];
    char path[PATH_BUF];
    tftp_file_t f;
    ssize_t n1, n2, n3;
    long len;
    int fd;

    /* 256 LFs encode to exactly 512 bytes: one full block, then an empty one */
    snprintf(path, sizeof path, "%s/lf.txt", dir);
    memset(content, '\n', 256);
    write_file(path, content, 256);
    if (file_open_read(&f, path, 1) == 0)
    {
        n1 = file_read_block(&f, block, BLIMIT);
        n2 = file_read_block(&f, block, BLIMIT);
        file_close(&f);
        check(n1 == BLIMIT && block[0] == '\r' && block[1] == '\n' && n2 == 0,
              "file: 256 LFs -> one full netascii block, then an empty block");
    }
    else
        check(0, "file: open lf.txt");

    /* 511 x + LF: the CR ends block 1, the LF starts block 2 */
    snprintf(path, sizeof path, "%s/split.txt", dir);
    memset(content, 'x', 511);
    content[511] = '\n';
    write_file(path, content, 512);
    if (file_open_read(&f, path, 1) == 0)
    {
        n1 = file_read_block(&f, block, BLIMIT);
        n2 = file_read_block(&f, block, BLIMIT);
        n3 = file_read_block(&f, block, BLIMIT);
        file_close(&f);
        check(n1 == BLIMIT && n2 == 1 && block[0] == '\n' && n3 == 0,
              "file: CR LF pair split across blocks when reading");
    }
    else
        check(0, "file: open split.txt");

    /* Octet mode: 1000 bytes -> 512 + 488 */
    snprintf(path, sizeof path, "%s/oct.bin", dir);
    write_file(path, content, 1000);
    if (file_open_read(&f, path, 0) == 0)
    {
        n1 = file_read_block(&f, block, BLIMIT);
        n2 = file_read_block(&f, block, BLIMIT);
        file_close(&f);
        check(n1 == BLIMIT && n2 == 488, "file: octet blocks of 512 and 488 bytes");
    }
    else
        check(0, "file: open oct.bin");

    /* Writing netascii with pairs split between blocks */
    snprintf(path, sizeof path, "%s/out.txt", dir);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0)
    {
        file_attach(&f, fd, 1);
        file_write_block(&f, (const unsigned char *)"abc\r", 4);
        file_write_block(&f, (const unsigned char *)"\nxy\r", 4);
        file_write_block(&f, (const unsigned char *)"\0z\r", 3);
        check(file_finish_write(&f) == 0 && f.fd == -1, "file: finish closes the file");
        len = read_whole(path, back, sizeof back);
        check(len == 9 && memcmp(back, "abc\nxy\rz\r", 9) == 0,
              "file: netascii written with pairs split between blocks");
    }
    else
        check(0, "file: create out.txt");
}

/*
 * Function    : try_open
 * Description : Calls file_open_served() and closes what it opened
 * Return      : 0 on success, otherwise the errno of the failure
 */
static int try_open(int root, const char *name, int for_write, char *leaf, size_t leaf_size)
{
    file_location_t loc;
    int fd = file_open_served(root, name, for_write, &loc);

    if (fd < 0)
        return errno;
    if (leaf != NULL)
        snprintf(leaf, leaf_size, "%s", loc.leaf);
    close(fd);
    file_location_close(&loc);
    return 0;
}

/*
 * Function    : test_served_paths
 * Description : file_open_served() keeps every access inside the directory
 */
static void test_served_paths(const char *dir)
{
    char root_path[PATH_BUF / 2], path[PATH_BUF], leaf[FILE_LEAF_MAX + 1];
    int root;

    snprintf(root_path, sizeof root_path, "%s/root", dir);
    mkdir(root_path, 0755);
    snprintf(path, sizeof path, "%s/sub", root_path);
    mkdir(path, 0755);
    snprintf(path, sizeof path, "%s/a.txt", root_path);
    write_file(path, "a", 1);
    snprintf(path, sizeof path, "%s/sub/b.txt", root_path);
    write_file(path, "b", 1);
    snprintf(path, sizeof path, "%s/secret.txt", dir);
    write_file(path, "s", 1);
    snprintf(path, sizeof path, "%s/link", root_path);
    if (symlink("../secret.txt", path) != 0)
        check(0, "paths: create symlink");
    snprintf(path, sizeof path, "%s/linkdir", root_path);
    if (symlink("..", path) != 0)
        check(0, "paths: create directory symlink");

    root = open(root_path, O_RDONLY | O_DIRECTORY);
    if (root < 0)
    {
        check(0, "paths: open root");
        return;
    }
    check(try_open(root, "a.txt", 0, NULL, 0) == 0, "paths: file in the directory");
    check(try_open(root, "sub/b.txt", 0, leaf, sizeof leaf) == 0 && strcmp(leaf, "b.txt") == 0,
          "paths: file in a subdirectory");
    check(try_open(root, "./sub//b.txt", 0, NULL, 0) == 0, "paths: '.' and empty components");
    check(try_open(root, "missing", 0, NULL, 0) == ENOENT, "paths: missing file -> ENOENT");
    check(try_open(root, "sub", 0, NULL, 0) == EISDIR, "paths: directory -> EISDIR");
    check(try_open(root, "../secret.txt", 0, NULL, 0) == EACCES, "paths: '..' refused");
    check(try_open(root, "/etc/passwd", 0, NULL, 0) == EACCES, "paths: absolute path refused");
    check(try_open(root, "link", 0, NULL, 0) == ELOOP, "paths: symlink to a file refused");
    check(try_open(root, "linkdir/secret.txt", 0, NULL, 0) == ELOOP,
          "paths: symlink to a directory refused");
    check(try_open(root, "a.txt", 1, NULL, 0) == EEXIST, "paths: upload never overwrites");
    check(try_open(root, "link", 1, NULL, 0) == EEXIST, "paths: upload onto a symlink refused");
    check(try_open(root, "sub/new.txt", 1, leaf, sizeof leaf) == 0 && strcmp(leaf, "new.txt") == 0,
          "paths: upload into a subdirectory");
    close(root);
}

/*
 * Function    : remove_tree
 * Description : Removes the test directory (small, known layout)
 */
static void remove_tree(const char *dir)
{
    static const char *const files[] =
    {
        "lf.txt", "split.txt", "oct.bin", "out.txt", "secret.txt", "root/a.txt", "root/link",
        "root/linkdir", "root/sub/b.txt", "root/sub/new.txt"
    };
    char path[PATH_BUF];
    size_t i;

    for (i = 0; i < sizeof files / sizeof files[0]; i++)
    {
        snprintf(path, sizeof path, "%s/%s", dir, files[i]);
        unlink(path);
    }
    snprintf(path, sizeof path, "%s/root/sub", dir);
    rmdir(path);
    snprintf(path, sizeof path, "%s/root", dir);
    rmdir(path);
    rmdir(dir);
}

/*
 * Function    : main
 * Description : Runs every unit test
 */
int main(void)
{
    char dir[] = TEMP_TEMPLATE;

    test_netascii();
    test_packets();
    test_helpers();
    if (mkdtemp(dir) == NULL)
    {
        check(0, "create temporary directory");
    }
    else
    {
        test_file_blocks(dir);
        test_served_paths(dir);
        remove_tree(dir);
    }
    printf("Unit tests passed: %d  failed: %d\n", g_pass, g_fail);
    return (g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
