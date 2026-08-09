/*
 * audio.h - Substrate audio framework (kernel-internal).
 *
 * The framework owns the user-facing devfs nodes and ioctl dispatch;
 * each registered audio_dev_t plugs in a backend with a small ops
 * vector.  Backends do nothing but implement the actual hardware
 * mechanics (DMA programming, register pokes) and report capabilities;
 * format negotiation, parameter clamping, and AUDIO_INITINFO sentinel
 * handling all live in the framework so every driver behaves the same.
 */

#ifndef _DRIVERS_AUDIO_H
#define _DRIVERS_AUDIO_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/audioio.h>

#define AUDIO_MAX_DEVICES        4
#define AUDIO_DEFAULT_BLOCKSIZE  1024U
#define AUDIO_DEFAULT_HIWAT      8U
#define AUDIO_DEFAULT_LOWAT      4U

/*
 * Staging buffer for encoding conversion.  Backends are handed signed
 * 16-bit little-endian PCM whatever the application wrote, so anything
 * else is translated on the way through.  The worst case is a companded
 * or 8-bit unsigned stream, which doubles in size, hence the 2:1 ratio
 * between these two.
 */
#define AUDIO_CONV_IN            2048U
#define AUDIO_CONV_OUT           (AUDIO_CONV_IN * 2U)

struct audio_dev;

typedef struct audio_dev_ops {
	/*
	 * Optional: invoked when /dev/audioN is opened (mode is the OR of
	 * AUMODE_PLAY / AUMODE_RECORD).  Returns 0 on success or -errno.
	 */
	int (*open)(struct audio_dev *dev, int mode);

	/*
	 * Optional: invoked when the last fd against /dev/audioN closes.
	 */
	int (*close)(struct audio_dev *dev);

	/*
	 * Submit a block of samples for playback.  Backends may copy the
	 * data into a DMA buffer and return the byte count consumed.
	 * Returning a negative value is treated as -errno.
	 */
	int (*write)(struct audio_dev *dev, const void *buf, size_t len);

	/*
	 * Pull a block of samples from the capture path.  May return 0 if
	 * no data is currently available.
	 */
	int (*read)(struct audio_dev *dev, void *buf, size_t len);

	/*
	 * Validate a fully-resolved (post-AUDIO_INITINFO sentinel removal)
	 * audio_info_t and apply it.  The framework hands the backend a
	 * mutable copy; the backend may down-clamp values it can't honor
	 * and the framework will write the clamped result back to userspace
	 * on AUDIO_GETINFO.  Return 0 on success, -EINVAL if the requested
	 * configuration is fundamentally unsupported.
	 */
	int (*set_params)(struct audio_dev *dev, audio_info_t *info);

	/*
	 * Block until in-flight playback drains.  No-op for backends that
	 * don't buffer.
	 */
	int (*drain)(struct audio_dev *dev);

	/*
	 * Discard any queued data without draining.
	 */
	int (*flush)(struct audio_dev *dev);

	/*
	 * Fill the audio_device_t identification block (name/version/config
	 * strings).
	 */
	void (*get_devinfo)(struct audio_dev *dev, audio_device_t *out);

	/*
	 * Return the AUDIO_PROP_* bitmap for this backend.
	 */
	int (*get_props)(struct audio_dev *dev);

	/*
	 * Optional: map the backend's playback DMA buffer into the calling
	 * process for zero-copy (OSS/SADA-style) mmap playback.  Mirrors the
	 * fs_node_t.mmap contract: returns the user virtual address on success
	 * or (void *)-1 on failure.  Backends that implement this typically
	 * switch into a continuous-loop DMA mode where the controller cycles
	 * the whole buffer and plays whatever userspace writes into the
	 * mapping, bypassing the write()/FIFO path.
	 */
	void *(*mmap)(struct audio_dev *dev, void *addr, size_t length,
		      int prot, int flags, off_t offset);

	/*
	 * Optional: report the playback FIFO's current occupancy for the OSS
	 * SNDCTL_DSP_GETOSPACE ioctl.  On success returns 0 and fills:
	 *   *fragsize        — bytes per fragment (the DMA chunk size)
	 *   *fragstotal      — total fragments the output buffer holds
	 *   *fragments_avail — fragments currently free for writing
	 *   *bytes_avail     — free bytes currently writable without blocking
	 * Backends that don't implement it let the OSS frontend synthesize a
	 * value from the framework-level blocksize/hiwat.  Returns -errno on
	 * failure.
	 */
	int (*get_ospace)(struct audio_dev *dev, int *fragsize, int *fragstotal,
			  int *fragments_avail, int *bytes_avail);
} audio_dev_ops_t;

typedef struct audio_dev {
	char            name[64];
	int             unit;          /* 0..AUDIO_MAX_DEVICES-1 */
	audio_dev_ops_t *ops;
	void           *driver_data;
	audio_info_t    current;       /* live state */
	int             open_refs;
	int             full_duplex;
	/*
	 * Exclusive playback owner.  The software FIFO / DMA path is
	 * single-producer; a second process write()ing concurrently corrupts it
	 * and both wedge in an uninterruptible D-state.  The first writer claims
	 * the device; another process's write() returns -EBUSY.  Released when
	 * the owner closes the device (or exits).  NULL == unclaimed; compared by
	 * process_t* (current_thread->proc).
	 */
	void            *play_owner;
	/*
	 * Encoding-conversion staging.  Only touched by the exclusive
	 * playback owner (see play_owner), so no additional locking.
	 */
	uint8_t          conv_buf[AUDIO_CONV_OUT];
	struct audio_dev *next;
} audio_dev_t;

/*
 * Translate a requested playback format into the one the backend is
 * actually programmed for.  Backends only ever see signed 16-bit
 * little-endian PCM (or the caller's format unchanged when it already is
 * that), so companded and unsigned and big-endian streams are converted
 * by the framework rather than handed to hardware that cannot render
 * them.  Exposed for tests.
 */
void audio_hw_prinfo(const audio_prinfo_t *sw, audio_prinfo_t *hw);

/*
 * Convert up to `in_len` bytes of `enc`/`prec` PCM into signed 16-bit
 * little-endian in `out`, returning the number of output bytes.  `out`
 * must have room for in_len * audio_conv_ratio(enc, prec) bytes.
 */
size_t audio_convert(uint32_t enc, uint32_t prec, const uint8_t *in,
		     size_t in_len, uint8_t *out);

/* Output bytes produced per input byte: 2 for 8-bit sources, else 1. */
unsigned audio_conv_ratio(uint32_t enc, uint32_t prec);

void audio_init(void);
int  audio_register_device(audio_dev_t *dev);
void audio_unregister_device(audio_dev_t *dev);

/*
 * Returns non-zero if at least one backend has registered through
 * audio_register_device().  null_audio uses this to skip registration
 * when a real backend already grabbed unit 0.
 */
int  audio_have_device(void);

/*
 * Apply a SETINFO payload onto a base audio_info_t, copying only fields
 * whose value differs from the AUDIO_NOTSET_* sentinel.  Exposed so
 * tests and ioctls share the same merge semantics.
 */
void audio_merge_info(audio_info_t *base, const audio_info_t *overlay);

/*
 * Initialize a fresh audio_info_t with backend-appropriate defaults
 * (44.1 kHz stereo signed 16-bit, mid-balance, mid-gain).
 */
void audio_default_info(audio_info_t *info);

/*
 * Validate an audio_info_t and clamp values to ranges the framework
 * accepts.  Returns 0 on success or -EINVAL for unrecoverable inputs
 * (e.g. encoding the framework doesn't recognize).
 */
int audio_validate_info(audio_info_t *info);

/*
 * The shared ioctl dispatcher used by both /dev/audioN and
 * /dev/audioctlN.  Backends never override this; they wire it as the
 * fs_node_t.ioctl callback through audio_register_device().  Kept
 * non-static so host tests can call it without devfs.
 */
int audio_ioctl_dispatch(audio_dev_t *dev, uint32_t request, void *arg);

/*
 * Shared fs_node_t callbacks implemented by the framework (audio.c).  The
 * Sun/SADA /dev/audioN nodes and the OSS /dev/dspN nodes both point their
 * read/write/open/close/mmap callbacks at these — only the ioctl callback
 * differs (Sun/SADA vs OSS).  Both node families set node->impl to the
 * shared audio_dev_t, so exclusive-playback arbitration and the backend
 * write path are common to every frontend.
 */
struct fs_node;
size_t audio_node_write(struct fs_node *node, off_t offset, size_t size,
			const uint8_t *buffer);
size_t audio_node_read(struct fs_node *node, off_t offset, size_t size,
		       uint8_t *buffer);
void   audio_node_open(struct fs_node *node);
void   audio_node_close(struct fs_node *node);
void  *audio_node_mmap(struct fs_node *node, void *addr, size_t length,
		       int prot, int flags, off_t offset);

/*
 * OSS (/dev/dsp) frontend, implemented in oss.c.  audio_register_device()
 * calls oss_register_device() so every backend that publishes /dev/audioN
 * also publishes the matching /dev/dspN with the OSS ioctl ABI.
 */
void oss_register_device(audio_dev_t *dev, int unit);
int  oss_ioctl_dispatch(audio_dev_t *dev, uint32_t request, void *arg);

void null_audio_init(void);
void ac97_init(void);
void sb16_init(void);
void hda_init(void);

#endif /* _DRIVERS_AUDIO_H */
