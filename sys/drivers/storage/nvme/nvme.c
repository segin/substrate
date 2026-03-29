#include <drivers/storage/nvme/nvme.h>

#include <kern/console.h>
#include <kern/device.h>
#include <kern/pci.h>

#include <stdio.h>
#include <string.h>

#define NVME_PCI_CLASS_STORAGE 0x01
#define NVME_PCI_SUBCLASS_NVM  0x08
#define NVME_PCI_PROGIF_NVME   0x02

static nvme_controller_t nvme_controllers[NVME_MAX_CONTROLLERS];
static size_t nvme_controller_total;

static uint64_t nvme_mmio_read64(volatile uint8_t *mmio, uint32_t reg) {
    volatile uint32_t *regs = (volatile uint32_t *)(mmio + reg);

    return (uint64_t)regs[0] | ((uint64_t)regs[1] << 32);
}

int nvme_decode_cap(uint64_t cap_raw, nvme_capability_t *cap) {
    if (cap == NULL) {
        return -1;
    }

    memset(cap, 0, sizeof(*cap));
    cap->mqes = (uint16_t)(cap_raw & 0xFFFFU);
    cap->timeout = (uint8_t)((cap_raw >> 24) & 0xFFU);
    cap->doorbell_stride = (uint8_t)((cap_raw >> 32) & 0x0FU);
    cap->timeout_ms = (uint32_t)cap->timeout * 500U;
    cap->doorbell_stride_bytes = 4U << cap->doorbell_stride;
    return 0;
}

size_t nvme_scan_controllers(nvme_controller_t *controllers, size_t max_controllers) {
    pci_device_t *pdev;
    size_t count = 0;

    if (controllers == NULL || max_controllers == 0) {
        return 0;
    }

    memset(controllers, 0, sizeof(*controllers) * max_controllers);

    for (pdev = pci_first_device(); pdev != NULL; pdev = pci_next_device(pdev)) {
        volatile uint8_t *mmio;
        uint16_t command;
        uint16_t want;
        nvme_controller_t *ctrl;
        char log_buf[128];

        if (count >= max_controllers) {
            break;
        }
        if (pdev->kdev == NULL) {
            continue;
        }
        if (pdev->kdev->class != NVME_PCI_CLASS_STORAGE ||
            pdev->kdev->subclass != NVME_PCI_SUBCLASS_NVM ||
            pdev->kdev->progif != NVME_PCI_PROGIF_NVME) {
            continue;
        }

        command = pci_read_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND);
        want = (uint16_t)(command | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
        if (want != command) {
            pci_write_config16(pdev->bus, pdev->slot, pdev->func, PCI_CONFIG_COMMAND, want);
        }

        mmio = (volatile uint8_t *)pci_iomap(pdev, 0, 0);
        if (mmio == NULL) {
            continue;
        }

        ctrl = &controllers[count];
        ctrl->bus = pdev->bus;
        ctrl->slot = pdev->slot;
        ctrl->func = pdev->func;
        ctrl->vendor_id = pdev->vendor_id;
        ctrl->device_id = pdev->device_id;
        ctrl->mmio = mmio;
        ctrl->cap_raw = nvme_mmio_read64(mmio, NVME_REG_CAP);
        ctrl->present = 1;
        if (nvme_decode_cap(ctrl->cap_raw, &ctrl->cap) < 0) {
            continue;
        }

        snprintf(log_buf, sizeof(log_buf),
                 "nvme: %u:%u.%u cap.to=%u (%ums) dstrd=%u (%uB)\n",
                 ctrl->bus, ctrl->slot, ctrl->func,
                 ctrl->cap.timeout, ctrl->cap.timeout_ms,
                 ctrl->cap.doorbell_stride,
                 ctrl->cap.doorbell_stride_bytes);
        kprint(log_buf);
        count++;
    }

    return count;
}

size_t nvme_controller_count(void) {
    return nvme_controller_total;
}

const nvme_controller_t *nvme_get_controller(size_t index) {
    if (index >= nvme_controller_total) {
        return NULL;
    }
    return &nvme_controllers[index];
}

void nvme_init(void) {
    nvme_controller_total = nvme_scan_controllers(nvme_controllers,
                                                  NVME_MAX_CONTROLLERS);

    if (nvme_controller_total == 0) {
        kprint("nvme: no controllers detected\n");
    }
}
