/*
 * A utility program originally written for the Linux OS SCSI subsystem.
 *
 * Copyright (C) 2000-2012 Ingo van Lil <inguin@gmx.de>
 * Copyright (C) 2014 Asgeir Storesund Nilsen <asgeir@twingine.no>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is used to send REPORT DENSITY SUPPORT and decode the
 * results.
 */

#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "sg_lib.h"
#include "sg_pt.h"

#define SG_DENSITY_VERSION "0.4.5.1 (2014-11-13)"

#define DEFAULT_TIMEOUT 20
#define MIN_SCSI_CDBSZ 6
#define MAX_SCSI_CDBSZ 256
#define MAX_SCSI_DXLEN (64 * 1024)

static struct option long_options[] = {
    { "binary",  no_argument,       NULL, 'b' },
    { "help",    no_argument,       NULL, 'h' },
    { "infile",  required_argument, NULL, 'i' },
    { "skip",    required_argument, NULL, 'k' },
    { "nosense", no_argument,       NULL, 'n' },
    { "outfile", required_argument, NULL, 'o' },
    { "request", required_argument, NULL, 'r' },
    { "readonly", no_argument,      NULL, 'R' },
    { "send",    required_argument, NULL, 's' },
    { "timeout", required_argument, NULL, 't' },
    { "verbose", no_argument,       NULL, 'v' },
    { "version", no_argument,       NULL, 'V' },
    { "media",   no_argument,       NULL, 'm' },
    { 0, 0, 0, 0 }
};

struct opts_t {
    char *device_name;
    unsigned char cdb[MAX_SCSI_CDBSZ];
    int cdb_length;
    int do_datain;
    int datain_len;
    const char *datain_file;
    int datain_binary;
    int do_dataout;
    int dataout_len;
    const char *dataout_file;
    off_t dataout_offset;
    int timeout;
    int no_sense;
    int readonly;
    int do_help;
    int do_verbose;
    int do_version;
    unsigned char do_media;
};

static void
version()
{
    fprintf(stderr,
            "sg_density " SG_DENSITY_VERSION "\n"
            "Copyright (C) 2007-2012 Ingo van Lil <inguin@gmx.de> (sg_raw)\n"
            "Copyright (C) 2014 Asgeir Storesund Nilsen <asgeir@twingine.no>\n"
            "This is free software.  You may redistribute copies of it "
            "under the terms of\n"
            "the GNU General Public License "
            "<http://www.gnu.org/licenses/gpl.html>.\n"
            "There is NO WARRANTY, to the extent permitted by law.\n");
}

static void
usage()
{
    fprintf(stderr,
            "Usage: sg_density [OPTION]* DEVICE\n"
            "\n"
            "Options:\n"
            "  -m, --media            Report density for media loaded (default all supported by drive).\n"
            "                         Drive will report Not Ready if no media loaded.\n"
            "  -b, --binary           Dump data in binary form, even when "
            "writing to stdout\n"
            "  -h, --help             Show this message and exit\n"
            "  -i, --infile=IFILE     Read data to send from IFILE (default: "
            "stdin)\n"
            "  -k, --skip=LEN         Skip the first LEN bytes when reading "
            "data to send\n"
            "  -n, --nosense          Don't display sense information\n"
            "  -o, --outfile=OFILE    Write binary data to OFILE (def: "
            "hexdump to stdout)\n"
            "  -r, --request=RLEN     Request up to RLEN bytes of data "
            "(data-in)\n"
            "  -R, --readonly         Open DEVICE read-only (default: "
            "read-write)\n"
            "  -s, --send=SLEN        Send SLEN bytes of data (data-out)\n"
            "  -t, --timeout=SEC      Timeout in seconds (default: 20)\n"
            "  -v, --verbose          Increase verbosity\n"
            "  -V, --version          Show version information and exit\n"
            "\n");
}

static int
process_cl(struct opts_t *optsp, int argc, char *argv[])
{
    while (1) {
        int c, n;

        c = getopt_long(argc, argv, "bhi:k:no:r:Rs:t:vVm", long_options, NULL);
        if (c == -1)
            break;

        switch (c) {
        case 'b':
            optsp->datain_binary = 1;
            break;
        case 'h':
        case '?':
            optsp->do_help = 1;
            return 0;
        case 'i':
            if (optsp->dataout_file) {
                fprintf(stderr, "Too many '--infile=' options\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            optsp->dataout_file = optarg;
            break;
        case 'k':
            n = sg_get_num(optarg);
            if (n < 0) {
                fprintf(stderr, "Invalid argument to '--skip'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            optsp->dataout_offset = n;
            break;
        case 'n':
            optsp->no_sense = 1;
            break;
        case 'o':
            if (optsp->datain_file) {
                fprintf(stderr, "Too many '--outfile=' options\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            optsp->datain_file = optarg;
            break;
        case 'r':
            optsp->do_datain = 1;
            n = sg_get_num(optarg);
            if (n < 0 || n > MAX_SCSI_DXLEN) {
                fprintf(stderr, "Invalid argument to '--request'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            optsp->datain_len = n;
            break;
        case 'R':
            ++optsp->readonly;
            break;
        case 's':
            optsp->do_dataout = 1;
            n = sg_get_num(optarg);
            if (n < 0 || n > MAX_SCSI_DXLEN) {
                fprintf(stderr, "Invalid argument to '--send'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            optsp->dataout_len = n;
            break;
        case 't':
            n = sg_get_num(optarg);
            if (n < 0) {
                fprintf(stderr, "Invalid argument to '--timeout'\n");
                return SG_LIB_SYNTAX_ERROR;
            }
            optsp->timeout = n;
            break;
        case 'v':
            ++optsp->do_verbose;
            break;
        case 'V':
            optsp->do_version = 1;
            return 0;
        case 'm':
            optsp->do_media = 1;
            break;
        default:
            return SG_LIB_SYNTAX_ERROR;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "No device specified\n");
        return SG_LIB_SYNTAX_ERROR;
    }
    optsp->device_name = argv[optind];
    ++optind;

    return 0;
}

/* Allocate aligned memory (heap) starting on page boundary */
static unsigned char *
my_memalign(int length, unsigned char ** wrkBuffp)
{
    unsigned char * wrkBuff;
    size_t psz;

#ifdef SG_LIB_MINGW
    psz = getpagesize();
#else
    psz = sysconf(_SC_PAGESIZE); /* was getpagesize() */
#endif
    wrkBuff = (unsigned char*)calloc(length + psz, 1);
    if (NULL == wrkBuff) {
        if (wrkBuffp)
            *wrkBuffp = NULL;
        return NULL;
    } else if (wrkBuffp)
        *wrkBuffp = wrkBuff;
    // posix_memalign() could be a better way to do this
    return (unsigned char *)(((unsigned long)wrkBuff + psz - 1) &
                             (~(psz - 1)));
}

static int
skip(int fd, off_t offset)
{
    off_t remain;
    char buffer[512];

    if (lseek(fd, offset, SEEK_SET) >= 0) {
        return 0;
    }

    // lseek failed; fall back to reading and discarding data
    remain = offset;
    while (remain > 0) {
        ssize_t amount, done;
        amount = (remain < (off_t)sizeof(buffer)) ? remain
                                         : (off_t)sizeof(buffer);
        done = read(fd, buffer, amount);
        if (done < 0) {
            perror("Error reading input data");
            return SG_LIB_FILE_ERROR;
        } else if (done == 0) {
            fprintf(stderr, "EOF on input file/stream\n");
            return SG_LIB_FILE_ERROR;
        } else {
            remain -= done;
        }
    }

    return 0;
}

static unsigned char *
fetch_dataout(struct opts_t *optsp)
{
    unsigned char *buf = NULL;
    unsigned char *wrkBuf = NULL;
    int fd, len;
    int ok = 0;

    if (optsp->dataout_file) {
        fd = open(optsp->dataout_file, O_RDONLY);
        if (fd < 0) {
            perror(optsp->dataout_file);
            goto bail;
        }

    } else {
        fd = STDIN_FILENO;
    }
    if (sg_set_binary_mode(fd) < 0) {
        perror("sg_set_binary_mode");
        goto bail;
    }

    if (optsp->dataout_offset > 0) {
        if (skip(fd, optsp->dataout_offset) != 0) {
            goto bail;
        }
    }

    buf = my_memalign(optsp->dataout_len, &wrkBuf);
    if (buf == NULL) {
        perror("malloc");
        goto bail;
    }

    len = read(fd, buf, optsp->dataout_len);
    if (len < 0) {
        perror("Failed to read input data");
        goto bail;
    } else if (len < optsp->dataout_len) {
        fprintf(stderr, "EOF on input file/stream\n");
        goto bail;
    }

    ok = 1;

bail:
    if (fd >= 0 && fd != STDIN_FILENO)
        close(fd);
    if (!ok) {
        if (wrkBuf)
            free(wrkBuf);
        return NULL;
    }
    return buf;
}

static int
write_dataout(const char *filename, unsigned char *buf, int len)
{
    int ret = SG_LIB_FILE_ERROR;
    int fd;

    if ((filename == NULL) ||
        ((1 == strlen(filename)) && ('-' == filename[0])))
        fd = STDOUT_FILENO;
    else {
        fd = creat(filename, 0666);
        if (fd < 0) {
            perror(filename);
            goto bail;
        }
    }
    if (sg_set_binary_mode(fd) < 0) {
        perror("sg_set_binary_mode");
        goto bail;
    }

    if (write(fd, buf, len) != len) {
        perror(filename ? filename : "stdout");
        goto bail;
    }

    ret = 0;

bail:
    if (fd >= 0 && fd != STDOUT_FILENO)
        close(fd);
    return ret;
}

int
main(int argc, char *argv[])
{
    int ret = 0;
    int res_cat, status, slen, k, ret2;
    struct opts_t opts;
    int sg_fd = -1;
    struct sg_pt_base *ptvp = NULL;
    unsigned char sense_buffer[32];
    unsigned char * dxfer_buffer_in = NULL;
    unsigned char * dxfer_buffer_out = NULL;
    unsigned char *wrkBuf = NULL;
    char b[128];

    memset(&opts, 0, sizeof(opts));
    opts.timeout = DEFAULT_TIMEOUT;
    ret = process_cl(&opts, argc, argv);
    if (ret != 0) {
        usage();
        goto done;
    } else if (opts.do_help) {
        usage();
        goto done;
    } else if (opts.do_version) {
        version();
        goto done;
    }

    unsigned char command[] = { 0x44, opts.do_media, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00 };
    memcpy(opts.cdb, command, 9);
    opts.cdb_length = 9;
    opts.do_datain = 1;
    opts.datain_len = 1024;

    sg_fd = scsi_pt_open_device(opts.device_name, opts.readonly,
                                opts.do_verbose);
    if (sg_fd < 0) {
        fprintf(stderr, "%s: %s\n", opts.device_name, safe_strerror(-sg_fd));
        ret = SG_LIB_FILE_ERROR;
        goto done;
    }

    ptvp = construct_scsi_pt_obj();
    if (ptvp == NULL) {
        fprintf(stderr, "out of memory\n");
        ret = SG_LIB_CAT_OTHER;
        goto done;
    }
    if (opts.do_verbose) {
        fprintf(stderr, "    cdb to send: ");
        for (k = 0; k < opts.cdb_length; ++k)
            fprintf(stderr, "%02x ", opts.cdb[k]);
        fprintf(stderr, "\n");
        if (opts.do_verbose > 2) {
            sg_get_command_name(opts.cdb, 0, sizeof(b) - 1, b);
            b[sizeof(b) - 1] = '\0';
            fprintf(stderr, "    Command name: %s\n", b);
        }
    }
    set_scsi_pt_cdb(ptvp, opts.cdb, opts.cdb_length);
    set_scsi_pt_sense(ptvp, sense_buffer, sizeof(sense_buffer));

    if (opts.do_dataout) {
        dxfer_buffer_out = fetch_dataout(&opts);
        if (dxfer_buffer_out == NULL) {
            ret = SG_LIB_CAT_OTHER;
            goto done;
        }
        set_scsi_pt_data_out(ptvp, dxfer_buffer_out, opts.dataout_len);
    }
    if (opts.do_datain) {
        dxfer_buffer_in = my_memalign(opts.datain_len, &wrkBuf);
        if (dxfer_buffer_in == NULL) {
            perror("malloc");
            ret = SG_LIB_CAT_OTHER;
            goto done;
        }
        set_scsi_pt_data_in(ptvp, dxfer_buffer_in, opts.datain_len);
    }

    ret = do_scsi_pt(ptvp, sg_fd, opts.timeout, opts.do_verbose);
    if (ret > 0) {
        if (SCSI_PT_DO_BAD_PARAMS == ret) {
            fprintf(stderr, "do_scsi_pt: bad pass through setup\n");
            ret = SG_LIB_CAT_OTHER;
        } else if (SCSI_PT_DO_TIMEOUT == ret) {
            fprintf(stderr, "do_scsi_pt: timeout\n");
            ret = SG_LIB_CAT_TIMEOUT;
        } else
            ret = SG_LIB_CAT_OTHER;
        goto done;
    } else if (ret < 0) {
        fprintf(stderr, "do_scsi_pt: %s\n", safe_strerror(-ret));
        ret = SG_LIB_CAT_OTHER;
        goto done;
    }

    slen = 0;
    res_cat = get_scsi_pt_result_category(ptvp);
    switch (res_cat) {
    case SCSI_PT_RESULT_GOOD:
        ret = 0;
        break;
    case SCSI_PT_RESULT_SENSE:
        slen = get_scsi_pt_sense_len(ptvp);
        ret = sg_err_category_sense(sense_buffer, slen);
        break;
    case SCSI_PT_RESULT_TRANSPORT_ERR:
        get_scsi_pt_transport_err_str(ptvp, sizeof(b), b);
        fprintf(sg_warnings_strm, ">>> transport error: %s\n", b);
        ret = SG_LIB_CAT_OTHER;
        break;
    case SCSI_PT_RESULT_OS_ERR:
        get_scsi_pt_os_err_str(ptvp, sizeof(b), b);
        fprintf(sg_warnings_strm, ">>> os error: %s\n", b);
        ret = SG_LIB_CAT_OTHER;
        break;
    default:
        fprintf(sg_warnings_strm, ">>> unknown pass through result "
                "category (%d)\n", res_cat);
        ret = SG_LIB_CAT_OTHER;
        break;
    }

    status = get_scsi_pt_status_response(ptvp);
    fprintf(stderr, "SCSI Status: ");
    sg_print_scsi_status(status);
    fprintf(stderr, "\n\n");
    if ((SAM_STAT_CHECK_CONDITION == status) && (! opts.no_sense)) {
        if (SCSI_PT_RESULT_SENSE != res_cat)
            slen = get_scsi_pt_sense_len(ptvp);
        if (0 == slen)
            fprintf(stderr, ">>> Strange: status is CHECK CONDITION but no "
                    "Sense Information\n");
        else {
            fprintf(stderr, "Sense Information:\n");
            sg_print_sense(NULL, sense_buffer, slen, (opts.do_verbose > 0));
            fprintf(stderr, "\n");
        }
    }

    if (opts.do_datain) {
        int data_len = opts.datain_len - get_scsi_pt_resid(ptvp);
        if (data_len == 0) {
            fprintf(stderr, "No data received\n");
        } else {
            if (opts.datain_file == NULL && !opts.datain_binary) {
                fprintf(stderr, "Received %d bytes of data:\n", data_len);
                if (opts.do_verbose) {
                    dStrHex((const char *)dxfer_buffer_in, data_len, 0);
                }
                const unsigned char * result = (const unsigned char *)dxfer_buffer_in;
                int len = (result[0] << 8) | result[1];
                int off = 0;
                printf("Avilable density support length: %d\n", len);
                off += 4;
                while (off+52 <= data_len)
                {
                    printf("\nDensity #%d\n", (off-4)/52);
                    printf("Primary density code: %#x\n", result[off]);
                    printf("Secondary density code: %#x\n", result[off+1]);
                    printf("Flags: ");
                    if (result[off+2] & 128) printf("WRTOK ");
                    if (result[off+2] & 64) printf("DUP ");
                    if (result[off+2] & 32) printf("DEFLT ");
                    if (result[off+2] & 1) printf("DLV");
                    printf("\n");
                    printf("Descriptor length: %d\n", (result[off+3] <<8) | result[off+4]);
                    printf("Bits per mm: %ld\n", ((long int) result[off+5] <<16) | (result[off+6] <<8) | result[off+7]);
                    printf("Media width: %d\n", (result[off+8] <<8) | result[off+9]);
                    printf("Tracks: %d\n", (result[off+10] <<8) | result[off+11]);
                    printf("Capacity: %'ld MB\n", ((long int)result[off+12] <<24) | (result[off+13] <<16) | (result[off+14] <<8) | result[off+15]);
                    printf("Organization: %.*s\n", 8, &result[off+16]);
                    printf("Density name: %.*s\n", 8, &result[off+24]);
                    printf("Description: %.*s\n", 20, &result[off+32]);
                    off += 52;
                }
            } else {
                const char * cp = "stdout";

                if (opts.datain_file &&
                    ! ((1 == strlen(opts.datain_file)) &&
                       ('-' == opts.datain_file[0])))
                    cp = opts.datain_file;
                fprintf(stderr, "Writing %d bytes of data to %s\n", data_len,
                        cp);
                ret2 = write_dataout(opts.datain_file, dxfer_buffer_in,
                                     data_len);
                if (0 != ret2) {
                    if (0 == ret)
                        ret = ret2;
                    goto done;
                }
            }
        }
    }

done:
    if (wrkBuf)
        free(wrkBuf);
    if (ptvp)
        destruct_scsi_pt_obj(ptvp);
    if (sg_fd >= 0)
        scsi_pt_close_device(sg_fd);
    return ret;
}
