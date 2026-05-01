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
#include <sys/audioio.h>

#define AUDIO_MAX_DEVICES        4
#define AUDIO_DEFAULT_BLOCKSIZE  1024U
#define AUDIO_DEFAULT_HIWAT      8U
#define AUDIO_DEFAULT_LOWAT      4U

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
} audio_dev_ops_t;

typedef struct audio_dev {
	char            name[64];
	int             unit;          /* 0..AUDIO_MAX_DEVICES-1 */
	audio_dev_ops_t *ops;
	void           *driver_data;
	audio_info_t    current;       /* live state */
	int             open_refs;
	int             full_duplex;
	struct audio_dev *next;
} audio_dev_t;

void audio_init(void);
int  audio_register_device(audio_dev_t *dev);
void audio_unregister_device(audio_dev_t *dev);

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

#endif /* _DRIVERS_AUDIO_H */
