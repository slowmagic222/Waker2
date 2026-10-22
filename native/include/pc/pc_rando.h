/*
 * pc_rando.h - rando mode (docs/RANDO.md): what a Wind Waker Randomizer seed changes in the game's
 * code, read from the randomizer's disc (COS_RANDO_DISC).
 *
 * wwrando writes the item of a location given by code (an NPC, a minigame, the mailbox) as one byte
 * into main.dol or a REL (its logic/item_locations.txt paths "rels/<rel>.rel@<offset>" and
 * "main.dol@<address>"). The port does not run that code, so the game's hunk for such a location
 * asks here instead, with the same path: the byte at that place of the randomizer's file, or the
 * original item without a randomizer disc. The game thread calls these.
 */
#ifndef PC_RANDO_H
#define PC_RANDO_H

#ifdef __cplusplus
extern "C" {
#endif

/* 1 with COS_RANDO_DISC (a randomizer's disc is the game's disc), else 0. */
int pc_rando_active(void);

/* The wwrando versions the port reads. Their item locations given by code are at the same places (the newer one
 * has 5 more), but the data wwrando adds to main.dol (custom_symbols: the new-game settings) moved. Another version
 * is refused when the game starts (pc_disc.cpp). */
#define PC_RANDO_VERSION_1_10_0 1 /* the 1.10.0 release (ec39074, 2023-01-06) */
#define PC_RANDO_VERSION_9775811 2 /* the development version at 9775811 (2026-07-20) */
/* The randomizer disc's version, or 0 without a randomizer disc. */
int pc_rando_version(void);
/* The item at `offset` of rels/<rel>.rel ("d_a_auction") on the randomizer's disc; `vanilla` (the
 * original game's item) without one, or if that byte cannot be read. */
unsigned char pc_rando_rel_item(const char* rel, unsigned int offset, unsigned char vanilla);
/* The item at `address` of the randomizer's main.dol; `vanilla` as above. */
unsigned char pc_rando_dol_item(unsigned int address, unsigned char vanilla);
/* `size` bytes at `address` of the randomizer's main.dol (data wwrando adds there too, such as its
 * custom_symbols), or NULL without a randomizer disc or if main.dol has no such bytes. */
const unsigned char* pc_rando_dol_bytes(unsigned int address, unsigned int size);

/* The seed's options that change the game's code (step 5). The randomizer writes an option as an asm
 * patch, so an option is on when its patch is in the randomizer's main.dol or REL (the first
 * instruction or value the patch writes). 0 without a randomizer disc. */
typedef enum PcRandoOption {
    PC_RANDO_REMOVE_MUSIC = 0, /* remove_music.asm: no background, battle or boss music, no jingles */
    PC_RANDO_INVERT_CAMERA,    /* invert_camera_x_axis.asm */
    PC_RANDO_INVERT_COMPASS,   /* invert_sea_compass_x_axis.asm */
    PC_RANDO_REVEAL_CHART,     /* reveal_sea_chart.asm */
    PC_RANDO_SWIFT_SAIL,       /* swift_sail.asm */
    PC_RANDO_HERO_MODE,        /* hero_mode.asm */
    PC_RANDO_SWORDLESS,        /* swordless.asm */
    PC_RANDO_OPTIONS
} PcRandoOption;
int pc_rando_option(PcRandoOption which);
/* Hero mode's damage multiplier (the randomizer's damage_multiplier, 4.0), 1.0 without hero mode. */
float pc_rando_damage_multiplier(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_RANDO_H */
