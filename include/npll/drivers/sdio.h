/*
 * NPLL - Drivers - SDIO
 *
 * Derived from seL4 projects_libs libsdhcdrivers:
 * Copyright 2017, Data61, CSIRO (ABN 41 687 119 230)
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef _DRIVERS_SDIO_H
#define _DRIVERS_SDIO_H

#include <npll/types.h>

/* NOT to be confused with SDHC. This API is universal */


/* for Hollywood/Latte main SDHC */
/* TODO: add one for the SDIO wifi and Wii U emmc at one point? */
#define SDHC0_ADDR (void *)0xcd070000
#define SDHC0_SIZE  0x1000
enum sdio_id {
	SDHC0,
	NSDHC,
	SDHC_DEFAULT = SDHC0
};


/* Present State Register */
#define SDHC_PRES_STATE_DAT3         (1 << 23)
#define SDHC_PRES_STATE_DAT2         (1 << 22)
#define SDHC_PRES_STATE_DAT1         (1 << 21)
#define SDHC_PRES_STATE_DAT0         (1 << 20)
#define SDHC_PRES_STATE_WPSPL        (1 << 19) //Write Protect Switch Pin Level
#define SDHC_PRES_STATE_CDPL         (1 << 18) //Card Detect Pin Level
#define SDHC_PRES_STATE_CINST        (1 << 16) //Card Inserted
#define SDHC_PRES_STATE_BWEN         (1 << 10) //Buffer Write Enable
#define SDHC_PRES_STATE_RTA          (1 << 9)  //Read Transfer Active
#define SDHC_PRES_STATE_WTA          (1 << 8)  //Write Transfer Active
#define SDHC_PRES_STATE_SDSTB        (1 << 3)  //SD Clock Stable
#define SDHC_PRES_STATE_DLA          (1 << 2)  //Data Line Active
#define SDHC_PRES_STATE_CDIHB        (1 << 1)  //Command Inhibit(DATA)
#define SDHC_PRES_STATE_CIHB         (1 << 0)  //Command Inhibit(CMD)

/* TODO turn this into sdio_cmd */
struct mmc_cmd;
struct sdio_host_dev;
typedef void (*sdio_cb)(struct sdio_host_dev *sdio, int status, struct mmc_cmd *cmd, void *token);

struct sdio_host_dev {
    int (*reset)(struct sdio_host_dev *sdio);
    int (*set_operational)(struct sdio_host_dev *sdio);
    int (*send_command)(struct sdio_host_dev *sdio, struct mmc_cmd *cmd, sdio_cb cb, void *token);
    int (*handle_irq)(struct sdio_host_dev *sdio, int irq);
    int (*is_voltage_compatible)(struct sdio_host_dev *sdio, int mv);
    int (*nth_irq)(struct sdio_host_dev *sdio, int n);
    u32 (*get_present_state)(struct sdio_host_dev *sdio);

    void *priv;
};
typedef struct sdio_host_dev sdio_host_dev_t;

/**
 * Send a command to an attached device
 * @param[in] sdio  A handle to an initialised SDIO driver
 * @param[in] cmd   A structure that has been filled to represent the command
 *                  that should be sent.
 * @param[in] cb    A function to be called onces the command has been accepted
 *                  by the connected device. NULL will cause the call to block
 *                  untill the command has been accepted.
 * @param[in] token A token to pass, unmodified, to the provided callback function.
 * @return          1 if the provided voltage level is supported
 */
static inline int sdio_send_command(sdio_host_dev_t *sdio, struct mmc_cmd *cmd, sdio_cb cb, void *token)
{
    return sdio->send_command(sdio, cmd, cb, token);
}

/**
 * Confirm if an SDIO device supports a specific voltage
 * @param[in] sdio A handle to an initialised SDIO driver
 * @param[in] mv   The voltage to be queried, in millivolts
 * @return         1 if the provided voltage level is supported
 */
static inline int sdio_is_voltage_compatible(sdio_host_dev_t *sdio, int mv)
{
    return sdio->is_voltage_compatible(sdio, mv);
}

/**
 * Resets the provided SDIO device
 * @param[in] sdio A handle to an initialised SDIO driver
 * @return         0 on success
 */
static inline int sdio_reset(sdio_host_dev_t *sdio)
{
    return sdio->reset(sdio);
}

/**
 * Set the SDIO device to an operational state
 * @param[in] sdio A handle to an initialised SDIO driver
 * @return         0 on success
 */
static inline int sdio_set_operational(sdio_host_dev_t *sdio)
{
    return sdio->set_operational(sdio);
}

/**
 * Returns the nth IRQ that this device generates
 * @param[in] sdio A handle to an initialised SDIO driver
 * @param[in] n    Index of the desired IRQ.
 * @return         The IRQ number, or -1 if n is invalid
 */
static inline int sdio_nth_irq(sdio_host_dev_t *sdio, int n)
{
    return sdio->nth_irq(sdio, n);
}

/**
 * @brief   Returns Present State Register's value
 *
 * @return  Value of the Present State Register, see SD specification for more
 *          details.
 */
static inline u32 sdio_get_present_state(
    sdio_host_dev_t *sdio //!< [in] Sdio handle.
)
{
    return sdio->get_present_state(sdio);
}

/**
 * Passes control to the IRQ handler of the provided SDIO device
 * @param[in] sdio A handle to an initialised SDIO driver
 * @param[in] irq  The IRQ number that was triggered.
 * @return         0 if an IRQ was handled
 */
static inline int sdio_handle_irq(sdio_host_dev_t *sdio, int irq)
{
    return sdio->handle_irq(sdio, irq);
}

/**
 * Returns the ID of the default SDIO device for the current platform
 * @return  The default SDIO device for the platform
 */
enum sdio_id sdio_default_id(void);

/**
 * Initialises a platform sdio device
 * @param[in]  id     The ID of the sdio device to initialise
 * @param[out] dev    An sdio structure to populate.
 * @return            0 on success
 */
int sdio_init(enum sdio_id id, sdio_host_dev_t *dev);

#endif /* _DRIVERS_SDIO_H */
