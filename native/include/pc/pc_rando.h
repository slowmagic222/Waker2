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
/* The item at `offset` of rels/<rel>.rel ("d_a_auction") on the randomizer's disc; `vanilla` (the
 * original game's item) without one, or if that byte cannot be read. */
unsigned char pc_rando_rel_item(const char* rel, unsigned int offset, unsigned char vanilla);
/* The item at `address` of the randomizer's main.dol; `vanilla` as above. */
unsigned char pc_rando_dol_item(unsigned int address, unsigned char vanilla);

#ifdef __cplusplus
}
#endif

#endif /* PC_RANDO_H */
