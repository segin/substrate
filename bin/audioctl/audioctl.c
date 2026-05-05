/*
 * audioctl.c - audio device control utility
 *
 * Display and set audio device parameters via AUDIO_GETINFO / AUDIO_SETINFO.
 *
 * Usage:
 *   audioctl [-d device] [-a] [-n]
 *   audioctl [-d device] name=value [name=value ...]
 *
 * Options:
 *   -d device    device to control (default /dev/audioctl0)
 *   -a           show all fields including read-only statistics
 *   -n           suppress output after setting values
 *
 * Variables (settable):
 *   play.gain           playback gain 0-255
 *   play.balance        playback balance 0(left)-32(mid)-64(right)
 *   play.sample_rate    sample rate in Hz
 *   play.channels       channel count
 *   play.precision      bits per sample
 *   play.encoding       encoding (ulaw/alaw/slinear_le/slinear_be/ulinear)
 *   play.pause          pause/resume playback (0 or 1)
 *   record.gain         record gain 0-255
 *   record.balance      record balance
 *   record.sample_rate  record sample rate
 *   record.channels     record channel count
 *   record.precision    record bits per sample
 *   record.encoding     record encoding
 *   record.pause        pause/resume recording
 *   monitor_gain        loopback monitor gain 0-255
 *
 * Shorthands:
 *   volume              alias for play.gain
 *   monitor             alias for monitor_gain
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>

#define DEFAULT_DEVICE "/dev/audioctl0"

static void __attribute__((noreturn))
die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}

/* ------------------------------------------------------------------ */
/* Encoding name table                                                   */
/* ------------------------------------------------------------------ */

typedef struct { const char *name; unsigned enc; } enc_entry_t;

static const enc_entry_t enc_table[] = {
    { "ulaw",       AUDIO_ENCODING_ULAW         },
    { "alaw",       AUDIO_ENCODING_ALAW         },
    { "pcm16",      AUDIO_ENCODING_PCM16        },
    { "pcm8",       AUDIO_ENCODING_PCM8         },
    { "adpcm",      AUDIO_ENCODING_ADPCM        },
    { "slinear_le", AUDIO_ENCODING_SLINEAR_LE   },
    { "slinear_be", AUDIO_ENCODING_SLINEAR_BE   },
    { "ulinear_le", AUDIO_ENCODING_ULINEAR_LE   },
    { "ulinear_be", AUDIO_ENCODING_ULINEAR_BE   },
    { "slinear",    AUDIO_ENCODING_SLINEAR      },
    { "ulinear",    AUDIO_ENCODING_ULINEAR      },
    { NULL, 0 }
};

static const char *
enc_name(unsigned enc)
{
    for (int i = 0; enc_table[i].name; i++)
        if (enc_table[i].enc == enc) return enc_table[i].name;
    return "unknown";
}

static int
enc_parse(const char *s, unsigned *out)
{
    for (int i = 0; enc_table[i].name; i++) {
        if (strcmp(s, enc_table[i].name) == 0) {
            *out = enc_table[i].enc;
            return 0;
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Display                                                               */
/* ------------------------------------------------------------------ */

static void
print_prinfo(const char *dir, const audio_prinfo_t *p, int all)
{
    printf("%s.sample_rate=%u\n",  dir, p->sample_rate);
    printf("%s.channels=%u\n",     dir, p->channels);
    printf("%s.precision=%u\n",    dir, p->precision);
    printf("%s.encoding=%s\n",     dir, enc_name(p->encoding));
    printf("%s.gain=%u\n",         dir, p->gain);
    printf("%s.balance=%u\n",      dir, p->balance);
    printf("%s.pause=%u\n",        dir, p->pause);
    if (all) {
        printf("%s.port=%u\n",     dir, p->port);
        printf("%s.avail_ports=%u\n", dir, p->avail_ports);
        printf("%s.buffer_size=%u\n", dir, p->buffer_size);
        printf("%s.samples=%u\n",  dir, p->samples);
        printf("%s.eof=%u\n",      dir, p->eof);
        printf("%s.error=%u\n",    dir, p->error);
        printf("%s.open=%u\n",     dir, p->open);
        printf("%s.active=%u\n",   dir, p->active);
    }
}

static void
print_info(const audio_info_t *info, int all)
{
    print_prinfo("play",   &info->play,   all);
    print_prinfo("record", &info->record, all);
    printf("monitor_gain=%u\n", info->monitor_gain);
    if (all) {
        printf("blocksize=%u\n", info->blocksize);
        printf("hiwat=%u\n",     info->hiwat);
        printf("lowat=%u\n",     info->lowat);
    }
}

/* ------------------------------------------------------------------ */
/* Set one variable                                                      */
/* ------------------------------------------------------------------ */

static int
apply_var(audio_info_t *info, const char *var, const char *valstr)
{
    unsigned long uval = 0;
    int is_num = 1;

    /* Try to parse numeric value */
    char *end;
    uval = strtoul(valstr, &end, 10);
    if (*end != '\0') is_num = 0;

    /* Shorthands */
    if (strcmp(var, "volume") == 0) var = "play.gain";
    if (strcmp(var, "monitor") == 0) var = "monitor_gain";

    /* play.* */
    if (strcmp(var, "play.gain") == 0) {
        if (!is_num || uval > 255) { fprintf(stderr, "gain: 0-255\n"); return -1; }
        info->play.gain = (uint32_t)uval;
    } else if (strcmp(var, "play.balance") == 0) {
        if (!is_num || uval > AUDIO_RIGHT_BAL) {
            fprintf(stderr, "balance: 0-%d\n", AUDIO_RIGHT_BAL); return -1;
        }
        info->play.balance = (uint8_t)uval;
    } else if (strcmp(var, "play.sample_rate") == 0) {
        if (!is_num) { fprintf(stderr, "sample_rate: numeric\n"); return -1; }
        info->play.sample_rate = (uint32_t)uval;
    } else if (strcmp(var, "play.channels") == 0) {
        if (!is_num || uval < 1) { fprintf(stderr, "channels: >=1\n"); return -1; }
        info->play.channels = (uint32_t)uval;
    } else if (strcmp(var, "play.precision") == 0) {
        if (!is_num) { fprintf(stderr, "precision: numeric\n"); return -1; }
        info->play.precision = (uint32_t)uval;
    } else if (strcmp(var, "play.encoding") == 0) {
        unsigned enc;
        if (enc_parse(valstr, &enc) < 0) {
            fprintf(stderr, "unknown encoding '%s'\n", valstr); return -1;
        }
        info->play.encoding = enc;
    } else if (strcmp(var, "play.pause") == 0) {
        if (!is_num || uval > 1) { fprintf(stderr, "pause: 0 or 1\n"); return -1; }
        info->play.pause = (uint8_t)uval;
    }
    /* record.* */
    else if (strcmp(var, "record.gain") == 0) {
        if (!is_num || uval > 255) { fprintf(stderr, "gain: 0-255\n"); return -1; }
        info->record.gain = (uint32_t)uval;
    } else if (strcmp(var, "record.balance") == 0) {
        if (!is_num || uval > AUDIO_RIGHT_BAL) {
            fprintf(stderr, "balance: 0-%d\n", AUDIO_RIGHT_BAL); return -1;
        }
        info->record.balance = (uint8_t)uval;
    } else if (strcmp(var, "record.sample_rate") == 0) {
        if (!is_num) { fprintf(stderr, "sample_rate: numeric\n"); return -1; }
        info->record.sample_rate = (uint32_t)uval;
    } else if (strcmp(var, "record.channels") == 0) {
        if (!is_num || uval < 1) { fprintf(stderr, "channels: >=1\n"); return -1; }
        info->record.channels = (uint32_t)uval;
    } else if (strcmp(var, "record.precision") == 0) {
        if (!is_num) { fprintf(stderr, "precision: numeric\n"); return -1; }
        info->record.precision = (uint32_t)uval;
    } else if (strcmp(var, "record.encoding") == 0) {
        unsigned enc;
        if (enc_parse(valstr, &enc) < 0) {
            fprintf(stderr, "unknown encoding '%s'\n", valstr); return -1;
        }
        info->record.encoding = enc;
    } else if (strcmp(var, "record.pause") == 0) {
        if (!is_num || uval > 1) { fprintf(stderr, "pause: 0 or 1\n"); return -1; }
        info->record.pause = (uint8_t)uval;
    }
    /* device-level */
    else if (strcmp(var, "monitor_gain") == 0) {
        if (!is_num || uval > 255) { fprintf(stderr, "monitor_gain: 0-255\n"); return -1; }
        info->monitor_gain = (uint32_t)uval;
    } else {
        fprintf(stderr, "unknown variable '%s'\n", var);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                  */
/* ------------------------------------------------------------------ */

static void
usage(void)
{
    fputs(
        "usage: audioctl [-d device] [-a] [-n]\n"
        "       audioctl [-d device] [-n] name=value ...\n"
        "\n"
        "Options:\n"
        "  -d device   audio control device (default /dev/audioctl0)\n"
        "  -a          show all fields including statistics\n"
        "  -n          suppress output after setting\n"
        "\n"
        "Settable variables:\n"
        "  volume              play.gain alias (0-255)\n"
        "  play.gain           playback gain\n"
        "  play.balance        stereo balance (0=left 32=mid 64=right)\n"
        "  play.sample_rate    sample rate in Hz\n"
        "  play.channels       channel count\n"
        "  play.precision      bits per sample\n"
        "  play.encoding       encoding (slinear_le, ulaw, alaw, ...)\n"
        "  play.pause          pause playback (0 or 1)\n"
        "  record.gain         record gain\n"
        "  record.balance      record balance\n"
        "  record.sample_rate  record sample rate\n"
        "  record.channels     record channels\n"
        "  record.precision    record bits per sample\n"
        "  record.encoding     record encoding\n"
        "  record.pause        pause recording (0 or 1)\n"
        "  monitor_gain        monitor loopback gain\n",
        stderr);
}

int
main(int argc, char **argv)
{
    const char *device = DEFAULT_DEVICE;
    int all = 0, no_print = 0;

    int i = 1;
    for (; i < argc; i++) {
        if (argv[i][0] != '-') break;
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        const char *opt = argv[i] + 1;
        for (; *opt; opt++) {
            switch (*opt) {
            case 'a': all = 1; break;
            case 'n': no_print = 1; break;
            case 'd':
                if (opt[1]) { device = opt + 1; opt += strlen(opt) - 1; }
                else if (i + 1 < argc) { device = argv[++i]; }
                else die("-d requires argument");
                break;
            case 'h': usage(); return 0;
            default:
                fprintf(stderr, "unknown option '-%c'\n", *opt);
                usage(); return 1;
            }
        }
    }

    int fd = open(device, O_RDONLY);
    if (fd < 0) die("open %s: %s", device, strerror(errno));

    audio_info_t info;
    if (ioctl(fd, AUDIO_GETINFO, &info) < 0) {
        close(fd);
        die("AUDIO_GETINFO: %s", strerror(errno));
    }

    if (i >= argc) {
        /* no assignments: show current state */
        print_info(&info, all);
    } else {
        /* parse name=value pairs */
        audio_info_t set;
        AUDIO_INITINFO(&set);

        int errors = 0;
        for (; i < argc; i++) {
            char *eq = strchr(argv[i], '=');
            if (!eq) {
                /* just show the variable */
                const char *var = argv[i];
                if (strcmp(var, "volume") == 0) var = "play.gain";
                if (strcmp(var, "monitor") == 0) var = "monitor_gain";

                if (strcmp(var, "play.gain") == 0)
                    printf("play.gain=%u\n", info.play.gain);
                else if (strcmp(var, "play.balance") == 0)
                    printf("play.balance=%u\n", info.play.balance);
                else if (strcmp(var, "play.sample_rate") == 0)
                    printf("play.sample_rate=%u\n", info.play.sample_rate);
                else if (strcmp(var, "play.channels") == 0)
                    printf("play.channels=%u\n", info.play.channels);
                else if (strcmp(var, "play.precision") == 0)
                    printf("play.precision=%u\n", info.play.precision);
                else if (strcmp(var, "play.encoding") == 0)
                    printf("play.encoding=%s\n", enc_name(info.play.encoding));
                else if (strcmp(var, "play.pause") == 0)
                    printf("play.pause=%u\n", info.play.pause);
                else if (strcmp(var, "record.gain") == 0)
                    printf("record.gain=%u\n", info.record.gain);
                else if (strcmp(var, "record.balance") == 0)
                    printf("record.balance=%u\n", info.record.balance);
                else if (strcmp(var, "record.sample_rate") == 0)
                    printf("record.sample_rate=%u\n", info.record.sample_rate);
                else if (strcmp(var, "record.channels") == 0)
                    printf("record.channels=%u\n", info.record.channels);
                else if (strcmp(var, "record.precision") == 0)
                    printf("record.precision=%u\n", info.record.precision);
                else if (strcmp(var, "record.encoding") == 0)
                    printf("record.encoding=%s\n", enc_name(info.record.encoding));
                else if (strcmp(var, "record.pause") == 0)
                    printf("record.pause=%u\n", info.record.pause);
                else if (strcmp(var, "monitor_gain") == 0)
                    printf("monitor_gain=%u\n", info.monitor_gain);
                else {
                    fprintf(stderr, "unknown variable '%s'\n", var);
                    errors++;
                }
                continue;
            }

            char var[128];
            size_t vlen = (size_t)(eq - argv[i]);
            if (vlen >= sizeof(var)) {
                fprintf(stderr, "variable name too long\n"); errors++; continue;
            }
            memcpy(var, argv[i], vlen); var[vlen] = '\0';
            const char *val = eq + 1;

            if (apply_var(&set, var, val) < 0)
                errors++;
        }

        if (errors) { close(fd); return 1; }

        if (ioctl(fd, AUDIO_SETINFO, &set) < 0) {
            close(fd);
            die("AUDIO_SETINFO: %s", strerror(errno));
        }

        if (!no_print) {
            /* re-read and show updated state */
            if (ioctl(fd, AUDIO_GETINFO, &info) < 0) {
                close(fd);
                die("AUDIO_GETINFO: %s", strerror(errno));
            }
            print_info(&info, all);
        }
    }

    close(fd);
    return 0;
}
