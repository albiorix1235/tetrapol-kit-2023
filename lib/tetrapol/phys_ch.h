#pragma once

#include <stdint.h>
#include <stdbool.h>

#include <tetrapol/tetrapol.h>

#define PHYS_CH_SCR_DETECT -1

typedef struct phys_ch_priv_t phys_ch_t;

/**
  Create new TETRAPOL physical channel instance.
  @param band VHF or UHF
  @param phys_ch_type Radio channel type, control or traffic.

  @return net phys_ch_t instance of NULL.
  */
phys_ch_t *tetrapol_phys_ch_create(tetrapol_t *tetrapol);
void tetrapol_phys_ch_destroy(phys_ch_t *phys_ch);
int tetrapol_phys_ch_process(phys_ch_t *phys_ch);

/** Get SCR, scrambling constant parameter. */
int tetrapol_phys_ch_get_scr(phys_ch_t *phys_ch);

/** Set SCR, scrambling constant parameter. */
void tetrapol_phys_ch_set_scr(phys_ch_t *phys_ch, int scr);

/** Get confidence for SRC detection (~ no. of valid frames). */
int tetrapol_phys_ch_get_scr_confidence(phys_ch_t *phys_ch);

/** Set confidence for SRC detection (~ no. of valid frames). */
void tetrapol_phys_ch_set_scr_confidence(phys_ch_t *phys_ch, int scr_confidence);

/**
  Eat some data from buf into channel decoder.

  @return number of bytes consumed
*/
int tetrapol_phys_ch_recv(phys_ch_t *phys_ch, uint8_t *buf, int len);

